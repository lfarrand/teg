/*
  eFlexPwm

  Teensy 4.1 PWM pins:

  | PWM | SubModule | Pol | Teensy | Native   |
  |-----|-----------|-----|--------|----------|
  | 1   | 3         | A   | 8      | B1_00    |
  | 1   | 3         | B   | 7      | B1_01    |
  | 2   | 0         | A   | 4      | EMC_06   |
  | 2   | 0         | B   | 33     | EMC_07   |
  | 2   | 1         | A   | 5      | EMC_08   |
  | 2   | 2         | A   | 6      | B0_10    |
  | 2   | 2         | B   | 9      | B0_11    |
  | 2   | 3         | A   | 36     | B1_02    |
  | 2   | 3         | B   | 37     | B1_03    |
  | 3   | 1         | A   | 29     | EMC_31   |
  | 3   | 1         | B   | 28     | EMC_32   |
  | 4   | 0         | A   | 22     | AD_B1_08 |
  | 4   | 1         | A   | 23     | AD_B1_09 |
  | 4   | 2         | A   | 2      | EMC_04   |
  | 4   | 2         | B   | 3      | EMC_05   |

  This program works under interrupt and takes advantage of the i.MX RT1062 computing power.
  The Teensy's LED is lit during the interrupt routine in order to be able to measure its execution time.

  PWM2 generates the signals used to drive a single phase full-bridge inverter.
  These are 2 pairs of SPWM signals (Sinusoidal Pulse Width Modulation)
  Each signal pair corresponds to the PWMA output and the PWMB (A's complement)
  output of an eFlexPWM submodule.
  Sub-modules 0 and 2 of PWM2 are used.

  Inverter switch arrangement
  1 (2.0A)      3 (2.2A)
  |             |
  -----[Load]----
  |             |
  4 (2.2A)      2 (2.0A)


eFlexPWM Info

Because each submodule has its own timer, it is possible for each submodule to run at a
different frequency. One of the options possible with this PWM module is to have one or
more submodules running at a lower frequency, but still synchronized to the timer in
submodule0.

Each complementary pair can operate with its own PWM frequency and deadtime
values.

Each eFlexPWM module has 4 sub-module, each sub-module has it's own Val0~Val5 register,
so each sub-module can generate different frequency PWM signals.

For the same sub-module, it's A/B/X three PWM signals must have the same frequency,
but can have different duty cycle.

The PWM has 2 channels: A and B. Each channel has its own duty cycle and level-mode specified,
however the same PWM period and PWM mode is applied to all channels requesting PWM output.
The signal duty cycle is provided as a percentage of the PWM period, its value should be between 0 and 100;
0=inactive signal(0% duty cycle) and 100=always active signal (100% duty cycle).
The function also sets up the channel dead time value which is used when the user selects
complementary mode of operation.

While in the complementary mode, a PWM pair can be used to drive top/bottom
transistors, as shown in the figure. When the top PWM channel is active, the bottom
PWM channel is inactive, and vice versa.

Note
To avoid short circuiting the DC bus and endangering the
transistor, there must be no overlap of conducting intervals
between top and bottom transistor. But the transistor's
characteristics may make its switching-off time longer than
switching-on time. To avoid the conducting overlap of top and
bottom transistors, deadtime needs to be inserted in the
switching period, as illustrated in the following figure.

The deadtime generators automatically insert software-selectable activation delays into
the pair of PWM outputs. The deadtime registers (DTCNT0 and DTCNT1) specify the
number of IPBus clock cycles to use for deadtime delay. Every time the deadtime
generator inputs change state, deadtime is inserted. Deadtime forces both PWM outputs
in the pair to the inactive state.
*/

// Core

#include <Arduino.h>
#include <arm_math.h>
#include <math.h>
#include <inttypes.h>
#include <limits.h>
#include <util/atomic.h>
#include <SdFat.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSans9pt7b.h>
#include <TeensyID.h>
#include <eFlexPwm.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <InternalTemperature.h>
#include <aWOT.h>
#include <HttpClient.h>
#include <TimeLib.h>
#include <ArduinoJson.h>
#include "defines.h"
#include <TeensyTimerTool.h>

#define TIMEOUT 3000;

static constexpr uint16_t FIFTY_PERCENT_DUTY = 32768;

// Configuration
struct ChannelConfig {
  uint32_t OnPeriodMicroseconds;
  uint16_t DutyCycle;
  uint8_t PhaseShift;
  bool Enabled = true;
};

struct SubmoduleConfig {
  uint16_t DeadTime;
  uint32_t PwmFrequency;
  ChannelConfig ChannelA;
  ChannelConfig ChannelB;
};

struct Module1Config {
  SubmoduleConfig Sm13;
};

struct Module2Config {
  bool UseSpwm = false;
  uint32_t SpwmCarrierFrequency = 20000;
  uint32_t SpwmModulationFrequency = 50;
  SubmoduleConfig Sm20{};
  SubmoduleConfig Sm21{};
  SubmoduleConfig Sm22{};
  SubmoduleConfig Sm23{};
};

struct Module3Config {
  SubmoduleConfig Sm31;
};

struct Module4Config {
  SubmoduleConfig Sm40;
  SubmoduleConfig Sm41;
  SubmoduleConfig Sm42;
};

struct PwmConfig {
  Module1Config Tm1{};
  Module2Config Tm2;
  Module3Config Tm3{};
  Module4Config Tm4{};
  bool PrintRegs = false;
  bool SyncPwm = false;
};

struct MainConfig {
  PwmConfig Pwm;
};

const char *filename = "/settings.cfg";  // <- SD library uses 8.3 filenames
MainConfig config;

// Ethernet
byte mac[] = { 0x04, 0xE9, 0xE5, 0x14, 0x7C, 0xB0 };
static constexpr uint32_t kDHCPTimeout = 15000;

// Web server
#define BUFFER_SIZE 32768
constexpr uint16_t kServerPort = 80;
EthernetServer server(kServerPort);
int reqCount = 0; // number of requests received
Application app;

// NTP
char timeServer[] = "uk.pool.ntp.org";
EthernetUDP ntpUDP;
constexpr uint16_t localPort = 8888;  // local port to listen for UDP packets

// Data wire is plugged into pin 34 on the Arduino
#define ONE_WIRE_BUS 34
// The resolution of the temperature sensor is user-configurable to 9, 10, 11, or 12 bits, corresponding to increments of 0.5°C, 0.25°C, 0.125°C, and 0.0625°C, respectively. The default resolution at power-up is 12-bit.
#define TEMP_PRECISION 9 // Range 9-12. Larger values are slower. 9 (0.5°C): 93.75ms, 10 (0.25°C): 187.5ms, 11 (0.125°C): 375ms, 12 (0.0625°C): 750ms
// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// OLED screen
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_ADDRESS 0x3C
#define OLED_RESET (-1)  // Reset pin # (or -1 if sharing Arduino reset pin)
// Port   SCL   SDA   Wire
// 0      19    18    Wire
// 1      16    17    Wire1
// 2      24    25    Wire2
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Sensors
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);
DeviceAddress Thermometer;
// TODO:
/*
It looks like you are hardcoding the device addresses, so I assume this is a static installation where the sensors are not being swapped in and out.
If so, you can save a lot of RAM by using PROGMEM to store the addresses in FLASH which is much less constrained.
You will need to make a very small edit to the library for it to work with addresses in PROGMEM.

// Array to hold device addresses.
// The address is a group of 8 bytes, and there are 'NUM_SENSORS' of those groups of 8 bytes.
// The "DeviceAddress" type already defined as a group of 8 bytes.
const DeviceAddress sensor_address[NUM_SENSORS] =
{
  { 0x28, 0xFF, 0xF8, 0x20, 0x05, 0x16, 0x03, 0x31 },       // Kitchen
  { 0x28, 0xFF, 0x1B, 0x22, 0x05, 0x16, 0x03, 0xA2 },       // Living room
  { 0x28, 0xFF, 0xFC, 0xF5, 0x04, 0x16, 0x03, 0x05 },       // Hall
  { 0x28, 0xFF, 0x58, 0x27, 0x05, 0x16, 0x03, 0x84 },       // Doghouse
  { 0x28, 0xFF, 0xE0, 0x25, 0x05, 0x16, 0x03, 0xF2 },       // Basement
};

// Create labels in PROGMEM, see : https://www.arduino.cc/en/Reference/PROGMEM
// The order of the labels must match the order of the addresses in 'sensor_address[]'.
const char string0[] PROGMEM = "Kitchen";
const char string1[] PROGMEM = "Living room";
const char string2[] PROGMEM = "Hall";
const char string3[] PROGMEM = "Doghouse";
const char string4[] PROGMEM = "Basement";

*/
struct temperatureSensor {
  DeviceAddress address;
  // Max string length is 11 characters +1 extra for NULL as string terminator
  const char addressString[24];
  const char name[20];
};

int tempSensorCount = 11;

temperatureSensor temperatureSensors[] = {
  {{ 0x28, 0x82, 0xDF, 0x7E, 0x0E, 0x00, 0x00, 0xE2 }, "28-82-DF-7E-0E-00-00-E2", "Ambient"},
  {{ 0x28, 0x1B, 0xAA, 0x7D, 0x0E, 0x00, 0x00, 0xF2 }, "28-1B-AA-7D-0E-00-00-F2", "S01-MOSFET"},
  {{ 0x28, 0xC8, 0xBD, 0x7D, 0x0E, 0x00, 0x00, 0x42 }, "28-C8-BD-7D-0E-00-00-42", "S01-GateDriver"},
  {{ 0x28, 0x6F, 0x9C, 0x7E, 0x0E, 0x00, 0x00, 0x26 }, "28-6F-9C-7E-0E-00-00-26", "S02-MOSFET"},
  {{ 0x28, 0x04, 0x4E, 0x7E, 0x0E, 0x00, 0x00, 0xC0 }, "28-04-4E-7E-0E-00-00-C0", "S02-GateDriver"},
  {{ 0x28, 0xE7, 0xC8, 0x7E, 0x0E, 0x00, 0x00, 0xE7 }, "28-E7-C8-7E-0E-00-00-E7", "S03-MOSFET"},
  {{ 0x28, 0x37, 0x15, 0x7F, 0x0E, 0x00, 0x00, 0x1E }, "28-37-15-7F-0E-00-00-1E", "S03-GateDriver"},
  {{ 0x28, 0x4D, 0xD1, 0x7D, 0x0E, 0x00, 0x00, 0x73 }, "28-4D-D1-7D-0E-00-00-73", "S04-MOSFET"},
  {{ 0x28, 0x59, 0xC2, 0x7E, 0x0E, 0x00, 0x00, 0x4E }, "28-59-C2-7E-0E-00-00-4E", "S04-GateDriver"},
  {{ 0x28, 0x51, 0xBD, 0x7E, 0x0E, 0x00, 0x00, 0xED }, "28-51-BD-7E-0E-00-00-ED", "S05-MOSFET"},
  {{ 0x28, 0xC7, 0xEB, 0x7D, 0x0E, 0x00, 0x00, 0x6F }, "28-C7-EB-7D-0E-00-00-6F", "S05-GateDriver"}
  //{{  }, "", "S06-MOSFET"},
  //{{  }, "", "S06-GateDriver"}
};

int deviceCount = 0;
float tempC;

// PWM
using namespace eFlex;

static u_int32_t microMHz = F_CPU_ACTUAL / 1000000;  // Clock frequency in MHz

// sPWM
// DutyCycle range 0-65535 (0-100%)
constexpr uint16_t MinDutyCycle = 0; // min duty cycle value
constexpr uint16_t MidDutyCycle = 32768; // middle duty cycle value
constexpr uint16_t MaxDutyCycle = 65535; // max duty cycle value
volatile uint32_t vSample;
volatile float32_t vSpwmUpdateSpeed;

const char* prescaleStr[] = {
  "fclk/1", "fclk/2", "fclk/4", "fclk/8", "fclk/16", "fclk/32", "fclk/64", "fclk/128"
};

// eFlexPWM submodules
// (Hardware > PWM1: SM[3])
SubModule Sm13 (8,7);
// (Hardware > PWM2: SM[0], SM[1], SM[2], SM[3])
SubModule Sm20 (4,33);
SubModule Sm21 (5);
SubModule Sm22 (6,9);
SubModule Sm23 (36,37);
// (Hardware > PWM3: SM[1])
SubModule Sm31 (29,28);
// (Hardware > PWM4: SM[0], SM[1], SM[2])
SubModule Sm40 (22);
SubModule Sm41 (23);
SubModule Sm42 (2,3);

// All the sub-modules are part of the same timer
// Tm1 simplifies access to the functions that concern all the sub-modules for PWM1
eFlex::Timer &Tm1 = Sm13.timer();
// Tm2 simplifies access to the functions that concern all the sub-modules for PWM2
eFlex::Timer &Tm2 = Sm20.timer();
//Tm3 simplifies access to the functions that concern all the sub-modules for PWM3
eFlex::Timer &Tm3 = Sm31.timer();
// Tm4 simplifies access to the functions that concern all the sub-modules for PWM4
eFlex::Timer &Tm4 = Sm40.timer();

TeensyTimerTool::OneShotTimer startupTimer(TeensyTimerTool::TCK64);
TeensyTimerTool::OneShotTimer chargeToggleTimer(TeensyTimerTool::GPT1);
TeensyTimerTool::OneShotTimer dischargeToggleTimer(TeensyTimerTool::GPT2);
TeensyTimerTool::PeriodicTimer pwmSyncTimer(TeensyTimerTool::PIT);

// Teensy identifiers
uint8_t serial[4];
uint8_t uid64[8];

// Trigger pin to trigger oscilloscope
constexpr int TriggerPin = 13;

// Log
constexpr int16_t LogSize = 5;
String logs[] = {
  "Line 1",
  "Line 2",
  "Line 3",
  "Line 4",
  "Line 5"
};

// Method defs
void writeInfluxDb(String data);
void configureTimers();
void startupTimerCallback();
void chargeToggleTimerCallback();
void dischargeToggleTimerCallback();
void loadSettings();
void configureSdCard();
void configureEthernet();
void index(Request &req, Response &res);
void settings_pwm(Request &req, Response &res);
void settings_pwm_update(Request &req, Response &res);
void settings_pwm_timer(Request &req, Response &res);
void settings_pwm_timer_update(Request &req, Response &res);
void configureWebServer();
void DumpText(EthernetClient& client);
void processWebServer();
void configureNtp();
void configureSensors();
void printAddress(DeviceAddress deviceAddress);
void pollMetrics();
void lookupSensorAddresses();
void pollTemperature();
void pollVoltage();
void pollCurrent();
void pollFreeMemory();
void pollConfigSettings();
void configurePwm();
void configureModule1();
void configureModule2();
void configureModule3();
void configureModule4();
void attachInterruptVectors();
void attachModule2PwmInterruptVectors();
void enablePwmInterrupts();
void disablePwmInterrupts();
void enableModule2PwmInterrupts();
void disableModule2PwmInterrupts();
void IsrOverflowSm20();
void enableXbar();
void printStats();
void writeLog(const String &msg);
void printDigits(int digits);
void sendNTPpacket(const char *host);
void loadConfiguration(const char *filename, MainConfig &config);
void saveConfiguration(const char *filename, const MainConfig &config);
void printFile(const char *filename);
int getFreeMemory();
time_t getNtpTime();

static constexpr char PwmTimerSettingsPageTemplate[] = "<!DOCTYPE html>\n\
<html lang=\"en\">\n\
<head>\n\
<title>PWM Timer Settings</title>\n\
<style>\n\
body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\n\
.input { padding-top: 4px; padding-bottom: 4px; padding-left: 4px; padding-right: 4px; }\n\
.switch {\n\
  position: relative;\n\
  display: inline-block;\n\
  width: 60px;\n\
  height: 34px;\n\
}\n\
.switch input {\n\
  opacity: 0;\n\
  width: 0;\n\
  height: 0;\n\
}\n\
.slider {\n\
  position: absolute;\n\
  cursor: pointer;\n\
  top: 0;\n\
  left: 0;\n\
  right: 0;\n\
  bottom: 0;\n\
  background-color: #ccc;\n\
  -webkit-transition: .4s;\n\
  transition: .4s;\n\
}\n\
.slider:before {\n\
  position: absolute;\n\
  content: \"\";\n\
  height: 26px;\n\
  width: 26px;\n\
  left: 4px;\n\
  bottom: 4px;\n\
  background-color: white;\n\
  -webkit-transition: .4s;\n\
  transition: .4s;\n\
}\n\
input:checked + .slider {\n\
  background-color: #2196F3;\n\
}\n\
input:focus + .slider {\n\
  box-shadow: 0 0 1px #2196F3;\n\
}\n\
input:checked + .slider:before {\n\
  -webkit-transform: translateX(26px);\n\
  -ms-transform: translateX(26px);\n\
  transform: translateX(26px);\n\
}\n\
.slider.round {\n\
  border-radius: 34px;\n\
}\n\
.slider.round:before {\n\
  border-radius: 50%%;\n\
}\n\
</style>\n\
</head>\n\
<body onload=\"recalcValues()\">\n\
<form method=\"post\" enctype=\"application/x-www-form-urlencoded\" action=\"/settings/pwm-timer/update\" accept-charset=\"utf-8\">\n\
    <div>\n\
        <div>\n\
            <h1>\n\
                PWM Timer Settings\n\
            </h1>\n\
            <div>\n\
                <nav>\n\
                    <ol>\n\
                        <li>\n\
                            <a href=\"/\">Home</a>\n\
                        </li>\n\
                        <li>\n\
                            <a href=\"/settings\">Settings</a>\n\
                        </li>\n\
                        <li>\n\
                            PWM Timer\n\
                        </li>\n\
                    </ol>\n\
                </nav>\n\
            </div>\n\
            <hr>\n\
            <p>The periodic timer setting controls the period during which the PWM signal is output. The periods are repeated.</p>\n\
            <div class=\"input\">\n\
                <label>\n\
                    1.3A Period\n\
                </label>\n\
                <input type=\"number\" style=\"width: 75px;\" id=\"period-13a\" name=\"period-13a\" value=\"%lu\"> us\n\
                <label class=\"switch\">\n\
                  <input type=\"checkbox\" id=\"toggle-13a\" name=\"toggle-13a\" value=\"on\"%s>\n\
                  <span class=\"slider round\"></span>\n\
                </label>\n\
            </div>\n\
            <div class=\"input\">\n\
                <label>\n\
                    1.3B Period\n\
                </label>\n\
                <input type=\"number\" style=\"width: 75px;\" id=\"period-13b\" name=\"period-13b\" value=\"%lu\"> us\n\
                <label class=\"switch\">\n\
                  <input type=\"checkbox\" id=\"toggle-13b\" name=\"toggle-13b\" value=\"on\"%s>\n\
                  <span class=\"slider round\"></span>\n\
                </label>\n\
            </div>\n\
        </div>\n\
    </div>\n\
    <br>\n\
    <input type=\"submit\" value=\"Submit\">\n\
</form>\n\
</body>\n\
</html>";

static constexpr char PwmSettingsPageTemplate[] = "<!DOCTYPE html>\
<html lang=\"en\">\
<head>\
<title>PWM Settings</title>\
<style>\
body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
.input { padding-top: 4px; padding-bottom: 4px; padding-left: 4px; padding-right: 4px; }\
</style>\
</head>\
<body onload=\"recalcValues()\">\
<form method=\"post\" enctype=\"application/x-www-form-urlencoded\" action=\"/settings/pwm/update\" accept-charset=\"utf-8\">\
    <div>\
        <div>\
            <h1>\
                PWM Settings\
            </h1>\
            <div>\
                <nav>\
                    <ol>\
                        <li>\
                            <a href=\"/\">Home</a>\
                        </li>\
                        <li>\
                            <a href=\"/settings\">Settings</a>\
                        </li>\
                        <li>\
                            PWM\
                        </li>\
                    </ol>\
                </nav>\
            </div>\
            <hr>\
            <p>The periodic timer setting controls the period during which the PWM signal is output. The periods are repeated.</p>\
            <div class=\"input\">\
                <label>\
                    1.3A Period\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"period-13a\" name=\"period-13a\" value=\"%lu\"> us\
            </div>\
            <div class=\"input\">\
                <label>\
                    1.3B Period\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"period-13b\" name=\"period-13b\" value=\"%lu\"> us\
            </div>\
            <p>The PWM Frequency setting controls how many times per second the PWM signal is generated.</p>\
            <div class=\"input\">\
                <label>\
                    1.3 PWM Frequency\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"pwm-frequency-13\" name=\"pwm-frequency-13\" value=\"%lu\" oninput=\"calcPwmFrequency(this)\"> Hz\
            </div>\
            <div class=\"input\">\
                <label>\
                    2.0 PWM Frequency\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"pwm-frequency-20\" name=\"pwm-frequency-20\" value=\"%lu\" oninput=\"calcPwmFrequency(this)\"> Hz\
            </div>\
            <div class=\"input\">\
                <label>\
                    2.1 PWM Frequency\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"pwm-frequency-21\" name=\"pwm-frequency-21\" value=\"%lu\" oninput=\"calcPwmFrequency(this)\"> Hz\
            </div>\
            <div class=\"input\">\
                <label>\
                    2.2 PWM Frequency\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"pwm-frequency-22\" name=\"pwm-frequency-22\" value=\"%lu\" oninput=\"calcPwmFrequency(this)\"> Hz\
            </div>\
            <div class=\"input\">\
                <label>\
                    2.3 PWM Frequency\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"pwm-frequency-23\" name=\"pwm-frequency-23\" value=\"%lu\" oninput=\"calcPwmFrequency(this)\"> Hz\
            </div>\
            <div class=\"input\">\
                <label>\
                    3.1 PWM Frequency\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"pwm-frequency-31\" name=\"pwm-frequency-31\" value=\"%lu\" oninput=\"calcPwmFrequency(this)\"> Hz\
            </div>\
            <div class=\"input\">\
                <label>\
                    4.0 PWM Frequency\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"pwm-frequency-40\" name=\"pwm-frequency-40\" value=\"%lu\" oninput=\"calcPwmFrequency(this)\"> Hz\
            </div>\
            <div class=\"input\">\
                <label>\
                    4.1 PWM Frequency\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"pwm-frequency-41\" name=\"pwm-frequency-41\" value=\"%lu\" oninput=\"calcPwmFrequency(this)\"> Hz\
            </div>\
            <div class=\"input\">\
                <label>\
                    4.2 PWM Frequency\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"pwm-frequency-42\" name=\"pwm-frequency-42\" value=\"%lu\" oninput=\"calcPwmFrequency(this)\"> Hz\
            </div>\
            <p>This controls how much of a delay in nanoseconds to wait in-between the PWM waveforms.</p>\
            <div class=\"input\">\
                <label>\
                    1.3 Dead Time\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"dead-time-13\" name=\"dead-time-13\" value=\"%u\"> ns\
            </div>\
            <div class=\"input\">\
                <label>\
                    2.0 Dead Time\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"dead-time-20\" name=\"dead-time-20\" value=\"%u\"> ns\
            </div>\
            <div class=\"input\">\
                <label>\
                    2.1 Dead Time\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"dead-time-21\" name=\"dead-time-21\" value=\"%u\"> ns\
            </div>\
            <div class=\"input\">\
                <label>\
                    2.2 Dead Time\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"dead-time-22\" name=\"dead-time-22\" value=\"%u\"> ns\
            </div>\
            <div class=\"input\">\
                <label>\
                    2.3 Dead Time\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"dead-time-23\" name=\"dead-time-23\" value=\"%u\"> ns\
            </div>\
            <div class=\"input\">\
                <label>\
                    3.1 Dead Time\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"dead-time-31\" name=\"dead-time-31\" value=\"%u\"> ns\
            </div>\
            <div class=\"input\">\
                <label>\
                    4.0 Dead Time\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"dead-time-40\" name=\"dead-time-40\" value=\"%u\"> ns\
            </div>\
            <div class=\"input\">\
                <label>\
                    4.1 Dead Time\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"dead-time-41\" name=\"dead-time-41\" value=\"%u\"> ns\
            </div>\
            <div class=\"input\">\
                <label>\
                    4.2 Dead Time\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"dead-time-42\" name=\"dead-time-42\" value=\"%u\"> ns\
            </div>\
            <p>The Duty Cycle setting controls how long each waveform is switched on for as a percentage of a single PWM time period. Duty Cycle range is 0-65535 (0-100%%).</p>\
            <div class=\"input\">\
                <label>\
                    1.3A Duty Cycle\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"duty-cycle-13a\" name=\"duty-cycle-13a\" value=\"%u\" oninput=\"calcDutyCycle(this)\">\
                <text class=\"input\" id=\"duty-cycle-13a-percent\" name=\"duty-cycle-13a-percent\"></text>\
                <text class=\"input\" id=\"duty-cycle-13a-period\" name=\"duty-cycle-13a-period\"></text>\
                <text class=\"input\" id=\"duty-cycle-13a-pulse-width\" name=\"duty-cycle-13a-pulse-width\"></text>\
            </div>\
            <div class=\"input\">\
                <label>\
                    1.3B Duty Cycle\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"duty-cycle-13b\" name=\"duty-cycle-13b\" value=\"%u\" oninput=\"calcDutyCycle(this)\">\
                <text class=\"input\" id=\"duty-cycle-13b-percent\" name=\"duty-cycle-13b-percent\"></text>\
                <text class=\"input\" id=\"duty-cycle-13b-period\" name=\"duty-cycle-13b-period\"></text>\
                <text class=\"input\" id=\"duty-cycle-13b-pulse-width\" name=\"duty-cycle-13b-pulse-width\"></text>\
            </div>\
            <div class=\"input\">\
                <label>\
                    2.0A Duty Cycle\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"duty-cycle-20a\" name=\"duty-cycle-20a\" value=\"%u\" oninput=\"calcDutyCycle(this)\">\
                <text class=\"input\" id=\"duty-cycle-20a-percent\" name=\"duty-cycle-20a-percent\"></text>\
                <text class=\"input\" id=\"duty-cycle-20a-period\" name=\"duty-cycle-20a-period\"></text>\
                <text class=\"input\" id=\"duty-cycle-20a-pulse-width\" name=\"duty-cycle-20a-pulse-width\"></text>\
            </div>\
            <div class=\"input\">\
                <label>\
                    2.0B Duty Cycle\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"duty-cycle-20b\" name=\"duty-cycle-20b\" value=\"%u\" oninput=\"calcDutyCycle(this)\">\
                <text class=\"input\" id=\"duty-cycle-20b-percent\" name=\"duty-cycle-20b-percent\"></text>\
                <text class=\"input\" id=\"duty-cycle-20b-period\" name=\"duty-cycle-20b-period\"></text>\
                <text class=\"input\" id=\"duty-cycle-20b-pulse-width\" name=\"duty-cycle-20b-pulse-width\"></text>\
            </div>\
            <div class=\"input\">\
                <label>\
                    2.1A Duty Cycle\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"duty-cycle-21a\" name=\"duty-cycle-21a\" value=\"%u\" oninput=\"calcDutyCycle(this)\">\
                <text class=\"input\" id=\"duty-cycle-21a-percent\" name=\"duty-cycle-21a-percent\"></text>\
                <text class=\"input\" id=\"duty-cycle-21a-period\" name=\"duty-cycle-21a-period\"></text>\
                <text class=\"input\" id=\"duty-cycle-21a-pulse-width\" name=\"duty-cycle-21a-pulse-width\"></text>\
            </div>\
            <div class=\"input\">\
                <label>\
                    2.2A Duty Cycle\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"duty-cycle-22a\" name=\"duty-cycle-22a\" value=\"%u\" oninput=\"calcDutyCycle(this)\">\
                <text class=\"input\" id=\"duty-cycle-22a-percent\" name=\"duty-cycle-22a-percent\"></text>\
                <text class=\"input\" id=\"duty-cycle-22a-period\" name=\"duty-cycle-22a-period\"></text>\
                <text class=\"input\" id=\"duty-cycle-22a-pulse-width\" name=\"duty-cycle-22a-pulse-width\"></text>\
            </div>\
            <div class=\"input\">\
                <label>\
                    2.2B Duty Cycle\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"duty-cycle-22b\" name=\"duty-cycle-22b\" value=\"%u\" oninput=\"calcDutyCycle(this)\">\
                <text class=\"input\" id=\"duty-cycle-22b-percent\" name=\"duty-cycle-22b-percent\"></text>\
                <text class=\"input\" id=\"duty-cycle-22b-period\" name=\"duty-cycle-22b-period\"></text>\
                <text class=\"input\" id=\"duty-cycle-22b-pulse-width\" name=\"duty-cycle-22b-pulse-width\"></text>\
            </div>\
            <div class=\"input\">\
                <label>\
                    2.3A Duty Cycle\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"duty-cycle-23a\" name=\"duty-cycle-23a\" value=\"%u\" oninput=\"calcDutyCycle(this)\">\
                <text class=\"input\" id=\"duty-cycle-23a-percent\" name=\"duty-cycle-23a-percent\"></text>\
                <text class=\"input\" id=\"duty-cycle-23a-period\" name=\"duty-cycle-23a-period\"></text>\
                <text class=\"input\" id=\"duty-cycle-23a-pulse-width\" name=\"duty-cycle-23a-pulse-width\"></text>\
            </div>\
            <div class=\"input\">\
                <label>\
                    2.3B Duty Cycle\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"duty-cycle-23b\" name=\"duty-cycle-23b\" value=\"%u\" oninput=\"calcDutyCycle(this)\">\
                <text class=\"input\" id=\"duty-cycle-23b-percent\" name=\"duty-cycle-23b-percent\"></text>\
                <text class=\"input\" id=\"duty-cycle-23b-period\" name=\"duty-cycle-23b-period\"></text>\
                <text class=\"input\" id=\"duty-cycle-23b-pulse-width\" name=\"duty-cycle-23b-pulse-width\"></text>\
            </div>\
            <div class=\"input\">\
                <label>\
                    3.1A Duty Cycle\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"duty-cycle-31a\" name=\"duty-cycle-31a\" value=\"%u\" oninput=\"calcDutyCycle(this)\">\
                <text class=\"input\" id=\"duty-cycle-31a-percent\" name=\"duty-cycle-31a-percent\"></text>\
                <text class=\"input\" id=\"duty-cycle-31a-period\" name=\"duty-cycle-31a-period\"></text>\
                <text class=\"input\" id=\"duty-cycle-31a-pulse-width\" name=\"duty-cycle-31a-pulse-width\"></text>\
            </div>\
            <div class=\"input\">\
                <label>\
                    3.1B Duty Cycle\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"duty-cycle-31b\" name=\"duty-cycle-31b\" value=\"%u\" oninput=\"calcDutyCycle(this)\">\
                <text class=\"input\" id=\"duty-cycle-31b-percent\" name=\"duty-cycle-31b-percent\"></text>\
                <text class=\"input\" id=\"duty-cycle-31b-period\" name=\"duty-cycle-31b-period\"></text>\
                <text class=\"input\" id=\"duty-cycle-31b-pulse-width\" name=\"duty-cycle-31b-pulse-width\"></text>\
            </div>\
            <div class=\"input\">\
                <label>\
                    4.0A Duty Cycle\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"duty-cycle-40a\" name=\"duty-cycle-40a\" value=\"%u\" oninput=\"calcDutyCycle(this)\">\
                <text class=\"input\" id=\"duty-cycle-40a-percent\" name=\"duty-cycle-40a-percent\"></text>\
                <text class=\"input\" id=\"duty-cycle-40a-period\" name=\"duty-cycle-40a-period\"></text>\
                <text class=\"input\" id=\"duty-cycle-40a-pulse-width\" name=\"duty-cycle-40a-pulse-width\"></text>\
            </div>\
            <div class=\"input\">\
                <label>\
                    4.1A Duty Cycle\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"duty-cycle-41a\" name=\"duty-cycle-41a\" value=\"%u\" oninput=\"calcDutyCycle(this)\">\
                <text class=\"input\" id=\"duty-cycle-41a-percent\" name=\"duty-cycle-41a-percent\"></text>\
                <text class=\"input\" id=\"duty-cycle-41a-period\" name=\"duty-cycle-41a-period\"></text>\
                <text class=\"input\" id=\"duty-cycle-41a-pulse-width\" name=\"duty-cycle-41a-pulse-width\"></text>\
            </div>\
            <div class=\"input\">\
                <label>\
                    4.2A Duty Cycle\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"duty-cycle-42a\" name=\"duty-cycle-42a\" value=\"%u\" oninput=\"calcDutyCycle(this)\">\
                <text class=\"input\" id=\"duty-cycle-42a-percent\" name=\"duty-cycle-42a-percent\"></text>\
                <text class=\"input\" id=\"duty-cycle-42a-period\" name=\"duty-cycle-42a-period\"></text>\
                <text class=\"input\" id=\"duty-cycle-42a-pulse-width\" name=\"duty-cycle-42a-pulse-width\"></text>\
            </div>\
            <div class=\"input\">\
                <label>\
                    4.2B Duty Cycle\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"duty-cycle-42b\" name=\"duty-cycle-42b\" value=\"%u\" oninput=\"calcDutyCycle(this)\">\
                <text class=\"input\" id=\"duty-cycle-42b-percent\" name=\"duty-cycle-42b-percent\"></text>\
                <text class=\"input\" id=\"duty-cycle-42b-period\" name=\"duty-cycle-42b-period\"></text>\
                <text class=\"input\" id=\"duty-cycle-42b-pulse-width\" name=\"duty-cycle-42b-pulse-width\"></text>\
            </div>\
            <p>The phase shift setting controls the phase shift between 2.0-2.1, 2.0-2.2 & 2.0-2.3.</p>\
            <div class=\"input\">\
                <label>\
                    2.1A Phase Shift\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"phase-shift-21a\" name=\"phase-shift-21a\" value=\"%u\">\
            </div>\
            <div class=\"input\">\
                <label>\
                    2.2A Phase Shift\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"phase-shift-22a\" name=\"phase-shift-22a\" value=\"%u\">\
            </div>\
            <div class=\"input\">\
                <label>\
                    2.2B Phase Shift\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"phase-shift-22b\" name=\"phase-shift-22b\" value=\"%u\">\
            </div>\
            <div class=\"input\">\
                <label>\
                    2.3A Phase Shift\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"phase-shift-23a\" name=\"phase-shift-23a\" value=\"%u\">\
            </div>\
            <div class=\"input\">\
                <label>\
                    2.3B Phase Shift\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"phase-shift-23b\" name=\"phase-shift-23b\" value=\"%u\">\
            </div>\
            <div class=\"input\">\
                <label>\
                    4.1A Phase Shift\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"phase-shift-41a\" name=\"phase-shift-41a\" value=\"%u\">\
            </div>\
            <div class=\"input\">\
                <label>\
                    4.2A Phase Shift\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"phase-shift-42a\" name=\"phase-shift-42a\" value=\"%u\">\
            </div>\
            <div class=\"input\">\
                <label>\
                    4.2B Phase Shift\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"phase-shift-42b\" name=\"phase-shift-42b\" value=\"%u\">\
            </div>\
            <div class=\"input\">\
                <label>\
                    Print Register Values?\
                </label>\
                <input type=\"text\" style=\"width: 75px;\" id=\"print-regs\" name=\"print-regs\" value=\"%s\"> Yes / No\
            </div>\
            <div class=\"input\">\
                <label>\
                    Synchronise PWM With PIT0 Timer?\
                </label>\
                <input type=\"text\" style=\"width: 75px;\" id=\"sync-pwm\" name=\"sync-pwm\" value=\"%s\"> Yes / No\
            </div>\
            <p>This is the configuration for the inverter PWM.</p>\
            <p>This is a full bridge SPWM using PWM outputs 2.0A, 2.0B, 2.2A &amp; 2.2B</p>\
            <p>The carrier signal of SPWM is usually a triangular wave with a high frequency, generally in several KHz. The modulation signal of SPWM is a sinusoidal waveform with a frequency equal to the desired output voltage frequency (50 or 60 Hz).</p>\
            <p>2.0A controls the positive half cycle of the output inverter and should be used by switches 1 & 2.</p>\
            <p>2.2A controls the negative half cycle of the output inverter and should be used by switches 3 & 4.</p>\
            <p>The SPWM Carrier Signal Frequency setting controls how many times per second the SPWM carrier signal is generated.</p>\
            <div class=\"input\">\
                <label>\
                    Use SPWM\
                </label>\
                <input type=\"text\" style=\"width: 75px;\" id=\"use-spwm\" name=\"use-spwm\" value=\"%s\"> Yes / No\
            </div>\
            <div class=\"input\">\
                <label>\
                    SPWM Carrier Signal Frequency\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"spwm-carrier-signal-frequency\" name=\"spwm-carrier-signal-frequency\" value=\"%lu\"> Hz\
            </div>\
            <p>The SPWM Modulation Frequency setting controls how many times per second the modulation signal is generated. Typical values are 50Hz (Europe) or 60Hz (North America).</p>\
            <div class=\"input\">\
                <label>\
                    SPWM Modulation Frequency\
                </label>\
                <input type=\"number\" style=\"width: 75px;\" id=\"spwm-modulation-frequency\" name=\"spwm-modulation-frequency\" value=\"%lu\"> Hz\
            </div>\
        </div>\
    </div>\
    <br>\
    <input type=\"submit\" value=\"Submit\">\
</form>\
<script>\
    function calcDutyCycle(element) {\
      let dutyCycle = element.value;\
      dutyCycle = ((dutyCycle / 65535.0)*100.0);\
      document.getElementById(element.id + \"-percent\").innerHTML = \"Percent: \" + dutyCycle.toFixed(4) + \"%%\";\
\
      let pwmFrequencyElementId = element.id.replace(\"duty-cycle\", \"pwm-frequency\").replace(/a|b$/gm,'');\
\
      let pwmVal = document.getElementById(pwmFrequencyElementId).value;\
      let period = (1 / pwmVal);\
      let periodMs = (period * 1000.0).toFixed(4);\
      let periodUs = (period * 1000000.0).toFixed(4);\
      let periodNs = (period * 1000000000.0).toFixed(0);\
      let pulseWidth = ((dutyCycle / 100.0) * period);\
      let pulseWidthMs = (pulseWidth * 1000.0).toFixed(4);\
      let pulseWidthUs = (pulseWidth * 1000000.0).toFixed(4);\
      let pulseWidthNs = (pulseWidth * 1000000000.0).toFixed(0);\
\
      document.getElementById(element.id + \"-period\").innerHTML = \"Period: \" + period.toFixed(4) + \"s \" + periodMs + \"ms \" + periodUs + \"us \" + periodNs + \"ns\";\
      document.getElementById(element.id + \"-pulse-width\").innerHTML = \"Pulse Width: \" + pulseWidth.toFixed(4) + \"s \" + pulseWidthMs + \"ms \" + pulseWidthUs + \"us \" + pulseWidthNs + \"ns\";\
    }\
\
    function processDutyCycleElement(channel) {\
      let dutyCycleElement = document.getElementById(\"duty-cycle-\" + channel);\
\
      if (typeof(dutyCycleElement) != 'undefined' && dutyCycleElement != null) {\
          calcDutyCycle(dutyCycleElement);\
      }\
    }\
\
    function calcPwmFrequency(element) {\
      let pwmVal = element.value;\
      let moduleNum = element.id.replace(\"pwm-frequency-\", \"\");\
\
      const channels = [moduleNum + \"a\", moduleNum + \"b\"];\
\
      channels.forEach(processDutyCycleElement);\
    }\
\
    function recalcPwm(module) {\
      let pwmElement = document.getElementById(\"pwm-frequency-\" + module);\
\
      if (typeof(pwmElement) != 'undefined' && pwmElement != null) {\
          calcPwmFrequency(pwmElement);\
      }\
    }\
\
    function recalcValues() {\
        const modules = [13, 20, 21, 22, 23, 31, 40, 41, 42];\
\
        modules.forEach(recalcPwm);\
    }\
</script>\
</body>\
</html>";

// InfluxDB
char influxDbServerAddress[] = "ub-1.lan";
int influxDbPort = 8086;
EthernetClient      influxDbClient;
//EthernetHttpClient  httpClient(influxDbClient, influxDbServerAddress, influxDbPort);

void writeInfluxDb(String data) {
  //Serial.println("Sending POST request to Influx DB");
  /*
  HttpClient http(influxDbClient);

  httpClient.beginRequest();
  httpClient.post(F("/api/v2/write?org=501eaf58ac3171cd&bucket=power_generator&precision=ms"));
  httpClient.sendHeader(F("Authorization"), F("Bearer oSe4_XGLyob-Ns0FT56CouDXw3jEpocQ0ntSuX7q0vr6JOn82GapRz0yUfrnpobYPxTTwS_EV2nyJ6vMCvGTcA=="));
  httpClient.sendHeader(F("Content-Type"), F("text/plain"));
  httpClient.sendHeader(F("Content-Length"), data.length());
  httpClient.beginBody();
  httpClient.print(data);
  httpClient.endRequest();

  // read the status code and body of the response
  int statusCode = httpClient.responseStatusCode();
  String response = httpClient.responseBody();

  if(statusCode >= 400) {
    Serial.print(F("Status code: "));
    Serial.println(statusCode);
    Serial.print(F("Response: "));
    Serial.println(response);
  }
  */
}

// QNEthernet links this variable with lwIP's `printf` calls for
// assertions and debugging. User code can also use `printf`.
Print *stdPrint;

void setup() {
  Serial.begin(9600);

  if ( Serial && CrashReport ) {
    Serial.println("\n" __FILE__ " " __DATE__ " " __TIME__);
    Serial.print(CrashReport);
  }

  stdPrint = &Serial;  // Make printf work

  // Ensure pins inactive
  // PWM 1.3
  pinMode (8, INPUT); // ChanA
  pinMode (7, INPUT); // ChanB
  // PWM 2.0
  pinMode ( 4, INPUT); // ChanA
  pinMode (33, INPUT); // ChanB
  // PWM 2.1
  pinMode ( 5, INPUT); // ChanA
  // PWM 2.2
  pinMode ( 6, INPUT); // ChanA
  pinMode ( 9, INPUT); // ChanB
  // PWM 2.3
  pinMode (36, INPUT); // ChanA
  pinMode (37, INPUT); // ChanB
  // PWM 3.1
  pinMode (29, INPUT); // ChanA
  pinMode (28, INPUT); // ChanB
  // PWM 4.0
  pinMode (22, INPUT); // ChanA
  // PWM 4.1
  pinMode (23, INPUT); // ChanA
  // PWM 4.2
  pinMode ( 2, INPUT); // ChanA
  pinMode ( 3, INPUT); // ChanB

  // Initialize LED digital pin as an output
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(TriggerPin, OUTPUT);

  digitalWriteFast(LED_BUILTIN, HIGH);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ; // Don't proceed, loop forever
  }

  display.display();

  teensySN(serial);
  teensyUID64(uid64);

  delay(2000);  // Pause for 2 seconds

  Serial.printf("USB Serial: %u \n", teensyUsbSN());
  Serial.printf("Teensy Serial: %02X-%02X-%02X-%02X \n", serial[0], serial[1], serial[2], serial[3]);
  //Serial.printf("MAC Address: %02X:%02X:%02X:%02X:%02X:%02X \n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.printf("UID 64-bit: %02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n", uid64[0], uid64[1], uid64[2], uid64[3], uid64[4], uid64[5], uid64[6], uid64[7]);

  configureSdCard();

  loadSettings();

  // Clear the buffer
  display.clearDisplay();
  //display.setFont(&FreeSans9pt7b);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);

  enableXbar();
  configurePwm();
  configureSensors();
  configureEthernet();
  configureTimers();

  //Serial.println(ETHERNET_WEBSERVER_VERSION);

  //configureNtp();
  //startWebServer();

  writeLog ("Init complete");

  printStats();

  writeLog (F("Ready"));

  digitalWriteFast(LED_BUILTIN, LOW);
}

unsigned timerStart;

/*
Max timer period for various timers

TCK:               5.00 seconds
TCK64:           634.20 years
TCK_RTC:         634.20 years
GPT(@24MHz)      178.96 seconds
TMR(PSC_AUTO)     55.92 milliseconds
PIT(@24MHz)      178.96 seconds
*/

void configureTimers() {
  writeLog (F("Configuring timers"));
  timerStart = millis();

  chargeToggleTimer.begin(chargeToggleTimerCallback);
  dischargeToggleTimer.begin(dischargeToggleTimerCallback);
  startupTimer.begin(startupTimerCallback);

  startupTimer.trigger(5s);
}

void configurePwmSyncTimer() {
  writeLog (F("Stopping PWM sync timer"));
  pwmSyncTimer.stop();

  if(config.Pwm.SyncPwm) {
    writeLog (F("Starting PWM sync timer"));
    pwmSyncTimer.begin([] { }, 1us, true); // 1MHz timer used to synchronise PWM modules
  }
}

void startupTimerCallback()
{
  digitalWriteFast(LED_BUILTIN, HIGH);
  Serial.printf("startupTimerCallback invoked at: %u ms\n", millis() - timerStart);
  chargeToggleTimer.trigger(1ms);
  digitalWriteFast(LED_BUILTIN, LOW);
}

void chargeToggleTimerCallback()
{
  digitalWriteFast(LED_BUILTIN, HIGH);

  if(config.Pwm.Tm1.Sm13.ChannelA.Enabled) {
    Sm13.setPwmForceOutputToZero(ChanB, true);
    Sm13.setPwmForceOutputToZero(ChanA, false);
  }
  else {
    Sm13.setPwmForceOutputToZero(ChanB, true);
    Sm13.setPwmForceOutputToZero(ChanA, true);
  }

  dischargeToggleTimer.trigger(std::chrono::microseconds(config.Pwm.Tm1.Sm13.ChannelA.OnPeriodMicroseconds));
  digitalWriteFast(LED_BUILTIN, LOW);
}

void dischargeToggleTimerCallback()
{
  digitalWriteFast(LED_BUILTIN, HIGH);

  if(config.Pwm.Tm1.Sm13.ChannelB.Enabled) {
    Sm13.setPwmForceOutputToZero(ChanA, true);
    Sm13.setPwmForceOutputToZero(ChanB, false);
  }
  else {
    Sm13.setPwmForceOutputToZero(ChanB, true);
    Sm13.setPwmForceOutputToZero(ChanA, true);
  }

  chargeToggleTimer.trigger(std::chrono::microseconds(config.Pwm.Tm1.Sm13.ChannelB.OnPeriodMicroseconds));
  digitalWriteFast(LED_BUILTIN, LOW);
}

void loadSettings() {
  // Should load default config if run for the first time
  Serial.println(F("Loading configuration..."));
  loadConfiguration(filename, config);

  // Create configuration file
  Serial.println(F("Saving configuration..."));
  saveConfiguration(filename, config);

  // Dump config file
  Serial.println(F("Print config file..."));
  printFile(filename);
}

#define BUILTIN_SDCARD 254
#define SD_CONFIG SdioConfig(FIFO_SDIO)
SdFs sd;
FsFile file;

void configureSdCard() {
  if (!sd.begin(SD_CONFIG)) {
    Serial.println("SD initialization failed.");
    sd.initErrorHalt(&Serial);
  }

  Serial.println("Files on card:");
  Serial.println("   Size    Name");

  sd.ls(LS_R | LS_SIZE);
}

void configureEthernet() {
bool hasStarted = false;

#if USE_NATIVE_ETHERNET
  Serial.println(F("Using NATIVE_ETHERNET"));
  Ethernet.init();
  hasStarted = Ethernet.begin(mac);
#elif USE_QN_ETHERNET
  Serial.println(F("Using QN_ETHERNET"));

  uint8_t mac[6];
  Ethernet.macAddress(mac);
  printf("[Main] MAC = %02x:%02x:%02x:%02x:%02x:%02x\r\n",
         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  Ethernet.onLinkState([](bool state) {
    if (state) {
      printf("[Ethernet] Link: ON, %d Mbps, %s duplex, %s crossover\r\n",
             Ethernet.linkSpeed(),
             Ethernet.linkIsFullDuplex() ? "full" : "half",
             Ethernet.linkIsCrossover() ? "is" : "not");
    } else {
      printf("[Ethernet] Link: OFF\r\n");
    }
  });

  Ethernet.onAddressChanged([]() {
    IPAddress ip = Ethernet.localIP();
    const bool hasIP = (ip != INADDR_NONE);
    if (hasIP) {
      IPAddress subnet = Ethernet.subnetMask();
      IPAddress gw = Ethernet.gatewayIP();
      IPAddress dns = Ethernet.dnsServerIP();
      printf("[Ethernet] Address changed:\r\n"
             "    Local IP = %" PRIu8 ".%" PRIu8 ".%" PRIu8 ".%" PRIu8 "\r\n"
             "    Subnet   = %" PRIu8 ".%" PRIu8 ".%" PRIu8 ".%" PRIu8 "\r\n"
             "    Gateway  = %" PRIu8 ".%" PRIu8 ".%" PRIu8 ".%" PRIu8 "\r\n"
             "    DNS      = %" PRIu8 ".%" PRIu8 ".%" PRIu8 ".%" PRIu8 "\r\n",
             ip[0], ip[1], ip[2], ip[3],
             subnet[0], subnet[1], subnet[2], subnet[3],
             gw[0], gw[1], gw[2], gw[3],
             dns[0], dns[1], dns[2], dns[3]);
    } else {
      printf("[Ethernet] Address changed: No IP address\r\n");
    }
  });

  hasStarted = Ethernet.begin();
#else
  Serial.println(F("Unable to determine Ethernet library used"));
#endif

  if (!hasStarted) {
    Serial.println(F("Failed to start Ethernet"));
    return;
  }

  if (Ethernet.hardwareStatus() == EthernetNoHardware)
  {
    Serial.println(F("No Ethernet found. Stay here forever"));

    while (true)
    {
      delay(1); // do nothing, no point running without Ethernet hardware
    }
  }

  if (Ethernet.linkStatus() == LinkOFF)
  {
    Serial.println(F("Not connected Ethernet cable"));
  }

  delay(2000);

  if (!Ethernet.waitForLocalIP(kDHCPTimeout)) {
    printf("[Main] ERROR: Failed to get DHCP address within %" PRIu32 "ms\r\n",
           kDHCPTimeout);
  }

  Serial.print(F("Connected! IP address: "));
  Serial.println(Ethernet.localIP());

  configureNtp();
  configureWebServer();
}

// Web server
void index(Request &req, Response &res) {
  char temp[BUFFER_SIZE];
  const uint32_t sec = millis() / 1000;
  const uint32_t min = sec / 60;
  uint32_t hr = min / 60;
  const uint32_t dy = hr / 24;

  hr = hr % 24;

  char timeText[20];
  snprintf(timeText,sizeof(timeText),"%d-%02d-%02d %02d:%02d:%02d", year(), month(), day(), hour(), minute(), second());

  Serial.println(timeText);

  snprintf(temp, BUFFER_SIZE - 1,
           "<html>\
<head>\
<title>TPG: Transient Power Generator</title>\
<style>\
body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
</style>\
</head>\
<body>\
<h1>TPG: Transient Power Generator</h1>\
<h2>Settings</h2>\
<ul>\
<li><a href='/settings/pwm'>PWM</a></li>\
<li><a href='/settings/pwm-timer'>PWM Timer</a></li>\
</ul>\
<p>Uptime: %lu d %02lu:%02lu:%02lu</p>\
<p>Current time: %s UTC</p>\
</body>\
</html>", dy, hr, min % 60, sec % 60, timeText);

  res.set("Content-Type", "text/html");
  res.printP(temp);
  res.flush();
  res.end();
}

/*
Format specifiers:
https://utat-ss.readthedocs.io/en/master/c-programming/print-formatting.html

uint8_t, uint16_t: %u (Unsigned)
uint32_t: %lu (Long Unsigned)
int8_t, int16_t: %d (Decimal - Signed)
int32_t: %ld (Long Decimal - Signed)
float, double: %f (Float)
uint8_t, uint16_t, int8_t, int16_t: %x (Hexadecimal - Lowercase), %X (Hexadecimal - Uppercase)
uint32_t, int32_t: %lx (Long Hexadecimal - Lowercase), %lX (Long Hexadecimal - Uppercase)
*/

void settings_pwm(Request &req, Response &res) {
  char temp[BUFFER_SIZE];

  snprintf(temp, BUFFER_SIZE - 1,
  PwmSettingsPageTemplate,
  config.Pwm.Tm1.Sm13.ChannelA.OnPeriodMicroseconds,
  config.Pwm.Tm1.Sm13.ChannelB.OnPeriodMicroseconds,
  config.Pwm.Tm1.Sm13.PwmFrequency,
  config.Pwm.Tm2.Sm20.PwmFrequency,
  config.Pwm.Tm2.Sm21.PwmFrequency,
  config.Pwm.Tm2.Sm22.PwmFrequency,
  config.Pwm.Tm2.Sm23.PwmFrequency,
  config.Pwm.Tm3.Sm31.PwmFrequency,
  config.Pwm.Tm4.Sm40.PwmFrequency,
  config.Pwm.Tm4.Sm41.PwmFrequency,
  config.Pwm.Tm4.Sm42.PwmFrequency,
  config.Pwm.Tm1.Sm13.DeadTime,
  config.Pwm.Tm2.Sm20.DeadTime,
  config.Pwm.Tm2.Sm21.DeadTime,
  config.Pwm.Tm2.Sm22.DeadTime,
  config.Pwm.Tm2.Sm23.DeadTime,
  config.Pwm.Tm3.Sm31.DeadTime,
  config.Pwm.Tm4.Sm40.DeadTime,
  config.Pwm.Tm4.Sm41.DeadTime,
  config.Pwm.Tm4.Sm42.DeadTime,
  config.Pwm.Tm1.Sm13.ChannelA.DutyCycle,
  config.Pwm.Tm1.Sm13.ChannelB.DutyCycle,
  config.Pwm.Tm2.Sm20.ChannelA.DutyCycle,
  config.Pwm.Tm2.Sm20.ChannelB.DutyCycle,
  config.Pwm.Tm2.Sm21.ChannelA.DutyCycle,
  config.Pwm.Tm2.Sm22.ChannelA.DutyCycle,
  config.Pwm.Tm2.Sm22.ChannelB.DutyCycle,
  config.Pwm.Tm2.Sm23.ChannelA.DutyCycle,
  config.Pwm.Tm2.Sm23.ChannelB.DutyCycle,
  config.Pwm.Tm3.Sm31.ChannelA.DutyCycle,
  config.Pwm.Tm3.Sm31.ChannelB.DutyCycle,
  config.Pwm.Tm4.Sm40.ChannelA.DutyCycle,
  config.Pwm.Tm4.Sm41.ChannelA.DutyCycle,
  config.Pwm.Tm4.Sm42.ChannelA.DutyCycle,
  config.Pwm.Tm4.Sm42.ChannelB.DutyCycle,
  config.Pwm.Tm2.Sm21.ChannelA.PhaseShift,
  config.Pwm.Tm2.Sm22.ChannelA.PhaseShift,
  config.Pwm.Tm2.Sm22.ChannelB.PhaseShift,
  config.Pwm.Tm2.Sm23.ChannelA.PhaseShift,
  config.Pwm.Tm2.Sm23.ChannelB.PhaseShift,
  config.Pwm.Tm4.Sm41.ChannelA.PhaseShift,
  config.Pwm.Tm4.Sm42.ChannelA.PhaseShift,
  config.Pwm.Tm4.Sm42.ChannelB.PhaseShift,
  config.Pwm.PrintRegs ? "Yes" : "No",
  config.Pwm.SyncPwm ? "Yes" : "No",
  config.Pwm.Tm2.UseSpwm ? "Yes" : "No",
  config.Pwm.Tm2.SpwmCarrierFrequency,
  config.Pwm.Tm2.SpwmModulationFrequency);

  res.set("Content-Type", "text/html");
  res.printP(temp);
  res.flush();
  res.end();
}

void settings_pwm_update(Request &req, Response &res) {
  if (req.method() != Request::MethodType::POST)
  {
    digitalWriteFast (LED_BUILTIN, HIGH);
    res.set("Content-Type", "text/plain");
    res.printP("Method Not Allowed");
    res.sendStatus(405);
    res.flush();
    res.end();
    digitalWriteFast (LED_BUILTIN, LOW);
  }
  else
  {
    digitalWriteFast (LED_BUILTIN, HIGH);

    disablePwmInterrupts();

    while (req.left()) {
      char value[100];
      char name[50];
      if(!req.form(name, 50, value, 100)) {
        res.sendStatus(400);
        res.flush();
        res.end();
      }

      res.print(name);
      res.print(": ");
      res.println(value);

      if(strcmp( name, "period-13a") == 0) {
        config.Pwm.Tm1.Sm13.ChannelA.OnPeriodMicroseconds = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "period-13b") == 0) {
        config.Pwm.Tm1.Sm13.ChannelB.OnPeriodMicroseconds = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "pwm-frequency-13") == 0) {
        config.Pwm.Tm1.Sm13.PwmFrequency = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "pwm-frequency-20") == 0) {
        config.Pwm.Tm2.Sm20.PwmFrequency = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "pwm-frequency-21") == 0) {
        config.Pwm.Tm2.Sm21.PwmFrequency = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "pwm-frequency-22") == 0) {
        config.Pwm.Tm2.Sm22.PwmFrequency = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "pwm-frequency-23") == 0) {
        config.Pwm.Tm2.Sm23.PwmFrequency = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "pwm-frequency-31") == 0) {
        config.Pwm.Tm3.Sm31.PwmFrequency = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "pwm-frequency-40") == 0) {
        config.Pwm.Tm4.Sm40.PwmFrequency = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "pwm-frequency-41") == 0) {
        config.Pwm.Tm4.Sm41.PwmFrequency = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "pwm-frequency-42") == 0) {
        config.Pwm.Tm4.Sm42.PwmFrequency = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "dead-time-13") == 0) {
        config.Pwm.Tm1.Sm13.DeadTime = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "dead-time-20") == 0) {
        config.Pwm.Tm2.Sm20.DeadTime = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "dead-time-21") == 0) {
        config.Pwm.Tm2.Sm21.DeadTime = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "dead-time-22") == 0) {
        config.Pwm.Tm2.Sm22.DeadTime = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "dead-time-23") == 0) {
        config.Pwm.Tm2.Sm23.DeadTime = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "dead-time-31") == 0) {
        config.Pwm.Tm3.Sm31.DeadTime = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "dead-time-40") == 0) {
        config.Pwm.Tm4.Sm40.DeadTime = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "dead-time-41") == 0) {
        config.Pwm.Tm4.Sm41.DeadTime = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "dead-time-42") == 0) {
        config.Pwm.Tm4.Sm42.DeadTime = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "duty-cycle-13a") == 0) {
        config.Pwm.Tm1.Sm13.ChannelA.DutyCycle = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "duty-cycle-13b") == 0) {
        config.Pwm.Tm1.Sm13.ChannelB.DutyCycle = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "duty-cycle-20a") == 0) {
        config.Pwm.Tm2.Sm20.ChannelA.DutyCycle = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "duty-cycle-20b") == 0) {
        config.Pwm.Tm2.Sm20.ChannelB.DutyCycle = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "duty-cycle-21a") == 0) {
        config.Pwm.Tm2.Sm21.ChannelA.DutyCycle = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "duty-cycle-22a") == 0) {
        config.Pwm.Tm2.Sm22.ChannelA.DutyCycle = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "duty-cycle-22b") == 0) {
        config.Pwm.Tm2.Sm22.ChannelB.DutyCycle = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "duty-cycle-23a") == 0) {
        config.Pwm.Tm2.Sm23.ChannelA.DutyCycle = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "duty-cycle-23b") == 0) {
        config.Pwm.Tm2.Sm23.ChannelB.DutyCycle = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "duty-cycle-31a") == 0) {
        config.Pwm.Tm3.Sm31.ChannelA.DutyCycle = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "duty-cycle-31b") == 0) {
        config.Pwm.Tm3.Sm31.ChannelB.DutyCycle = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "duty-cycle-40a") == 0) {
        config.Pwm.Tm4.Sm40.ChannelA.DutyCycle = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "duty-cycle-41a") == 0) {
        config.Pwm.Tm4.Sm41.ChannelA.DutyCycle = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "duty-cycle-42a") == 0) {
        config.Pwm.Tm4.Sm42.ChannelA.DutyCycle = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "duty-cycle-42b") == 0) {
        config.Pwm.Tm4.Sm42.ChannelB.DutyCycle = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "phase-shift-21a") == 0) {
        config.Pwm.Tm2.Sm21.ChannelA.PhaseShift = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "phase-shift-22a") == 0) {
        config.Pwm.Tm2.Sm22.ChannelA.PhaseShift = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "phase-shift-22b") == 0) {
        config.Pwm.Tm2.Sm22.ChannelB.PhaseShift = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "phase-shift-23a") == 0) {
        config.Pwm.Tm2.Sm23.ChannelA.PhaseShift = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "phase-shift-23b") == 0) {
        config.Pwm.Tm2.Sm23.ChannelB.PhaseShift = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "phase-shift-41a") == 0) {
        config.Pwm.Tm4.Sm41.ChannelA.PhaseShift = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "phase-shift-42a") == 0) {
        config.Pwm.Tm4.Sm42.ChannelA.PhaseShift = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "phase-shift-42b") == 0) {
        config.Pwm.Tm4.Sm42.ChannelB.PhaseShift = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "print-regs") == 0) {
        if(strcmp( value, "Yes") == 0) {
          config.Pwm.PrintRegs = true;
        }
        else {
          config.Pwm.PrintRegs = false;
        }
      }
      else if(strcmp( name, "sync-pwm") == 0) {
        if(strcmp( value, "Yes") == 0) {
          config.Pwm.SyncPwm = true;
        }
        else {
          config.Pwm.SyncPwm = false;
        }
      }
      else if(strcmp( name, "use-spwm") == 0) {
        if(strcmp( value, "Yes") == 0) {
          config.Pwm.Tm2.UseSpwm = true;
        }
        else {
          config.Pwm.Tm2.UseSpwm = false;
        }
      }
      else if(strcmp( name, "spwm-carrier-signal-frequency") == 0) {
        config.Pwm.Tm2.SpwmCarrierFrequency = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "spwm-modulation-frequency") == 0) {
        config.Pwm.Tm2.SpwmModulationFrequency = strtol(value, nullptr, 10);
      }
    }

    Serial.println(F("Saving configuration..."));
    saveConfiguration(filename, config);

    Serial.println(F("Print config file..."));
    printFile(filename);

    delay(1000);
    configurePwm();

    // Redirect
    res.set("Location", "/settings/pwm");
    res.sendStatus(302);
    res.flush();
    res.end();

    digitalWriteFast (LED_BUILTIN, LOW);
  }
}

void settings_pwm_timer(Request &req, Response &res) {
  char temp[BUFFER_SIZE];

  snprintf(temp, BUFFER_SIZE - 1,
  PwmTimerSettingsPageTemplate,
  config.Pwm.Tm1.Sm13.ChannelA.OnPeriodMicroseconds,
  config.Pwm.Tm1.Sm13.ChannelA.Enabled ? " checked" : "",
  config.Pwm.Tm1.Sm13.ChannelB.OnPeriodMicroseconds,
  config.Pwm.Tm1.Sm13.ChannelB.Enabled ? " checked" : "");
  res.set("Content-Type", "text/html");
  res.printP(temp);
  res.flush();
  res.end();
}

void settings_pwm_timer_update(Request &req, Response &res) {
  if (req.method() != Request::MethodType::POST)
  {
    digitalWriteFast (LED_BUILTIN, HIGH);
    res.set("Content-Type", "text/plain");
    res.printP("Method Not Allowed");
    res.sendStatus(405);
    res.flush();
    res.end();
    digitalWriteFast (LED_BUILTIN, LOW);
  }
  else
  {
    digitalWriteFast (LED_BUILTIN, HIGH);

    writeLog(F("Disabling SM13 channel A"));
    writeLog(F("Disabling SM13 channel B"));
    config.Pwm.Tm1.Sm13.ChannelA.Enabled = false;
    config.Pwm.Tm1.Sm13.ChannelB.Enabled = false;

    while (req.left()) {
      char value[100];
      char name[50];
      if(!req.form(name, 50, value, 100)) {
        return res.sendStatus(400);
      }

      res.print(name);
      res.print(": ");
      res.println(value);

      if(strcmp( name, "period-13a") == 0) {
        config.Pwm.Tm1.Sm13.ChannelA.OnPeriodMicroseconds = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "toggle-13a") == 0) {
        if(strcmp( value, "on") == 0) {
          writeLog(F("Enabling SM13 channel A"));
          config.Pwm.Tm1.Sm13.ChannelA.Enabled = true;
        }
      }
      else if(strcmp( name, "period-13b") == 0) {
        config.Pwm.Tm1.Sm13.ChannelB.OnPeriodMicroseconds = strtol(value, nullptr, 10);
      }
      else if(strcmp( name, "toggle-13b") == 0) {
        if(strcmp( value, "on") == 0) {
          writeLog(F("Enabling SM13 channel B"));
          config.Pwm.Tm1.Sm13.ChannelB.Enabled = true;
        }
      }
    }

    Serial.println(F("Saving configuration..."));
    saveConfiguration(filename, config);

    Serial.println(F("Print config file..."));
    printFile(filename);

    // Redirect
    res.set("Location", "/settings/pwm-timer");
    res.sendStatus(302);
    res.flush();
    res.end();

    digitalWriteFast (LED_BUILTIN, LOW);
  }
}

void configureWebServer() {

  // EthernetClient intermittent issues + workaround https://forum.pjrc.com/threads/68469-QNEthernet-Library-EthernetClient-misbehaving
  // Use writeFully() instead of write()

  Serial.println(F("Configuring web server"));

  app.get("/", &index);

  app.get("/settings/pwm", &settings_pwm);
  app.post("/settings/pwm/update", &settings_pwm_update);

  app.get("/settings/pwm-timer", &settings_pwm_timer);
  app.post("/settings/pwm-timer/update", &settings_pwm_timer_update);

  Serial.printf("Listening for clients on port %u...\n", kServerPort);
  server.begin();
  Serial.println(F("Configured web server"));
}

void DumpText(EthernetClient& client) {

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");  // the connection will be closed after completion of the response
  client.println();
  client.println("<!DOCTYPE HTML>");
  client.println("<html>");

  for (int i = 0; i < 500; i++) {
    // 102 byte string (crlf)
    client.writeFully("0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789\r\n");
  }

  client.println("</html>");
  //50k dumped
}


void processWebServer() {
  EthernetClient client = server.available();

  if (client) {
    Serial.println("New HTTP client");

    if (client.connected()) {
      Serial.println("HTTP client connected");

      Serial.println("Starting aWOT processing");
      app.process(&client);
      Serial.println("Finished aWOT processing");

      Serial.println("Stopping HTTP client");
      client.stop();
      Serial.println("HTTP client disconnected");
    }
  }
}

void configureNtp() {
  ntpUDP.begin(localPort);
  Serial.println(F("Waiting for NTP sync"));
  setSyncProvider(getNtpTime);
}

// Generally, you should use "unsigned long" for variables that hold time
// The value will quickly become too large for an int to store
unsigned long prevSensorMillis = 0;  // will store last time sensors were updated
constexpr long sensorUpdateInterval = 5000;  // interval at which to update sensors, every 5 seconds

unsigned long prevPrintStatsMillis = 0;
constexpr long printStatsUpdateInterval = 60000; // every minute

void loop() {
  digitalWriteFast (LED_BUILTIN, HIGH);

  const unsigned long currentMillis = millis();

  // Use pin 13 for 'scope trigger.
  static volatile byte pin13_val = 0;
  digitalWriteFast (TriggerPin, pin13_val);
  pin13_val = 1 - pin13_val;

  processWebServer();
/*
  if (currentMillis - prevSensorMillis >= sensorUpdateInterval) {
    pollMetrics();
    prevSensorMillis = currentMillis;
  }
*/
  if (currentMillis - prevPrintStatsMillis >= printStatsUpdateInterval) {
    printStats();
    prevPrintStatsMillis = currentMillis;
  }

  digitalWriteFast (TriggerPin, LOW) ;
  digitalWriteFast (LED_BUILTIN, LOW);
}

void configureSensors() {
  sensors.begin();

  Serial.print(F("Parasite power is: "));
  if (sensors.isParasitePowerMode()) {
    Serial.println(F("ON"));
  }
  else {
    Serial.println(F("OFF"));
  }

  Serial.print(F("Locating devices..."));
  Serial.print(F("Found "));
  deviceCount = sensors.getDeviceCount();
  Serial.print(deviceCount, DEC);
  Serial.println(F(" devices."));
  Serial.println(F(""));

  for (int i = 0; i < tempSensorCount; i++)
  {
    const temperatureSensor s = temperatureSensors[i];

/*
    char hexAddrText[24];
    snprintf(hexAddrText,sizeof(hexAddrText),"%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X",
      s.address[0], s.address[1], s.address[2], s.address[3], s.address[4], s.address[5], s.address[6], s.address[7]);

    strncpy(s.addressString, hexAddrText, sizeof(s.addressString)-1);
*/
    Serial.print(F("Setting temperature sensor precision for "));
    Serial.print(s.name);
    Serial.print(F(" ("));
    Serial.print(s.addressString);
    Serial.print(F(") to "));
    Serial.println(TEMP_PRECISION);

    sensors.setResolution(s.address, TEMP_PRECISION);
  }

  Serial.println(F("Printing addresses..."));
  for (int i = 0;  i < deviceCount;  i++)
  {
    Serial.print(F("Sensor "));
    Serial.print(i+1);
    Serial.print(F(" : "));
    sensors.getAddress(Thermometer, i);
    printAddress(Thermometer);
  }
}

void printAddress(DeviceAddress deviceAddress) {
  for (uint8_t i = 0; i < 8; i++)
  {
    Serial.print(F("0x"));
    if (deviceAddress[i] < 0x10) Serial.print(F("0"));
    Serial.print(deviceAddress[i], HEX);
    if (i < 7) Serial.print(F(", "));
  }
  Serial.println(F(""));
}

void pollMetrics() {
  pollConfigSettings();
  pollTemperature();
  pollVoltage();
  pollCurrent();
  pollFreeMemory();
}

void lookupSensorAddresses() {
  // Use this to determine sensor address mappings
  for (int i = 0;  i < deviceCount;  i++)
  {
    sensors.getAddress(Thermometer, i);
    char hexAddrText[24];
    snprintf(hexAddrText,sizeof(hexAddrText),"%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X",
      Thermometer[0], Thermometer[1], Thermometer[2], Thermometer[3], Thermometer[4], Thermometer[5], Thermometer[6], Thermometer[7]);
    tempC = sensors.getTempC(Thermometer);
    char buffer[10];
    char temp[500];
    dtostrf(tempC, 4, 6, buffer);
    snprintf(temp, sizeof(temp), "sensor_address=%s temperature=%s", hexAddrText, buffer);
    Serial.println(temp);
  }
}

void pollTemperature() {
  sensors.requestTemperatures();

  String postRequest = F("");
  const uint64_t timestamp = now() * 1000000000;
  char buffer[10];
  char temp[200];

  for (int i = 0; i < tempSensorCount; i++)
  {
    const temperatureSensor s = temperatureSensors[i];
    tempC = sensors.getTempC(s.address);

    // Handle sensor issues by
    if(tempC == -127) {
      tempC = 0;
    }

    dtostrf(tempC, 4, 6, buffer);
    snprintf(temp, sizeof(temp), "temperatureSensors,sensor_id=%s,sensor_address=%s temperature=%s %llu\n", s.name, s.addressString, buffer, timestamp);
    postRequest += temp;
  }

  tempC = InternalTemperatureClass::readTemperatureC();
  dtostrf(tempC, 4, 6, buffer);
  snprintf(temp, sizeof(temp), "temperatureSensors,sensor_id=%s temperature=%s %llu\n", "CPU", buffer, timestamp);
  postRequest += temp;

  //Serial.print(postRequest);

  writeInfluxDb(postRequest);
}

void pollVoltage() {
}

void pollCurrent() {
}

void pollFreeMemory() {
  //int freeMemory = getFreeMemory();

  //Serial.print(F("Free memory: "));
  //Serial.print(freeMemory);
  //Serial.println(F(" bytes"));
}

void pollConfigSettings() {
  String postRequest = F("");
  uint64_t timestamp = now() * 1000000000;
  char buffer[30];
  char temp[200];

  // On period
  ultoa(config.Pwm.Tm1.Sm13.ChannelA.OnPeriodMicroseconds, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=onPeriodSm13ChA,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm1.Sm13.ChannelB.OnPeriodMicroseconds, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=onPeriodSm13ChB,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  // PWM frequency
  ultoa(config.Pwm.Tm1.Sm13.PwmFrequency, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=pwmFrequencySm13,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm2.Sm20.PwmFrequency, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=pwmFrequencySm20,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm2.Sm21.PwmFrequency, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=pwmFrequencySm21,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm2.Sm22.PwmFrequency, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=pwmFrequencySm22,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm2.Sm23.PwmFrequency, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=pwmFrequencySm23,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm3.Sm31.PwmFrequency, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=pwmFrequencySm31,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm4.Sm40.PwmFrequency, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=pwmFrequencySm40,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm4.Sm41.PwmFrequency, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=pwmFrequencySm41,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm4.Sm42.PwmFrequency, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=pwmFrequencySm42,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  // Duty cycle
  ultoa(config.Pwm.Tm1.Sm13.DeadTime, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=deadTimeSm13,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm2.Sm20.DeadTime, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=deadTimeSm20,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm2.Sm21.DeadTime, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=deadTimeSm21,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm2.Sm22.DeadTime, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=deadTimeSm22,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm2.Sm23.DeadTime, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=deadTimeSm23,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm3.Sm31.DeadTime, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=deadTimeSm31,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm4.Sm40.DeadTime, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=deadTimeSm40,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm4.Sm41.DeadTime, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=deadTimeSm41,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm4.Sm42.DeadTime, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=deadTimeSm42,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  // Duty cycle
  ultoa(config.Pwm.Tm1.Sm13.ChannelA.DutyCycle, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=dutyCycleSm13ChA,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm1.Sm13.ChannelB.DutyCycle, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=dutyCycleSm13ChB,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm2.Sm20.ChannelA.DutyCycle, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=dutyCycleSm20ChA,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm2.Sm20.ChannelB.DutyCycle, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=dutyCycleSm20ChB,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm2.Sm21.ChannelA.DutyCycle, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=dutyCycleSm21ChA,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm2.Sm22.ChannelA.DutyCycle, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=dutyCycleSm22ChA,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm2.Sm22.ChannelB.DutyCycle, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=dutyCycleSm22ChB,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm2.Sm23.ChannelA.DutyCycle, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=dutyCycleSm23ChA,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm2.Sm23.ChannelB.DutyCycle, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=dutyCycleSm23ChB,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm3.Sm31.ChannelA.DutyCycle, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=dutyCycleSm31ChA,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm3.Sm31.ChannelB.DutyCycle, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=dutyCycleSm31ChB,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm4.Sm40.ChannelA.DutyCycle, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=dutyCycleSm40ChA,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm4.Sm41.ChannelA.DutyCycle, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=dutyCycleSm41ChA,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm4.Sm42.ChannelA.DutyCycle, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=dutyCycleSm42ChA,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm4.Sm42.ChannelB.DutyCycle, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=dutyCycleSm42ChB,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm2.Sm20.ChannelA.PhaseShift, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=phaseShiftSm20ChA,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm2.Sm20.ChannelB.PhaseShift, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=phaseShiftSm20ChB,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm2.Sm22.ChannelA.PhaseShift, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=phaseShiftSm22ChA,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm2.Sm22.ChannelB.PhaseShift, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=phaseShiftSm22ChB,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm2.Sm23.ChannelA.PhaseShift, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=phaseShiftSm23ChA,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm2.Sm23.ChannelB.PhaseShift, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=phaseShiftSm23ChB,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm4.Sm41.ChannelA.PhaseShift, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=phaseShiftSm41ChA,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm4.Sm42.ChannelA.PhaseShift, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=phaseShiftSm42ChA,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm4.Sm42.ChannelB.PhaseShift, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=phaseShiftSm42ChB,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  // SPWM
  ultoa(config.Pwm.Tm2.SpwmCarrierFrequency, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=spwmCarrierFrequency,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm2.SpwmModulationFrequency, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=spwmModulationFrequency,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  ultoa(config.Pwm.Tm2.UseSpwm, buffer, DEC);
  snprintf(temp, sizeof(temp), "configSettings,setting_id=useSpwm,setting_category=pwm value=%s %llu\n", buffer, timestamp);
  postRequest += temp;

  //Serial.print(postRequest);

  writeInfluxDb(postRequest);
}

void configurePwm() {
  writeLog(F("Initializing PWM"));
  attachInterruptVectors();
  configurePwmSyncTimer();
  configureModule1();
  configureModule2();
  configureModule3();
  configureModule4();
}

void configureModule1() {
  // sPWM
  Config pwmConfig;

  /*
  SubModule Sm13 (8,7);
  */
  Serial.println(F("Configuring TM1"));

  Tm1.disable();
  Tm1.setPwmLdok(false);

  Sm13.disable();
  Sm13.setPwmLdok(false);

  if(config.Pwm.SyncPwm) {
    writeLog (F("SM13 synchronised with PIT timer"));
    pwmConfig.setInitializationControl (kPWM_Initialize_ExtSync);
  }
  else {
    writeLog (F("SM13 using local timer"));
    pwmConfig.setInitializationControl (kPWM_Initialize_LocalSync);
  }

  pwmConfig.setReloadLogic (kPWM_ReloadPwmFullCycle);
  pwmConfig.setPairOperation (kPWM_Independent);
  pwmConfig.setMode(kPWM_SignedEdgeAligned);
  pwmConfig.setPwmFreqHz(config.Pwm.Tm1.Sm13.PwmFrequency);

  if (!Sm13.configure (pwmConfig)) {
    writeLog (F("SM13 init failed"));
    exit (EXIT_FAILURE);
  }

  const uint16_t deadTimeCycles = (static_cast<uint64_t>(Tm1.srcClockHz()) * config.Pwm.Tm1.Sm13.DeadTime) / 1000000000;
  Sm13.setupDeadtime(deadTimeCycles);

  char strBuf[150];
  sprintf(strBuf, "Set TM1 deadtime to %hu cycles", deadTimeCycles);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));

  if(Sm13.setPwmFrequency(config.Pwm.Tm1.Sm13.PwmFrequency, false, true)) {
    sprintf(strBuf, "Set SM13 PWM freq. to %luHz", config.Pwm.Tm1.Sm13.PwmFrequency);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }
  else {
    sprintf(strBuf, "Failed to set SM13 PWM freq. to %luHz", config.Pwm.Tm1.Sm13.PwmFrequency);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }

  Sm13.setupDutyCycle(ChanA, config.Pwm.Tm1.Sm13.ChannelA.DutyCycle);
  Sm13.setupDutyCycle(ChanB, config.Pwm.Tm1.Sm13.ChannelB.DutyCycle);

  Tm1.setPwmLdok(true);
  Tm1.enable();

  Sm13.setPwmLdok(true);
  Sm13.enable();

  if (!Tm1.begin()) {
    writeLog (F("Failed to start TM1"));
    exit (EXIT_FAILURE);
  }

  const uint32_t pwmFrequency = Sm13.pwmFrequency();
  const uint32_t pwmMode = Sm13.pwmMode();
  const uint16_t deadtimeSettingChanA = Sm13.deadtimeSetting(ChanA);
  const uint16_t deadtimeSettingChanB = Sm13.deadtimeSetting(ChanB);
  const uint16_t dutyCycleSettingChanA = Sm13.dutyCycleSetting(ChanA);
  const uint16_t dutyCycleSettingChanB = Sm13.dutyCycleSetting(ChanB);
  const char *prescale = prescaleStr[Sm13.prescaler()];
  sprintf(strBuf, "Configured SM13 %luHz %s md:%lu dtA:%hu dcA:%hhu%% dtB:%hu dcB:%hhu%%", pwmFrequency, prescale, pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA, deadtimeSettingChanB, dutyCycleSettingChanB);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));

  if(config.Pwm.PrintRegs) {
    writeLog (F("Printing SM13 register values"));
    Sm13.printRegs();
  }

  writeLog (F("Started TM1"));
}

void configureModule2() {
  // sPWM
  Config pwmConfig;

  /*
  SubModule Sm20 (4,33); - SPWM
  SubModule Sm21 (5);
  SubModule Sm22 (6,9); - SPWM
  SubModule Sm23 (36,37);
  */
  Serial.println(F("Configuring TM2"));

  char strBuf[150];

  if(config.Pwm.Tm2.UseSpwm) {
    disableModule2PwmInterrupts();
  }

  Tm2.disable();
  Tm2.setPwmLdok(false);

  Sm20.disable();
  Sm20.setPwmLdok(false);

  Sm21.disable();
  Sm21.setPwmLdok(false);

  Sm22.disable();
  Sm22.setPwmLdok(false);

  Sm23.disable();
  Sm23.setPwmLdok(false);

  if(config.Pwm.Tm2.UseSpwm) {
    Serial.println(F("Using SPWM for TM2"));

    const auto spwmModulationFrequency = static_cast<float>(config.Pwm.Tm2.SpwmModulationFrequency);
    const auto spwmCarrierFrequency = static_cast<float>(config.Pwm.Tm2.SpwmCarrierFrequency);
    const float32_t spwmRatio = spwmModulationFrequency / spwmCarrierFrequency;
    const float32_t spwmUpdateSpeed = 2.0f * static_cast<float>(PI) * spwmRatio;

    ATOMIC_BLOCK (ATOMIC_RESTORESTATE) {
      vSpwmUpdateSpeed = spwmUpdateSpeed;
    }

    pwmConfig.setReloadLogic (kPWM_ReloadPwmFullCycle);
    pwmConfig.setPairOperation (kPWM_ComplementaryPwmA);

    if (!Sm20.configure (pwmConfig)) {
      writeLog (F("SM20 init failed"));
      exit (EXIT_FAILURE);
    }

    // Initialize submodule 2, make it use same counter clock as submodule 0
    pwmConfig.setClockSource (kPWM_Submodule0Clock);
    pwmConfig.setInitializationControl (kPWM_Initialize_MasterSync);

    if (!Sm22.configure (pwmConfig)) {
      writeLog (F("SM22 init failed"));
      exit (EXIT_FAILURE);
    }

    sprintf(strBuf, "SPWM carrier frequency: %luHz, modulation frequency: %luHz, ratio: %lf, update speed: %lf", config.Pwm.Tm2.SpwmCarrierFrequency, config.Pwm.Tm2.SpwmModulationFrequency, spwmRatio, spwmUpdateSpeed);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }
  else {
    Serial.println(F("Using PWM for TM2"));

    if(config.Pwm.SyncPwm) {
      writeLog (F("SM20 synchronised with PIT timer"));
      pwmConfig.setInitializationControl (kPWM_Initialize_ExtSync);
    }
    else {
      writeLog (F("SM20 using local timer"));
      pwmConfig.setInitializationControl (kPWM_Initialize_LocalSync);
    }

    pwmConfig.setReloadLogic (kPWM_ReloadPwmFullCycle);
    pwmConfig.setPairOperation (kPWM_ComplementaryPwmA);
    pwmConfig.setMode(kPWM_SignedCenterAligned);
    pwmConfig.setPwmFreqHz(config.Pwm.Tm2.Sm20.PwmFrequency);

    if (!Sm20.configure (pwmConfig)) {
      writeLog (F("SM20 init failed"));
      exit (EXIT_FAILURE);
    }

    pwmConfig.setClockSource (kPWM_Submodule0Clock);
    pwmConfig.setInitializationControl (kPWM_Initialize_MasterSync);
    pwmConfig.setPairOperation (kPWM_Independent);
    pwmConfig.setPwmFreqHz(config.Pwm.Tm2.Sm21.PwmFrequency);

    if (!Sm21.configure (pwmConfig)) {
      writeLog (F("SM21 init failed"));
      exit (EXIT_FAILURE);
    }

    pwmConfig.setPairOperation (kPWM_ComplementaryPwmA);
    pwmConfig.setPwmFreqHz(config.Pwm.Tm2.Sm22.PwmFrequency);

    if (!Sm22.configure (pwmConfig)) {
      writeLog (F("SM22 init failed"));
      exit (EXIT_FAILURE);
    }

    pwmConfig.setPwmFreqHz(config.Pwm.Tm2.Sm23.PwmFrequency);

    if (!Sm23.configure (pwmConfig)) {
      writeLog (F("SM23 init failed"));
      exit (EXIT_FAILURE);
    }

    if(Sm20.setPwmFrequency(config.Pwm.Tm2.Sm20.PwmFrequency, false, true)) {
      sprintf(strBuf, "Set SM20 PWM freq. to %luHz", config.Pwm.Tm2.Sm20.PwmFrequency);
      writeLog(strBuf);
      memset(strBuf, 0, sizeof(strBuf));
    }
    else {
      sprintf(strBuf, "Failed to set SM20 PWM freq. to %luHz", config.Pwm.Tm2.Sm20.PwmFrequency);
      writeLog(strBuf);
      memset(strBuf, 0, sizeof(strBuf));
    }

    Sm20.setupDutyCycle(ChanA, config.Pwm.Tm2.Sm20.ChannelA.DutyCycle);

    sprintf(strBuf, "Setting SM20 ChanA duty cycle to %u", config.Pwm.Tm2.Sm20.ChannelA.DutyCycle);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));

    Sm20.setupDutyCycle(ChanB, config.Pwm.Tm2.Sm20.ChannelB.DutyCycle);

    sprintf(strBuf, "Setting SM20 ChanB duty cycle to %u", config.Pwm.Tm2.Sm20.ChannelB.DutyCycle);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));

    if(Sm21.setPwmFrequency(config.Pwm.Tm2.Sm21.PwmFrequency, false, true)) {
      sprintf(strBuf, "Set SM21 PWM freq. to %luHz", config.Pwm.Tm2.Sm21.PwmFrequency);
      writeLog(strBuf);
      memset(strBuf, 0, sizeof(strBuf));
    }
    else {
      sprintf(strBuf, "Failed to set SM21 PWM freq. to %luHz", config.Pwm.Tm2.Sm21.PwmFrequency);
      writeLog(strBuf);
      memset(strBuf, 0, sizeof(strBuf));
    }

    if(config.Pwm.Tm2.Sm21.ChannelA.PhaseShift != 0) {
      Sm21.setupDutyCycle(ChanA, FIFTY_PERCENT_DUTY);

      sprintf(strBuf, "SM21 ChanA phase shift was %u, setting duty cycle to %u", config.Pwm.Tm2.Sm21.ChannelA.PhaseShift, FIFTY_PERCENT_DUTY);
      writeLog(strBuf);
      memset(strBuf, 0, sizeof(strBuf));
    }
    else {
      Sm21.setupDutyCycle(ChanA, config.Pwm.Tm2.Sm21.ChannelA.DutyCycle);

      sprintf(strBuf, "Setting SM21 ChanA duty cycle to %u", config.Pwm.Tm2.Sm21.ChannelA.DutyCycle);
      writeLog(strBuf);
      memset(strBuf, 0, sizeof(strBuf));
    }

    if(Sm22.setPwmFrequency(config.Pwm.Tm2.Sm22.PwmFrequency, false, true)) {
      sprintf(strBuf, "Set SM22 PWM freq. to %luHz", config.Pwm.Tm2.Sm22.PwmFrequency);
      writeLog(strBuf);
      memset(strBuf, 0, sizeof(strBuf));
    }
    else {
      sprintf(strBuf, "Failed to set SM22 PWM freq. to %luHz", config.Pwm.Tm2.Sm22.PwmFrequency);
      writeLog(strBuf);
      memset(strBuf, 0, sizeof(strBuf));
    }

    if(config.Pwm.Tm2.Sm22.ChannelA.PhaseShift != 0) {
      Sm22.setupDutyCycle(ChanA, FIFTY_PERCENT_DUTY);

      sprintf(strBuf, "SM22 ChanA phase shift was %u, setting duty cycle to %u", config.Pwm.Tm2.Sm22.ChannelA.PhaseShift, FIFTY_PERCENT_DUTY);
      writeLog(strBuf);
      memset(strBuf, 0, sizeof(strBuf));
    }
    else {
      Sm22.setupDutyCycle(ChanA, config.Pwm.Tm2.Sm22.ChannelA.DutyCycle);

      sprintf(strBuf, "Setting SM22 ChanA duty cycle to %u", config.Pwm.Tm2.Sm22.ChannelA.DutyCycle);
      writeLog(strBuf);
      memset(strBuf, 0, sizeof(strBuf));
    }

    if(config.Pwm.Tm2.Sm22.ChannelB.PhaseShift != 0) {
      Sm22.setupDutyCycle(ChanB, FIFTY_PERCENT_DUTY);

      sprintf(strBuf, "SM22 ChanB phase shift was %u, setting duty cycle to %u", config.Pwm.Tm2.Sm22.ChannelB.PhaseShift, FIFTY_PERCENT_DUTY);
      writeLog(strBuf);
      memset(strBuf, 0, sizeof(strBuf));
    }
    else {
      Sm22.setupDutyCycle(ChanB, config.Pwm.Tm2.Sm22.ChannelB.DutyCycle);

      sprintf(strBuf, "Setting SM22 ChanB duty cycle to %u", config.Pwm.Tm2.Sm22.ChannelB.DutyCycle);
      writeLog(strBuf);
      memset(strBuf, 0, sizeof(strBuf));
    }

    if(Sm23.setPwmFrequency(config.Pwm.Tm2.Sm23.PwmFrequency, false, true)) {
      sprintf(strBuf, "Set SM23 PWM freq. to %luHz", config.Pwm.Tm2.Sm23.PwmFrequency);
      writeLog(strBuf);
      memset(strBuf, 0, sizeof(strBuf));
    }
    else {
      sprintf(strBuf, "Failed to set SM23 PWM freq. to %luHz", config.Pwm.Tm2.Sm23.PwmFrequency);
      writeLog(strBuf);
      memset(strBuf, 0, sizeof(strBuf));
    }

    if(config.Pwm.Tm2.Sm23.ChannelA.PhaseShift != 0) {
      Sm23.setupDutyCycle(ChanA, FIFTY_PERCENT_DUTY);

      sprintf(strBuf, "SM23 ChanA phase shift was %u, setting duty cycle to %u", config.Pwm.Tm2.Sm23.ChannelA.PhaseShift, FIFTY_PERCENT_DUTY);
      writeLog(strBuf);
      memset(strBuf, 0, sizeof(strBuf));
    }
    else {
      Sm23.setupDutyCycle(ChanA, config.Pwm.Tm2.Sm23.ChannelA.DutyCycle);

      sprintf(strBuf, "Setting SM23 ChanA duty cycle to %u", config.Pwm.Tm2.Sm23.ChannelA.DutyCycle);
      writeLog(strBuf);
      memset(strBuf, 0, sizeof(strBuf));
    }

    if(config.Pwm.Tm2.Sm23.ChannelB.PhaseShift != 0) {
      Sm23.setupDutyCycle(ChanB, FIFTY_PERCENT_DUTY);

      sprintf(strBuf, "SM23 ChanB phase shift was %u, setting duty cycle to %u", config.Pwm.Tm2.Sm23.ChannelB.PhaseShift, FIFTY_PERCENT_DUTY);
      writeLog(strBuf);
      memset(strBuf, 0, sizeof(strBuf));
    }
    else {
      Sm23.setupDutyCycle(ChanB, config.Pwm.Tm2.Sm23.ChannelB.DutyCycle);

      sprintf(strBuf, "Setting SM23 ChanB duty cycle to %u", config.Pwm.Tm2.Sm21.ChannelB.DutyCycle);
      writeLog(strBuf);
      memset(strBuf, 0, sizeof(strBuf));
    }
  }

  uint16_t deadTimeCycles = (static_cast<uint64_t>(Tm2.srcClockHz()) * config.Pwm.Tm2.Sm20.DeadTime) / 1000000000;
  Sm20.setupDeadtime(deadTimeCycles);
  sprintf(strBuf, "Set Sm20 deadtime to %hu cycles", deadTimeCycles);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));

  deadTimeCycles = (static_cast<uint64_t>(Tm2.srcClockHz()) * config.Pwm.Tm2.Sm21.DeadTime) / 1000000000;
  Sm21.setupDeadtime(deadTimeCycles);
  sprintf(strBuf, "Set Sm21 deadtime to %hu cycles", deadTimeCycles);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));

  uint32_t pwmFrequency = Sm21.pwmFrequency();
  uint32_t pwmMode = Sm21.pwmMode();
  uint16_t deadtimeSettingChanA = Sm21.deadtimeSetting(ChanA);
  uint16_t dutyCycleSettingChanA = Sm21.dutyCycleSetting(ChanA);
  const char *prescale = prescaleStr[Sm21.prescaler()];
  sprintf(strBuf, "#4 PWM 2.1 %luHz %s md:%lu dtA:%hu dcA:%hhu%%", pwmFrequency, prescale, pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));

  deadTimeCycles = (static_cast<uint64_t>(Tm2.srcClockHz()) * config.Pwm.Tm2.Sm22.DeadTime) / 1000000000;
  Sm22.setupDeadtime(deadTimeCycles);
  sprintf(strBuf, "Set Sm22 deadtime to %hu cycles", deadTimeCycles);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));

  deadTimeCycles = (static_cast<uint64_t>(Tm2.srcClockHz()) * config.Pwm.Tm2.Sm23.DeadTime) / 1000000000;
  Sm23.setupDeadtime(deadTimeCycles);
  sprintf(strBuf, "Set Sm23 deadtime to %hu cycles", deadTimeCycles);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));

  if(config.Pwm.SyncPwm) {
    // Enable output trigger, used to synchronise other PWM modules
    /*
    Refer to page 3095 in i.MX RT1060 Processor Reference Manual, Rev. 3
    55.3.1 PWM Capabilities

        VAL1 ($0100)        /|      /|
        VAL3               / |     / |
        VAL5              /  |    /  |
        VAL0 ($0000)     /   |   /   |
        VAL4            /    |  /    |  / (etc.)
        VAL2           /     | /     | /
        INIT ($FF00)  /      |/      |/
    */
    Sm20.enableOutputTrigger(kPWM_ValueRegister_2);
  }

  if(config.Pwm.Tm2.UseSpwm) {
    Sm20.setPwmLdok(true);
    Sm20.enable();

    Sm22.setPwmLdok(true);
    Sm22.enable();
  }
  else {
    Sm20.setPwmLdok(true);
    Sm20.enable();

    Sm21.setPwmLdok(true);
    Sm21.enable();

    Sm22.setPwmLdok(true);
    Sm22.enable();

    Sm23.setPwmLdok(true);
    Sm23.enable();
  }

  Tm2.setPwmLdok(true);
  Tm2.enable();

  if (!Tm2.begin()) {
    writeLog (F("Failed to start TM2"));
    exit (EXIT_FAILURE);
  }

  if(config.Pwm.Tm2.UseSpwm) {
    enableModule2PwmInterrupts();
  }

  pwmFrequency = Sm21.pwmFrequency();
  pwmMode = Sm21.pwmMode();
  deadtimeSettingChanA = Sm21.deadtimeSetting(ChanA);
  dutyCycleSettingChanA = Sm21.dutyCycleSetting(ChanA);
  prescale = prescaleStr[Sm21.prescaler()];
  sprintf(strBuf, "#5 PWM 2.1 %luHz %s md:%lu dtA:%hu dcA:%hhu%%", pwmFrequency, prescale, pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));

  // Phase shift
  /*
  if(Sm21.setupPwmPhaseShift(ChanA, config.Pwm.Tm2.Sm21.ChannelA.PhaseShift, true)) {
    sprintf(strBuf, "Setting phase shift to %u for SM21 ChanA succeeded", config.Pwm.Tm2.Sm21.ChannelA.PhaseShift);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }
  else {
    sprintf(strBuf, "Setting phase shift to %u for SM21 ChanA failed", config.Pwm.Tm2.Sm21.ChannelA.PhaseShift);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }
  */

  if(Sm22.setupPwmPhaseShift(ChanA, config.Pwm.Tm2.Sm22.ChannelA.PhaseShift, true)) {
    sprintf(strBuf, "Setting phase shift to %u for SM22 ChanA succeeded", config.Pwm.Tm2.Sm22.ChannelA.PhaseShift);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }
  else {
    sprintf(strBuf, "Setting phase shift to %u for SM22 ChanA failed", config.Pwm.Tm2.Sm22.ChannelA.PhaseShift);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }

  if(Sm22.setupPwmPhaseShift(ChanB, config.Pwm.Tm2.Sm22.ChannelB.PhaseShift, true)) {
    sprintf(strBuf, "Setting phase shift to %u for SM22 ChanB succeeded", config.Pwm.Tm2.Sm22.ChannelB.PhaseShift);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }
  else {
    sprintf(strBuf, "Setting phase shift to %u for SM22 ChanB failed", config.Pwm.Tm2.Sm22.ChannelB.PhaseShift);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }

  if(Sm23.setupPwmPhaseShift(ChanA, config.Pwm.Tm2.Sm23.ChannelA.PhaseShift, true)) {
    sprintf(strBuf, "Setting phase shift to %u for SM23 ChanA succeeded", config.Pwm.Tm2.Sm23.ChannelA.PhaseShift);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }
  else {
    sprintf(strBuf, "Setting phase shift to %u for SM23 ChanA failed", config.Pwm.Tm2.Sm23.ChannelA.PhaseShift);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }

  if(Sm23.setupPwmPhaseShift(ChanB, config.Pwm.Tm2.Sm23.ChannelB.PhaseShift, true)) {
    sprintf(strBuf, "Setting phase shift to %u for SM23 ChanB succeeded", config.Pwm.Tm2.Sm23.ChannelB.PhaseShift);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }
  else {
    sprintf(strBuf, "Setting phase shift to %u for SM23 ChanB failed", config.Pwm.Tm2.Sm23.ChannelB.PhaseShift);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }

  pwmFrequency = Sm20.pwmFrequency();
  pwmMode = Sm20.pwmMode();
  deadtimeSettingChanA = Sm20.deadtimeSetting(ChanA);
  uint16_t deadtimeSettingChanB = Sm20.deadtimeSetting(ChanB);
  dutyCycleSettingChanA = Sm20.dutyCycleSetting(ChanA);
  uint16_t dutyCycleSettingChanB = Sm20.dutyCycleSetting(ChanB);
  prescale = prescaleStr[Sm20.prescaler()];
  sprintf(strBuf, "Configured PWM 2.0 %luHz %s md:%lu dtA:%hu dcA:%hhu%% dtB:%hu dcB:%hhu%%", pwmFrequency, prescale, pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA, deadtimeSettingChanB, dutyCycleSettingChanB);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));

  pwmFrequency = Sm21.pwmFrequency();
  pwmMode = Sm21.pwmMode();
  deadtimeSettingChanA = Sm21.deadtimeSetting(ChanA);
  dutyCycleSettingChanA = Sm21.dutyCycleSetting(ChanA);
  prescale = prescaleStr[Sm21.prescaler()];
  sprintf(strBuf, "Configured PWM 2.1 %luHz %s md:%lu dtA:%hu dcA:%hhu%%", pwmFrequency, prescale, pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));

  pwmFrequency = Sm22.pwmFrequency();
  pwmMode = Sm22.pwmMode();
  deadtimeSettingChanA = Sm22.deadtimeSetting(ChanA);
  deadtimeSettingChanB = Sm22.deadtimeSetting(ChanB);
  dutyCycleSettingChanA = Sm22.dutyCycleSetting(ChanA);
  dutyCycleSettingChanB = Sm22.dutyCycleSetting(ChanB);
  prescale = prescaleStr[Sm22.prescaler()];
  sprintf(strBuf, "Configured PWM 2.2 %luHz %s md:%lu dtA:%hu dcA:%hhu%% dtB:%hu dcB:%hhu%%", pwmFrequency, prescale, pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA, deadtimeSettingChanB, dutyCycleSettingChanB);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));

  pwmFrequency = Sm23.pwmFrequency();
  pwmMode = Sm23.pwmMode();
  deadtimeSettingChanA = Sm23.deadtimeSetting(ChanA);
  deadtimeSettingChanB = Sm23.deadtimeSetting(ChanB);
  dutyCycleSettingChanA = Sm23.dutyCycleSetting(ChanA);
  dutyCycleSettingChanB = Sm23.dutyCycleSetting(ChanB);
  prescale = prescaleStr[Sm23.prescaler()];
  sprintf(strBuf, "Configured PWM 2.3 %luHz %s md:%lu dtA:%hu dcA:%hhu%% dtB:%hu dcB:%hhu%%", pwmFrequency, prescale, pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA, deadtimeSettingChanB, dutyCycleSettingChanB);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));

  if(config.Pwm.PrintRegs) {
    writeLog (F("Printing SM20 register values"));
    Sm20.printRegs();
    writeLog (F("Printing SM21 register values"));
    Sm21.printRegs();
    writeLog (F("Printing SM22 register values"));
    Sm22.printRegs();
    writeLog (F("Printing SM23 register values"));
    Sm23.printRegs();
  }

  writeLog (F("Started TM2"));
}

void configureModule3() {
  // sPWM
  Config pwmConfig;

  /*
  SubModule Sm31 (29,28);
  */

  Serial.println(F("Configuring TM3"));

  char strBuf[150];

  Tm3.disable();
  Tm3.setPwmLdok(false);

  Sm31.disable();
  Sm31.setPwmLdok(false);

  if(config.Pwm.SyncPwm) {
    writeLog (F("SM31 synchronised with PIT timer"));
    pwmConfig.setInitializationControl (kPWM_Initialize_ExtSync);
  }
  else {
    writeLog (F("SM31 using local timer"));
    pwmConfig.setInitializationControl (kPWM_Initialize_LocalSync);
  }

  pwmConfig.setReloadLogic (kPWM_ReloadPwmFullCycle);
  pwmConfig.setPairOperation (kPWM_ComplementaryPwmA);
  pwmConfig.setMode(kPWM_SignedEdgeAligned);
  pwmConfig.setPwmFreqHz(config.Pwm.Tm3.Sm31.PwmFrequency);

  if (!Sm31.configure (pwmConfig)) {
    writeLog (F("TM3 init failed"));
    exit (EXIT_FAILURE);
  }

  const uint16_t deadTimeCycles = (static_cast<uint64_t>(Tm3.srcClockHz()) * config.Pwm.Tm3.Sm31.DeadTime) / 1000000000;
  Sm31.setupDeadtime(deadTimeCycles);

  // Set ChanB to be an inverted mirror of ChanA
  Sm31.setChannelOutput(ChanB, kPWM_InvertState);

  writeLog(F("Set SM31 ChanB to inverted"));

  if(Sm31.setPwmFrequency(config.Pwm.Tm3.Sm31.PwmFrequency, false, true)) {
    sprintf(strBuf, "Set SM31 PWM freq. to %luHz", config.Pwm.Tm3.Sm31.PwmFrequency);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }
  else {
    sprintf(strBuf, "Failed to set SM31 PWM freq. to %luHz", config.Pwm.Tm3.Sm31.PwmFrequency);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }

  Sm31.setupDutyCycle(ChanA, config.Pwm.Tm3.Sm31.ChannelA.DutyCycle);

  sprintf(strBuf, "Setting SM31 ChanA duty cycle to %u", config.Pwm.Tm3.Sm31.ChannelA.DutyCycle);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));

  Sm31.setupDutyCycle(ChanB, config.Pwm.Tm3.Sm31.ChannelB.DutyCycle);

  sprintf(strBuf, "Setting SM31 ChanB duty cycle to %u", config.Pwm.Tm3.Sm31.ChannelB.DutyCycle);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));

  Tm3.setPwmLdok(true);
  Tm3.enable();

  Sm31.setPwmLdok(true);
  Sm31.enable();

  if (!Tm3.begin()) {
    writeLog (F("Failed to start TM3"));
    exit (EXIT_FAILURE);
  }

  sprintf(strBuf, "Set TM3 deadtime to %hu cycles", deadTimeCycles);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));

  const uint32_t pwmFrequency = Sm31.pwmFrequency();
  const uint32_t pwmMode = Sm31.pwmMode();
  const uint16_t deadtimeSettingChanA = Sm31.deadtimeSetting(ChanA);
  const uint16_t deadtimeSettingChanB = Sm31.deadtimeSetting(ChanB);
  const uint16_t dutyCycleSettingChanA = Sm31.dutyCycleSetting(ChanA);
  const uint16_t dutyCycleSettingChanB = Sm31.dutyCycleSetting(ChanB);
  const char *prescale = prescaleStr[Sm31.prescaler()];
  sprintf(strBuf, "Configured SM31 %luHz %s md:%lu dtA:%hu dcA:%hhu%% dtB:%hu dcB:%hhu%%", pwmFrequency, prescale, pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA, deadtimeSettingChanB, dutyCycleSettingChanB);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));

  if(config.Pwm.PrintRegs) {
    writeLog (F("Printing SM31 register values"));
    Sm31.printRegs();
  }

  writeLog (F("Started TM3"));
}

void configureModule4() {
  Config pwmConfig;

  /*
  SubModule Sm40 (22);
  SubModule Sm41 (23);
  SubModule Sm42 (2,3);
  */
  Serial.println(F("Configuring TM4"));

  Tm4.disable();
  Tm4.setPwmLdok(false);

  if(config.Pwm.SyncPwm) {
    writeLog (F("SM40 synchronised with PIT timer"));
    pwmConfig.setInitializationControl (kPWM_Initialize_ExtSync);
  }
  else {
    writeLog (F("SM40 using local timer"));
    pwmConfig.setInitializationControl (kPWM_Initialize_LocalSync);
  }

  pwmConfig.setReloadLogic (kPWM_ReloadPwmFullCycle);
  pwmConfig.setMode(kPWM_SignedEdgeAligned);
  pwmConfig.setPwmFreqHz(config.Pwm.Tm4.Sm40.PwmFrequency);

  if (!Sm40.configure (pwmConfig)) {
    writeLog (F("SM40 init failed"));
    exit (EXIT_FAILURE);
  }

  pwmConfig.setClockSource (kPWM_Submodule0Clock);
  pwmConfig.setInitializationControl (kPWM_Initialize_MasterSync);

  if (!Sm41.configure (pwmConfig)) {
    writeLog (F("SM41 init failed"));
    exit (EXIT_FAILURE);
  }

  //TODO: pwmConfig.setPairOperation (kPWM_ComplementaryPwmA);

  writeLog (F("-----#1 Printing SM42 register values"));
  Sm42.printRegs();

  pwmConfig.setClockSource (kPWM_BusClock);
  pwmConfig.setInitializationControl (kPWM_Initialize_LocalSync);

  if (!Sm42.configure (pwmConfig)) {
    writeLog (F("SM42 init failed"));
    exit (EXIT_FAILURE);
  }

  writeLog (F("-----#2 Printing SM42 register values"));
  Sm42.printRegs();

  char strBuf[150];

  uint16_t deadTimeCycles = (static_cast<uint64_t>(Tm4.srcClockHz()) * config.Pwm.Tm4.Sm40.DeadTime) / 1000000000;
  Sm40.setupDeadtime(deadTimeCycles);
  sprintf(strBuf, "Set TM4.0 deadtime to %hu cycles", deadTimeCycles);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));

  deadTimeCycles = (static_cast<uint64_t>(Tm4.srcClockHz()) * config.Pwm.Tm4.Sm41.DeadTime) / 1000000000;
  Sm41.setupDeadtime(deadTimeCycles);
  sprintf(strBuf, "Set TM4.1 deadtime to %hu cycles", deadTimeCycles);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));

  /*
  deadTimeCycles = (static_cast<uint64_t>(Tm4.srcClockHz()) * config.Pwm.Tm4.Sm42.DeadTime) / 1000000000;
  Sm42.setupDeadtime(deadTimeCycles);
  sprintf(strBuf, "Set TM4.2 deadtime to %hu cycles", deadTimeCycles);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));
  */

  Sm41.setChannelOutput(ChanA, kPWM_InvertState);
  writeLog(F("Set Sm41 ChanA to inverted"));

  //Sm42.setChannelOutput(ChanB, kPWM_InvertState);
  //writeLog(F("Set Sm42 ChanB to inverted"));

  if(Sm40.setPwmFrequency(config.Pwm.Tm4.Sm40.PwmFrequency, false, true)) {
    sprintf(strBuf, "Set SM40 PWM freq. to %luHz", config.Pwm.Tm4.Sm40.PwmFrequency);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }
  else {
    sprintf(strBuf, "Failed to set SM40 PWM freq. to %luHz", config.Pwm.Tm4.Sm40.PwmFrequency);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }

  Sm40.setupDutyCycle(ChanA, config.Pwm.Tm4.Sm40.ChannelA.DutyCycle);

  sprintf(strBuf, "Setting SM40 ChanA duty cycle to %u", config.Pwm.Tm4.Sm40.ChannelA.DutyCycle);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));

  if(Sm41.setPwmFrequency(config.Pwm.Tm4.Sm41.PwmFrequency, false, true)) {
    sprintf(strBuf, "Set SM41 PWM freq. to %luHz", config.Pwm.Tm4.Sm41.PwmFrequency);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }
  else {
    sprintf(strBuf, "Failed to set SM41 PWM freq. to %luHz", config.Pwm.Tm4.Sm41.PwmFrequency);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }

  Sm41.setupDutyCycle(ChanA, config.Pwm.Tm4.Sm41.ChannelA.DutyCycle);

  sprintf(strBuf, "Setting SM41 ChanA duty cycle to %u", config.Pwm.Tm4.Sm41.ChannelA.DutyCycle);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));
/*
  if(Sm42.setPwmFrequency(config.Pwm.Tm4.Sm42.PwmFrequency, false, true)) {
    sprintf(strBuf, "Set SM42 PWM freq. to %luHz", config.Pwm.Tm4.Sm42.PwmFrequency);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }
  else {
    sprintf(strBuf, "Failed to set SM42 PWM freq. to %luHz", config.Pwm.Tm4.Sm42.PwmFrequency);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }

  Sm42.setupDutyCycle(ChanA, config.Pwm.Tm4.Sm42.ChannelA.DutyCycle);

  sprintf(strBuf, "Setting SM42 ChanA duty cycle to %u", config.Pwm.Tm4.Sm42.ChannelA.DutyCycle);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));

  Sm42.setupDutyCycle(ChanB, config.Pwm.Tm4.Sm42.ChannelB.DutyCycle);

  sprintf(strBuf, "Setting SM42 ChanB duty cycle to %u", config.Pwm.Tm4.Sm42.ChannelB.DutyCycle);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));
*/
  if(config.Pwm.Tm4.Sm41.ChannelA.PhaseShift != 0) {
    Sm41.setupDutyCycle(ChanA, FIFTY_PERCENT_DUTY);

    sprintf(strBuf, "SM41 ChanA phase shift was %u, setting duty cycle to %u", config.Pwm.Tm4.Sm41.ChannelA.PhaseShift, FIFTY_PERCENT_DUTY);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }
  else {
    Sm41.setupDutyCycle(ChanA, config.Pwm.Tm4.Sm41.ChannelA.DutyCycle);

    sprintf(strBuf, "Setting SM41 ChanA duty cycle to %u", config.Pwm.Tm4.Sm41.ChannelA.DutyCycle);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }
/*
  if(config.Pwm.Tm4.Sm42.ChannelA.PhaseShift != 0) {
    Sm42.setupDutyCycle(ChanA, FIFTY_PERCENT_DUTY);

    sprintf(strBuf, "SM42 ChanA phase shift was %u, setting duty cycle to %u", config.Pwm.Tm4.Sm42.ChannelA.PhaseShift, FIFTY_PERCENT_DUTY);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }
  else {
    Sm42.setupDutyCycle(ChanA, config.Pwm.Tm4.Sm42.ChannelA.DutyCycle);

    sprintf(strBuf, "Setting SM42 ChanA duty cycle to %u", config.Pwm.Tm4.Sm42.ChannelA.DutyCycle);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }

  if(config.Pwm.Tm4.Sm42.ChannelB.PhaseShift != 0) {
    Sm42.setupDutyCycle(ChanB, FIFTY_PERCENT_DUTY);

    sprintf(strBuf, "SM42 ChanB phase shift was %u, setting duty cycle to %u", config.Pwm.Tm4.Sm42.ChannelB.PhaseShift, FIFTY_PERCENT_DUTY);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }
  else {
    Sm42.setupDutyCycle(ChanB, config.Pwm.Tm4.Sm42.ChannelB.DutyCycle);

    sprintf(strBuf, "Setting SM42 ChanB duty cycle to %u", config.Pwm.Tm4.Sm42.ChannelB.DutyCycle);
    writeLog(strBuf);
    memset(strBuf, 0, sizeof(strBuf));
  }
*/
  // EXPERIMENTAL
  // Multi-phase PWM

  uint16_t range = (F_CPU_ACTUAL / config.Pwm.Tm4.Sm42.PwmFrequency) / 4; // 600MHz/1MHz = 600;
  uint16_t dutycycle1 = (range * 20)/100; /* 20% duty cycle */
  uint16_t halfRange = range/2;
  uint16_t thirdRange = range/3;

  Sm42.setInitValue(-halfRange); /* Set Initial count register as -300, 600MHz/1MHz/2 = 300 */
  Sm42.setVal0Value(0x00U); /* Set Value 0 register as 0, middle point of PWM period */
  Sm42.setVal1Value(halfRange-1); /* Set Value 1 register as 299, 600MHz/1MHz/2-1 = 299 */
  Sm42.setVal2Value(0x00U); /* Set Value 2 register as 0 */
  Sm42.setVal3Value(0x00U); /* Set Value 3 register as 0 to generate low level for PWM1A, and high level for PWM1B */
  Sm42.setVal4Value(0x00U);
  Sm42.setVal5Value(0x00U);
  Sm42.setupDeadtime(0);

  sprintf(strBuf, "SM42 duty cycle was %u, range was %u, half range was %u, third range was %u", dutycycle1, range, halfRange, thirdRange);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));

  Sm42.disableOutput(ChanA);
  Sm42.disableOutput(ChanB);

  // Chan A
  //Sm42.setVal2Value(-thirdRange-(dutycycle1/2)); // -200-(120/2) = -260
  //Sm42.setVal3Value(-thirdRange+(dutycycle1/2-1)); // -200+(120/2-1) = -141
  //SM42 duty cycle was 1500, range was 7500, half range was 3750, third range was 2500
  Sm42.setVal2Value(-3750);
  Sm42.setVal3Value(-3000);

  // Chan B
  //Sm42.setVal4Value(-(dutycycle1/2)); //-(120/2) = -60
  //Sm42.setVal5Value((dutycycle1/2-1)); // (120/2-1)) = 59
  Sm42.setVal4Value(-3050);
  Sm42.setVal5Value(3700);

  writeLog (F("-----#3 Printing SM42 register values"));
  Sm42.printRegs();

  uint32_t pwmFrequency = Sm40.pwmFrequency();
  uint32_t pwmMode = Sm40.pwmMode();
  uint16_t deadtimeSettingChanA = Sm40.deadtimeSetting(ChanA);
  uint16_t dutyCycleSettingChanA = Sm40.dutyCycleSetting(ChanA);
  const char *prescale = prescaleStr[Sm40.prescaler()];
  sprintf(strBuf, "Configured PWM 4.0 %luHz %s md:%lu dtA:%hu dcA:%hhu%%", pwmFrequency, prescale, pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));

  pwmFrequency = Sm41.pwmFrequency();
  pwmMode = Sm41.pwmMode();
  deadtimeSettingChanA = Sm41.deadtimeSetting(ChanA);
  dutyCycleSettingChanA = Sm41.dutyCycleSetting(ChanA);
  prescale = prescaleStr[Sm41.prescaler()];
  sprintf(strBuf, "Configured PWM 4.1 %luHz %s md:%lu dtA:%hu dcA:%hhu%%", pwmFrequency, prescale, pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));

  pwmFrequency = Sm42.pwmFrequency();
  pwmMode = Sm42.pwmMode();
  deadtimeSettingChanA = Sm42.deadtimeSetting(ChanA);
  const uint16_t deadtimeSettingChanB = Sm42.deadtimeSetting(ChanB);
  dutyCycleSettingChanA = Sm42.dutyCycleSetting(ChanA);
  const uint16_t dutyCycleSettingChanB = Sm42.dutyCycleSetting(ChanB);
  prescale = prescaleStr[Sm42.prescaler()];
  sprintf(strBuf, "Configured PWM 4.2 %luHz %s md:%lu dtA:%hu dcA:%hhu%% dtB:%hu dcB:%hhu%%", pwmFrequency, prescale, pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA, deadtimeSettingChanB, dutyCycleSettingChanB);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));

  writeLog (F("-----#4 Printing SM42 register values"));
  Sm42.printRegs();

  Tm4.setPwmLdok(true);
  Tm4.enable();

  writeLog (F("-----#5 Printing SM42 register values"));
  Sm42.printRegs();

  if (!Sm40.begin()) {
    writeLog (F("Failed to start SM40"));
    exit (EXIT_FAILURE);
  }

  if (!Sm41.begin()) {
    writeLog (F("Failed to start SM41"));
    exit (EXIT_FAILURE);
  }

  Sm42.setPwmLdok(true);

  if (!Sm42.begin(true, true, false)) {
    writeLog (F("Failed to start SM42"));
    exit (EXIT_FAILURE);
  }

  Sm42.enableOutput(ChanA);
  Sm42.enableOutput(ChanB);

  writeLog (F("-----#6 Printing SM42 register values"));
  Sm42.printRegs();

  if(config.Pwm.PrintRegs) {
    writeLog (F("Printing SM40 register values"));
    Sm40.printRegs();
    writeLog (F("Printing SM41 register values"));
    Sm41.printRegs();
    writeLog (F("Printing SM42 register values"));
    Sm42.printRegs();
  }

  writeLog (F("Started TM4"));
}

void attachInterruptVectors() {
  Serial.println(F("Attaching PWM interrupt vectors"));

  if(config.Pwm.Tm2.UseSpwm) {
    attachModule2PwmInterruptVectors();
    enablePwmInterrupts();
  }
}

void attachModule2PwmInterruptVectors() {
  Serial.println(F("Attaching module 2 PWM interrupt vectors"));
  attachInterruptVector (IRQ_FLEXPWM2_0, &IsrOverflowSm20);
}

void enablePwmInterrupts() {
  Serial.println(F("Enabling PWM interrupts"));

  enableModule2PwmInterrupts();
}

void disablePwmInterrupts() {
  Serial.println(F("Disabling PWM interrupts"));

  disableModule2PwmInterrupts();
}

void enableModule2PwmInterrupts() {
  Serial.println(F("Enabling module 2 PWM interrupts"));
  NVIC_ENABLE_IRQ (IRQ_FLEXPWM2_0);
  Sm20.enableInterrupts (kPWM_CompareVal1InterruptEnable);
}

void disableModule2PwmInterrupts() {
  Serial.println(F("Disabling module 2 PWM interrupts"));
  Sm20.disableInterrupts (kPWM_CompareVal1InterruptEnable);
  NVIC_DISABLE_IRQ (IRQ_FLEXPWM2_0);
}

// PWM Waveforms
/*
   ||  1.3A
  _||_____________
   1.3B
     |||||||||||||
  ___|||||||||||||
*/

/*
MOSFET Pairs: Q1 & Q4, Q2 & Q3

2.0A (Q1)
     ||||||      ||||||      |||
  ___||||||______||||||______|||
2.0B (Q2)
  |||      ||||||      ||||||
  |||______||||||______||||||___
2.2A (Q3)
        ||||||      ||||||
  ______||||||______||||||______
2.2B (Q4)
  ||||||      ||||||      ||||||
  ||||||______||||||______||||||

Output
     ||||        ||||        |||
___||||||||____||||||||____|||||
*/

/*
   ||  4.0A
  _||_____________

         ||  4.1A
  _______||_______

   4.2B        ||
  _____________||_
*/

// Interrupt Service Routines
void IsrOverflowSm20() {
  if(!config.Pwm.Tm2.UseSpwm) {
    return;
  }

  float32_t s;

  // The Teensy's LED is lit during the interrupt routine in order to be able to measure its execution time.
  digitalWriteFast (LED_BUILTIN, HIGH);

  // Use pin 13 for 'scope trigger.
  static volatile byte pin13_val = 0 ;
  digitalWriteFast (TriggerPin, pin13_val) ;
  pin13_val = 1 - pin13_val;

  s = roundf ( (MidDutyCycle - 1) * arm_sin_f32 (vSpwmUpdateSpeed * static_cast<float32_t>(++vSample)));

  Tm2.setPwmLdok(false);

  Sm20.updateDutyCycle (static_cast<uint16_t> (MidDutyCycle + s));
  Sm22.updateDutyCycle (static_cast<uint16_t> (MidDutyCycle - s));

  Sm20.clearStatusFlags (kPWM_CompareVal1Flag);

  Tm2.setPwmLdok(true);

  digitalWriteFast (LED_BUILTIN, LOW);

  // Adding a 'asm volatile("dsb");' as last line of the ISR means that it should wait until the cache is written before leaving the ISR.
  asm volatile("dsb");
}

// The i.MXRT1062 uses one config register per two XBAR outputs,
// so this is a helper function to make code more readable.
bool xbarConnect (uint8_t input, uint8_t output) {
  if (input >= 88 || output >= 132) {
    return false;
  }

  char strBuf[150];
  volatile uint16_t *xbar_select_reg = &XBARA1_SEL0 + (output / 2); // 1 reg per 2 outputs
  uint16_t val = *xbar_select_reg;

  sprintf(strBuf, "  Value of XBARA1_SEL%hu (address 0x%" PRIXPTR ") register before writing is 0x%04X", (output / 2), reinterpret_cast<uintptr_t>(&XBARA1_SEL0 + (output / 2)), *xbar_select_reg);
  writeLog(strBuf);
/*
  uint32_t addr1 = (uint32_t)&XBARA1_SEL16;
  uint16_t val1 = XBARA1_SEL16;
  Serial.print("&XBARA1_SEL16=0x"); Serial.println(addr1, HEX);
  Serial.print("XBARA1_SEL16=0x"); Serial.println(val1, HEX);
*/

  if (output & 1) { // high byte or low byte choice.
    val = (val & 0x00FF) | (input << 8);
  }
  else {
    val = (val & 0xFF00) | input;
  }

  sprintf(strBuf, "  Writing value 0x%04X to register XBARA1_SEL%hu (address 0x%" PRIXPTR ")", val, (output / 2), reinterpret_cast<uintptr_t>(&XBARA1_SEL0 + (output / 2)));
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf));

  *xbar_select_reg = val;

  sprintf(strBuf, "  Value of XBARA1_SEL%hu (address 0x%" PRIXPTR ") register after writing is 0x%04X", (output / 2), reinterpret_cast<uintptr_t>(&XBARA1_SEL0 + (output / 2)), *xbar_select_reg);
  writeLog(strBuf);
/*
  addr1 = (uint32_t)&XBARA1_SEL16;
  val1 = XBARA1_SEL16;
  Serial.print("&XBARA1_SEL16=0x"); Serial.println(addr1, HEX);
  Serial.print("XBARA1_SEL16=0x"); Serial.println(val1, HEX);
*/
  return true;
}

void enableXbar() {
  writeLog("Enabling XBAR");

  // Turn on XBAR1 clock for all but stop mode
  CCM_CCGR2 |= CCM_CCGR2_XBAR1(CCM_CCGR_ON);

  //XBARA1_CTRL0 = XBARA_CTRL_STS0 | XBARA_CTRL_EDGE0(3) | XBARA_CTRL_IEN0;

  // Connect trigger to synchronize PWM modules
  // IN is 1 based, i.e. 1-4, whereas OUT is 0 based, i.e. 0-3
  // IN_FLEXPWM2_PWM1 is PWM2.0
  // OUT_FLEXPWM3_EXT_SYNC1 is PWM3.1
  // TRIG0 is Channel A
  // TRIG1 is Channel B

  // PIT0 -> SM1.3
  // XBARA1_IN_FLEXPWM2_PWM1_OUT_TRIG0
  writeLog("XBAR connecting PIT TRIG0 to PWM1.3 EXT_SYNC");
  if(xbarConnect (XBARA1_IN_PIT_TRIGGER0, XBARA1_OUT_FLEXPWM1_PWM3_EXT_SYNC)) {
    writeLog("XBAR connected PIT TRIG0 to PWM1.3 EXT_SYNC");
  }
  else {
    writeLog("ERROR: XBAR did not connect PIT TRIG0 to PWM1.3 EXT_SYNC");
  }

  // PIT0 -> SM2.0
  writeLog("XBAR connecting PIT TRIG0 to PWM2.0 EXT_SYNC");
  if(xbarConnect (XBARA1_IN_PIT_TRIGGER0, XBARA1_OUT_FLEXPWM2_PWM0_EXT_SYNC)) {
    writeLog("XBAR connected PIT TRIG0 to PWM2.0 EXT_SYNC");
  }
  else {
    writeLog("ERROR: XBAR did not connect PIT TRIG0 to PWM2.0 EXT_SYNC");
  }

  // PIT0 -> SM3.1
  writeLog("XBAR connecting PIT TRIG0 to PWM3.1 EXT_SYNC");
  if(xbarConnect (XBARA1_IN_PIT_TRIGGER0, XBARA1_OUT_FLEXPWM3_EXT_SYNC1)) {
    writeLog("XBAR connected PIT TRIG0 to PWM3.1 EXT_SYNC");
  }
  else {
    writeLog("ERROR: XBAR did not connect PIT TRIG0 to PWM3.1 EXT_SYNC");
  }

  // PIT0 -> SM4.0
  writeLog("XBAR connecting PIT TRIG0 to PWM4.0 EXT_SYNC");
  if(xbarConnect (XBARA1_IN_PIT_TRIGGER0, XBARA1_OUT_FLEXPWM4_EXT_SYNC0)) {
    writeLog("XBAR connected PIT TRIG0 to PWM4.0 EXT_SYNC");
  }
  else {
    writeLog("ERROR: XBAR did not connect PIT TRIG0 to PWM4.0 EXT_SYNC");
  }

  // Select alt 3 for EMC_06 (XBAR), rather than original 5 (GPIO)
  //CORE_PIN4_CONFIG = 3; // shorthand for IOMUXC_SW_MUX_CTL_PAD_GPIO_EMC_06 = 3;

  // Turn up drive & speed as very short pulse
  //IOMUXC_SW_PAD_CTL_PAD_GPIO_EMC_06 = IOMUXC_PAD_DSE(7) | IOMUXC_PAD_SPEED(3) | IOMUXC_PAD_SRE;

  writeLog("Enabled XBAR");
}

void printStats() {
  char strBuf[150];

  uint32_t pwmFrequency = Sm13.pwmFrequency();
  uint32_t pwmMode = Sm13.pwmMode();
  uint16_t deadtimeSettingChanA = Sm13.deadtimeSetting(ChanA);
  uint16_t deadtimeSettingChanB = Sm13.deadtimeSetting(ChanB);
  uint16_t dutyCycleSettingChanA = Sm13.dutyCycleSetting(ChanA);
  uint16_t dutyCycleSettingChanB = Sm13.dutyCycleSetting(ChanB);
  const char *prescale = prescaleStr[Sm13.prescaler()];
  sprintf(strBuf, "1.3 %luHz %s md:%lu dtA:%hu dcA:%hhu%% dtB:%hu dcB:%hhu%%", pwmFrequency, prescale, pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA, deadtimeSettingChanB, dutyCycleSettingChanB);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf)); // Clear buffer

  if(config.Pwm.PrintRegs) {
    Sm13.printRegs();
  }

  pwmFrequency = Sm20.pwmFrequency();
  pwmMode = Sm20.pwmMode();
  deadtimeSettingChanA = Sm20.deadtimeSetting(ChanA);
  deadtimeSettingChanB = Sm20.deadtimeSetting(ChanB);
  dutyCycleSettingChanA = Sm20.dutyCycleSetting(ChanA);
  dutyCycleSettingChanB = Sm20.dutyCycleSetting(ChanB);
  prescale = prescaleStr[Sm20.prescaler()];
  sprintf(strBuf, "2.0 %luHz %s md:%lu dtA:%hu dcA:%hhu%% dtB:%hu dcB:%hhu%%", pwmFrequency, prescale, pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA, deadtimeSettingChanB, dutyCycleSettingChanB);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf)); // Clear buffer

  if(config.Pwm.PrintRegs) {
    Sm20.printRegs();
  }

  pwmFrequency = Sm21.pwmFrequency();
  pwmMode = Sm21.pwmMode();
  deadtimeSettingChanA = Sm21.deadtimeSetting(ChanA);
  dutyCycleSettingChanA = Sm21.dutyCycleSetting(ChanA);
  prescale = prescaleStr[Sm21.prescaler()];
  sprintf(strBuf, "2.1 %luHz %s md:%lu dtA:%hu dcA:%hhu%%", pwmFrequency, prescale, pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf)); // Clear buffer

  if(config.Pwm.PrintRegs) {
    Sm21.printRegs();
  }

  pwmFrequency = Sm22.pwmFrequency();
  pwmMode = Sm22.pwmMode();
  deadtimeSettingChanA = Sm22.deadtimeSetting(ChanA);
  deadtimeSettingChanB = Sm22.deadtimeSetting(ChanB);
  dutyCycleSettingChanA = Sm22.dutyCycleSetting(ChanA);
  dutyCycleSettingChanB = Sm22.dutyCycleSetting(ChanB);
  prescale = prescaleStr[Sm22.prescaler()];
  sprintf(strBuf, "2.2 %luHz %s md:%lu dtA:%hu dcA:%hhu%% dtB:%hu dcB:%hhu%%", pwmFrequency, prescale, pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA, deadtimeSettingChanB, dutyCycleSettingChanB);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf)); // Clear buffer

  if(config.Pwm.PrintRegs) {
    Sm22.printRegs();
  }

  pwmFrequency = Sm23.pwmFrequency();
  pwmMode = Sm23.pwmMode();
  deadtimeSettingChanA = Sm23.deadtimeSetting(ChanA);
  deadtimeSettingChanB = Sm23.deadtimeSetting(ChanB);
  dutyCycleSettingChanA = Sm23.dutyCycleSetting(ChanA);
  dutyCycleSettingChanB = Sm23.dutyCycleSetting(ChanB);
  prescale = prescaleStr[Sm23.prescaler()];
  sprintf(strBuf, "2.3 %luHz %s md:%lu dtA:%hu dcA:%hhu%% dtB:%hu dcB:%hhu%%", pwmFrequency, prescale, pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA, deadtimeSettingChanB, dutyCycleSettingChanB);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf)); // Clear buffer

  if(config.Pwm.PrintRegs) {
    Sm23.printRegs();
  }

  pwmFrequency = Sm31.pwmFrequency();
  pwmMode = Sm31.pwmMode();
  deadtimeSettingChanA = Sm31.deadtimeSetting(ChanA);
  deadtimeSettingChanB = Sm31.deadtimeSetting(ChanB);
  dutyCycleSettingChanA = Sm31.dutyCycleSetting(ChanA);
  dutyCycleSettingChanB = Sm31.dutyCycleSetting(ChanB);
  prescale = prescaleStr[Sm31.prescaler()];
  sprintf(strBuf, "3.1 %luHz %s md:%lu dtA:%hu dcA:%hhu%% dtB:%hu dcB:%hhu%%", pwmFrequency, prescale, pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA, deadtimeSettingChanB, dutyCycleSettingChanB);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf)); // Clear buffer

  if(config.Pwm.PrintRegs) {
    Sm31.printRegs();
  }

  pwmFrequency = Sm40.pwmFrequency();
  pwmMode = Sm40.pwmMode();
  deadtimeSettingChanA = Sm40.deadtimeSetting(ChanA);
  dutyCycleSettingChanA = Sm40.dutyCycleSetting(ChanA);
  prescale = prescaleStr[Sm40.prescaler()];
  sprintf(strBuf, "4.0 %luHz %s md:%lu dtA:%hu dcA:%hhu%%", pwmFrequency, prescale, pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf)); // Clear buffer

  if(config.Pwm.PrintRegs) {
    Sm40.printRegs();
  }

  pwmFrequency = Sm41.pwmFrequency();
  pwmMode = Sm41.pwmMode();
  deadtimeSettingChanA = Sm41.deadtimeSetting(ChanA);
  dutyCycleSettingChanA = Sm41.dutyCycleSetting(ChanA);
  prescale = prescaleStr[Sm41.prescaler()];
  sprintf(strBuf, "4.1 %luHz %s md:%lu dtA:%hu dcA:%hhu%%", pwmFrequency, prescale, pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf)); // Clear buffer

  if(config.Pwm.PrintRegs) {
    Sm41.printRegs();
  }

  pwmFrequency = Sm42.pwmFrequency();
  pwmMode = Sm42.pwmMode();
  deadtimeSettingChanA = Sm42.deadtimeSetting(ChanA);
  deadtimeSettingChanB = Sm42.deadtimeSetting(ChanB);
  dutyCycleSettingChanA = Sm42.dutyCycleSetting(ChanA);
  dutyCycleSettingChanB = Sm42.dutyCycleSetting(ChanB);
  prescale = prescaleStr[Sm42.prescaler()];
  sprintf(strBuf, "4.2 %luHz %s md:%lu dtA:%hu dcA:%hhu%% dtB:%hu dcB:%hhu%%", pwmFrequency, prescale, pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA, deadtimeSettingChanB, dutyCycleSettingChanB);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf)); // Clear buffer

  if(config.Pwm.PrintRegs) {
    Sm42.printRegs();
  }

  sprintf(strBuf, "IP: %d.%d.%d.%d", Ethernet.localIP()[0], Ethernet.localIP()[1], Ethernet.localIP()[2], Ethernet.localIP()[3]);
  writeLog(strBuf);
  memset(strBuf, 0, sizeof(strBuf)); // Clear buffer
}

void writeLog(const String &msg) {
  for (int i = 0; i < LogSize - 1; i++) {
    logs[i] = logs[i + 1];
  }

  logs[LogSize - 1] = msg;

  Serial.println(msg);

  display.clearDisplay();

  for (int16_t i = 0; i < LogSize; i++) {
    display.setCursor(10, (i + 1) * 10);
    display.println(logs[i]);
  }

  display.display();
}

#if ( defined(__arm__) && USE_NATIVE_ETHERNET )
// should use uinstd.h to define sbrk but Due causes a conflict
extern "C" char* sbrk(int incr);
#else  // __ARM__
extern char *__brkval;
#endif  // __arm__

int getFreeMemory() {
  char top;
#ifdef __arm__
  return &top - static_cast<char*>(sbrk(0));
#elif defined(CORE_TEENSY) || (ARDUINO > 103 && ARDUINO != 151)
  return &top - __brkval;
#else  // __arm__
  return __brkval ? &top - __brkval : &top - __malloc_heap_start;
#endif  // __arm__
}

void printDigits(int digits){
  // utility for digital clock display: prints preceding colon and leading 0
  Serial.print(F(":"));
  if(digits < 10)
    Serial.print('0');
  Serial.print(digits);
}

/*-------- NTP code ----------*/

constexpr int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  while (ntpUDP.parsePacket() > 0) {}
  // discard any previously received packets
  Serial.println(F("Transmit NTP Request"));
  sendNTPpacket(timeServer);
  const uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    const int size = ntpUDP.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println(F("Receive NTP Response"));
      ntpUDP.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      // convert four bytes starting at location 40 to a long integer
      unsigned long secsSince1900 = static_cast<unsigned long>(packetBuffer[40]) << 24;
      secsSince1900 |= static_cast<unsigned long>(packetBuffer[41]) << 16;
      secsSince1900 |= static_cast<unsigned long>(packetBuffer[42]) << 8;
      secsSince1900 |= static_cast<unsigned long>(packetBuffer[43]);
      Serial.print(F("Seconds since 1 Jan 1900: "));
      Serial.println(secsSince1900);
      const time_t time = secsSince1900 - 2208988800UL;
      Serial.print(F("Seconds since 1 Jan 1970: "));
      Serial.println(time);
      return time;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(const char *host)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  ntpUDP.beginPacket(host, 123); //NTP requests are to port 123
  ntpUDP.write(packetBuffer, NTP_PACKET_SIZE);
  ntpUDP.endPacket();
}

// Configuration
// https://arduinojson.org/v6/assistant
// https://www.objgen.com/json/local/design

// Loads the configuration from a file
void loadConfiguration(const char *filename, MainConfig &config) {
  Serial.println(F("Loading configuration from file"));
  Serial.println(F("Opening existing config file"));

  if (!sd.exists(filename)) {
    Serial.println(F("Config file does not exist, using defaults"));
    return;
  }

  // Open file for reading
  FsFile file = sd.open(filename, FILE_READ);

  if (!file) {
    Serial.println(F("Failed to open config file for reading"));
    return;
  }

  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/v6/assistant to compute the capacity.

  // Deserialize the JSON document
  JsonDocument doc;

  Serial.println(F("Deserializing config from file"));

  DeserializationError error = deserializeJson(doc, file);

  if (error) {
    Serial.println(F("Loading configuration failed, using default config"));
    Serial.println(error.f_str());
    return;
  }

  Serial.println(F("Config deserialized successfully"));

  // Copy values from the JsonDocument to the Config
  JsonObject Config_Pwm = doc[F("Config")][F("Pwm")];
  config.Pwm.PrintRegs = Config_Pwm[F("PrintRegs")] | false;

  JsonObject Config_Pwm_Tm1_Sm13 = Config_Pwm[F("Tm1")][F("Sm13")];
  config.Pwm.Tm1.Sm13.DeadTime = Config_Pwm_Tm1_Sm13[F("DeadTime")] | 50;
  config.Pwm.Tm1.Sm13.PwmFrequency = Config_Pwm_Tm1_Sm13[F("PwmFrequency")] | 1000;
  config.Pwm.Tm1.Sm13.ChannelA.OnPeriodMicroseconds = Config_Pwm_Tm1_Sm13[F("ChannelA")][F("OnPeriodMicroseconds")] | 1000;
  config.Pwm.Tm1.Sm13.ChannelA.DutyCycle = Config_Pwm_Tm1_Sm13[F("ChannelA")][F("DutyCycle")] | 32768;
  config.Pwm.Tm1.Sm13.ChannelA.PhaseShift = Config_Pwm_Tm1_Sm13[F("ChannelA")][F("PhaseShift")] | 0;
  config.Pwm.Tm1.Sm13.ChannelA.Enabled = Config_Pwm_Tm1_Sm13[F("ChannelA")][F("Enabled")] | true;
  config.Pwm.Tm1.Sm13.ChannelB.OnPeriodMicroseconds = Config_Pwm_Tm1_Sm13[F("ChannelB")][F("OnPeriodMicroseconds")] | 1000;
  config.Pwm.Tm1.Sm13.ChannelB.DutyCycle = Config_Pwm_Tm1_Sm13[F("ChannelB")][F("DutyCycle")] | 32768;
  config.Pwm.Tm1.Sm13.ChannelB.PhaseShift = Config_Pwm_Tm1_Sm13[F("ChannelB")][F("PhaseShift")] | 0;
  config.Pwm.Tm1.Sm13.ChannelB.Enabled = Config_Pwm_Tm1_Sm13[F("ChannelB")][F("Enabled")] | true;

  JsonObject Config_Pwm_Tm2 = Config_Pwm[F("Tm2")];
  config.Pwm.Tm2.UseSpwm = Config_Pwm_Tm2[F("UseSpwm")] | false;
  config.Pwm.Tm2.SpwmCarrierFrequency = Config_Pwm_Tm2[F("SpwmCarrierFrequency")] | 20000;
  config.Pwm.Tm2.SpwmModulationFrequency = Config_Pwm_Tm2[F("SpwmModulationFrequency")] | 50;

  JsonObject Config_Pwm_Tm2_Sm20 = Config_Pwm_Tm2[F("Sm20")];
  config.Pwm.Tm2.Sm20.DeadTime = Config_Pwm_Tm2_Sm20[F("DeadTime")] | 50;
  config.Pwm.Tm2.Sm20.PwmFrequency = Config_Pwm_Tm2_Sm20[F("PwmFrequency")] | 1000;
  config.Pwm.Tm2.Sm20.ChannelA.DutyCycle = Config_Pwm_Tm2_Sm20[F("ChannelA")][F("DutyCycle")] | 32768;
  config.Pwm.Tm2.Sm20.ChannelA.PhaseShift = Config_Pwm_Tm2_Sm20[F("ChannelA")][F("PhaseShift")] | 0;
  config.Pwm.Tm2.Sm20.ChannelB.DutyCycle = Config_Pwm_Tm2_Sm20[F("ChannelB")][F("DutyCycle")] | 32768;
  config.Pwm.Tm2.Sm20.ChannelB.PhaseShift = Config_Pwm_Tm2_Sm20[F("ChannelB")][F("PhaseShift")] | 0;

  JsonObject Config_Pwm_Tm2_Sm21 = Config_Pwm_Tm2[F("Sm21")];
  config.Pwm.Tm2.Sm21.DeadTime = Config_Pwm_Tm2_Sm21[F("DeadTime")] | 50;
  config.Pwm.Tm2.Sm21.PwmFrequency = Config_Pwm_Tm2_Sm21[F("PwmFrequency")] | 1000;
  config.Pwm.Tm2.Sm21.ChannelA.DutyCycle = Config_Pwm_Tm2_Sm21[F("ChannelA")][F("DutyCycle")] | 32768;
  config.Pwm.Tm2.Sm21.ChannelA.PhaseShift = Config_Pwm_Tm2_Sm21[F("ChannelA")][F("PhaseShift")] | 0;

  JsonObject Config_Pwm_Tm2_Sm22 = Config_Pwm_Tm2[F("Sm22")];
  config.Pwm.Tm2.Sm22.DeadTime = Config_Pwm_Tm2_Sm22[F("DeadTime")] | 50;
  config.Pwm.Tm2.Sm22.PwmFrequency = Config_Pwm_Tm2_Sm22[F("PwmFrequency")] | 1000;
  config.Pwm.Tm2.Sm22.ChannelA.DutyCycle = Config_Pwm_Tm2_Sm22[F("ChannelA")][F("DutyCycle")] | 32768;
  config.Pwm.Tm2.Sm22.ChannelA.PhaseShift = Config_Pwm_Tm2_Sm22[F("ChannelA")][F("PhaseShift")] | 0;
  config.Pwm.Tm2.Sm22.ChannelB.DutyCycle = Config_Pwm_Tm2_Sm22[F("ChannelB")][F("DutyCycle")] | 32768;
  config.Pwm.Tm2.Sm22.ChannelB.PhaseShift = Config_Pwm_Tm2_Sm22[F("ChannelB")][F("PhaseShift")] | 0;

  JsonObject Config_Pwm_Tm2_Sm23 = Config_Pwm_Tm2[F("Sm23")];
  config.Pwm.Tm2.Sm23.DeadTime = Config_Pwm_Tm2_Sm23[F("DeadTime")] | 50;
  config.Pwm.Tm2.Sm23.PwmFrequency = Config_Pwm_Tm2_Sm23[F("PwmFrequency")] | 1000;
  config.Pwm.Tm2.Sm23.ChannelA.DutyCycle = Config_Pwm_Tm2_Sm23[F("ChannelA")][F("DutyCycle")] | 32768;
  config.Pwm.Tm2.Sm23.ChannelA.PhaseShift = Config_Pwm_Tm2_Sm23[F("ChannelA")][F("PhaseShift")] | 0;
  config.Pwm.Tm2.Sm23.ChannelB.DutyCycle = Config_Pwm_Tm2_Sm23[F("ChannelB")][F("DutyCycle")] | 32768;
  config.Pwm.Tm2.Sm23.ChannelB.PhaseShift = Config_Pwm_Tm2_Sm23[F("ChannelB")][F("PhaseShift")] | 0;

  JsonObject Config_Pwm_Tm3_Sm31 = Config_Pwm[F("Tm3")][F("Sm31")];
  config.Pwm.Tm3.Sm31.DeadTime = Config_Pwm_Tm3_Sm31[F("DeadTime")] | 50;
  config.Pwm.Tm3.Sm31.PwmFrequency = Config_Pwm_Tm3_Sm31[F("PwmFrequency")] | 1000;
  config.Pwm.Tm3.Sm31.ChannelA.DutyCycle = Config_Pwm_Tm3_Sm31[F("ChannelA")][F("DutyCycle")] | 32768;
  config.Pwm.Tm3.Sm31.ChannelA.PhaseShift = Config_Pwm_Tm3_Sm31[F("ChannelA")][F("PhaseShift")] | 0;
  config.Pwm.Tm3.Sm31.ChannelB.DutyCycle = Config_Pwm_Tm3_Sm31[F("ChannelB")][F("DutyCycle")] | 32768;
  config.Pwm.Tm3.Sm31.ChannelB.PhaseShift = Config_Pwm_Tm3_Sm31[F("ChannelB")][F("PhaseShift")] | 0;

  JsonObject Config_Pwm_Tm4 = Config_Pwm[F("Tm4")];
  JsonObject Config_Pwm_Tm4_Sm40 = Config_Pwm_Tm4[F("Sm40")];
  config.Pwm.Tm4.Sm40.DeadTime = Config_Pwm_Tm4_Sm40[F("DeadTime")] | 50;
  config.Pwm.Tm4.Sm40.PwmFrequency = Config_Pwm_Tm4_Sm40[F("PwmFrequency")] | 1000;
  config.Pwm.Tm4.Sm40.ChannelA.DutyCycle = Config_Pwm_Tm4_Sm40[F("ChannelA")][F("DutyCycle")] | 32768;
  config.Pwm.Tm4.Sm40.ChannelA.PhaseShift = Config_Pwm_Tm4_Sm40[F("ChannelA")][F("PhaseShift")] | 0;

  JsonObject Config_Pwm_Tm4_Sm41 = Config_Pwm_Tm4[F("Sm41")];
  config.Pwm.Tm4.Sm41.DeadTime = Config_Pwm_Tm4_Sm41[F("DeadTime")] | 50;
  config.Pwm.Tm4.Sm41.PwmFrequency = Config_Pwm_Tm4_Sm41[F("PwmFrequency")] | 1000;
  config.Pwm.Tm4.Sm41.ChannelA.DutyCycle = Config_Pwm_Tm4_Sm41[F("ChannelA")][F("DutyCycle")] | 32768;
  config.Pwm.Tm4.Sm41.ChannelA.PhaseShift = Config_Pwm_Tm4_Sm41[F("ChannelA")][F("PhaseShift")] | 0;

  JsonObject Config_Pwm_Tm4_Sm42 = Config_Pwm_Tm4[F("Sm42")];
  config.Pwm.Tm4.Sm42.DeadTime = Config_Pwm_Tm4_Sm42[F("DeadTime")] | 50;
  config.Pwm.Tm4.Sm42.PwmFrequency = Config_Pwm_Tm4_Sm42[F("PwmFrequency")] | 1000;
  config.Pwm.Tm4.Sm42.ChannelA.DutyCycle = Config_Pwm_Tm4_Sm42[F("ChannelA")][F("DutyCycle")] | 32768;
  config.Pwm.Tm4.Sm42.ChannelA.PhaseShift = Config_Pwm_Tm4_Sm42[F("ChannelA")][F("PhaseShift")] | 0;
  config.Pwm.Tm4.Sm42.ChannelB.DutyCycle = Config_Pwm_Tm4_Sm42[F("ChannelB")][F("DutyCycle")] | 32768;
  config.Pwm.Tm4.Sm42.ChannelB.PhaseShift = Config_Pwm_Tm4_Sm42[F("ChannelB")][F("PhaseShift")] | 0;

  Serial.println(F("Config loaded successfully"));

  // Close the file (Curiously, File's destructor doesn't close the file)
  file.close();
}

// Saves the configuration to a file
void saveConfiguration(const char *filename, const MainConfig &config) {
  Serial.println(F("Saving configuration to file"));


  // Delete existing file, otherwise the configuration is appended to the file
  if (sd.exists(filename)) {
    Serial.println(F("Deleting existing config file"));
    sd.remove(filename);
    Serial.println(F("Deleted existing config file"));
  }
  else {
    Serial.println(F("Existing config file did not exist"));
  }

  // Open file for writing
  FsFile file = sd.open(filename, FILE_WRITE);
  if (!file) {
    Serial.println(F("Failed to create config file"));
    return;
  }

  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/assistant to compute the capacity.

  // Set the values in the document
  JsonDocument doc;

  JsonObject Config_Pwm = doc[F("Config")][F("Pwm")].to<JsonObject>();
  Config_Pwm[F("PrintRegs")] = config.Pwm.PrintRegs;

  JsonObject Config_Pwm_Tm1_Sm13 = Config_Pwm[F("Tm1")][F("Sm13")].to<JsonObject>();
  Config_Pwm_Tm1_Sm13[F("DeadTime")] = config.Pwm.Tm1.Sm13.DeadTime;
  Config_Pwm_Tm1_Sm13[F("PwmFrequency")] = config.Pwm.Tm1.Sm13.PwmFrequency;

  JsonObject Config_Pwm_Tm1_Sm13_ChannelA = Config_Pwm_Tm1_Sm13[F("ChannelA")].to<JsonObject>();
  Config_Pwm_Tm1_Sm13_ChannelA[F("OnPeriodMicroseconds")] = config.Pwm.Tm1.Sm13.ChannelA.OnPeriodMicroseconds;
  Config_Pwm_Tm1_Sm13_ChannelA[F("DutyCycle")] = config.Pwm.Tm1.Sm13.ChannelA.DutyCycle;
  Config_Pwm_Tm1_Sm13_ChannelA[F("PhaseShift")] = config.Pwm.Tm1.Sm13.ChannelA.PhaseShift;
  Config_Pwm_Tm1_Sm13_ChannelA[F("Enabled")] = config.Pwm.Tm1.Sm13.ChannelA.Enabled;

  JsonObject Config_Pwm_Tm1_Sm13_ChannelB = Config_Pwm_Tm1_Sm13[F("ChannelB")].to<JsonObject>();
  Config_Pwm_Tm1_Sm13_ChannelB[F("OnPeriodMicroseconds")] = config.Pwm.Tm1.Sm13.ChannelB.OnPeriodMicroseconds;
  Config_Pwm_Tm1_Sm13_ChannelB[F("DutyCycle")] = config.Pwm.Tm1.Sm13.ChannelB.DutyCycle;
  Config_Pwm_Tm1_Sm13_ChannelB[F("PhaseShift")] = config.Pwm.Tm1.Sm13.ChannelB.PhaseShift;
  Config_Pwm_Tm1_Sm13_ChannelB[F("Enabled")] = config.Pwm.Tm1.Sm13.ChannelB.Enabled;

  JsonObject Config_Pwm_Tm2 = Config_Pwm[F("Tm2")].to<JsonObject>();
  Config_Pwm_Tm2[F("UseSpwm")] = config.Pwm.Tm2.UseSpwm;
  Config_Pwm_Tm2[F("SpwmCarrierFrequency")] = config.Pwm.Tm2.SpwmCarrierFrequency;
  Config_Pwm_Tm2[F("SpwmModulationFrequency")] = config.Pwm.Tm2.SpwmModulationFrequency;

  JsonObject Config_Pwm_Tm2_Sm20 = Config_Pwm_Tm2[F("Sm20")].to<JsonObject>();
  Config_Pwm_Tm2_Sm20[F("DeadTime")] = config.Pwm.Tm2.Sm20.DeadTime;
  Config_Pwm_Tm2_Sm20[F("PwmFrequency")] = config.Pwm.Tm2.Sm20.PwmFrequency;

  JsonObject Config_Pwm_Tm2_Sm20_ChannelA = Config_Pwm_Tm2_Sm20[F("ChannelA")].to<JsonObject>();
  Config_Pwm_Tm2_Sm20_ChannelA[F("DutyCycle")] = config.Pwm.Tm2.Sm20.ChannelA.DutyCycle;
  Config_Pwm_Tm2_Sm20_ChannelA[F("PhaseShift")] = config.Pwm.Tm2.Sm20.ChannelA.PhaseShift;

  JsonObject Config_Pwm_Tm2_Sm20_ChannelB = Config_Pwm_Tm2_Sm20[F("ChannelB")].to<JsonObject>();
  Config_Pwm_Tm2_Sm20_ChannelB[F("DutyCycle")] = config.Pwm.Tm2.Sm20.ChannelB.DutyCycle;
  Config_Pwm_Tm2_Sm20_ChannelB[F("PhaseShift")] = config.Pwm.Tm2.Sm20.ChannelB.PhaseShift;

  JsonObject Config_Pwm_Tm2_Sm21 = Config_Pwm_Tm2[F("Sm21")].to<JsonObject>();
  Config_Pwm_Tm2_Sm21[F("DeadTime")] = config.Pwm.Tm2.Sm21.DeadTime;
  Config_Pwm_Tm2_Sm21[F("PwmFrequency")] = config.Pwm.Tm2.Sm21.PwmFrequency;

  JsonObject Config_Pwm_Tm2_Sm21_ChannelA = Config_Pwm_Tm2_Sm21[F("ChannelA")].to<JsonObject>();
  Config_Pwm_Tm2_Sm21_ChannelA[F("DutyCycle")] = config.Pwm.Tm2.Sm21.ChannelA.DutyCycle;
  Config_Pwm_Tm2_Sm21_ChannelA[F("PhaseShift")] = config.Pwm.Tm2.Sm21.ChannelA.PhaseShift;

  JsonObject Config_Pwm_Tm2_Sm22 = Config_Pwm_Tm2[F("Sm22")].to<JsonObject>();
  Config_Pwm_Tm2_Sm22[F("DeadTime")] = config.Pwm.Tm2.Sm22.DeadTime;
  Config_Pwm_Tm2_Sm22[F("PwmFrequency")] = config.Pwm.Tm2.Sm22.PwmFrequency;

  JsonObject Config_Pwm_Tm2_Sm22_ChannelA = Config_Pwm_Tm2_Sm22[F("ChannelA")].to<JsonObject>();
  Config_Pwm_Tm2_Sm22_ChannelA[F("DutyCycle")] = config.Pwm.Tm2.Sm22.ChannelA.DutyCycle;
  Config_Pwm_Tm2_Sm22_ChannelA[F("PhaseShift")] = config.Pwm.Tm2.Sm22.ChannelA.PhaseShift;

  JsonObject Config_Pwm_Tm2_Sm22_ChannelB = Config_Pwm_Tm2_Sm22[F("ChannelB")].to<JsonObject>();
  Config_Pwm_Tm2_Sm22_ChannelB[F("DutyCycle")] = config.Pwm.Tm2.Sm22.ChannelB.DutyCycle;
  Config_Pwm_Tm2_Sm22_ChannelB[F("PhaseShift")] = config.Pwm.Tm2.Sm22.ChannelB.PhaseShift;

  JsonObject Config_Pwm_Tm2_Sm23 = Config_Pwm_Tm2[F("Sm23")].to<JsonObject>();
  Config_Pwm_Tm2_Sm23[F("DeadTime")] = config.Pwm.Tm2.Sm23.DeadTime;
  Config_Pwm_Tm2_Sm23[F("PwmFrequency")] = config.Pwm.Tm2.Sm23.PwmFrequency;

  JsonObject Config_Pwm_Tm2_Sm23_ChannelA = Config_Pwm_Tm2_Sm23[F("ChannelA")].to<JsonObject>();
  Config_Pwm_Tm2_Sm23_ChannelA[F("DutyCycle")] = config.Pwm.Tm2.Sm23.ChannelA.DutyCycle;
  Config_Pwm_Tm2_Sm23_ChannelA[F("PhaseShift")] = config.Pwm.Tm2.Sm23.ChannelA.PhaseShift;

  JsonObject Config_Pwm_Tm2_Sm23_ChannelB = Config_Pwm_Tm2_Sm23[F("ChannelB")].to<JsonObject>();
  Config_Pwm_Tm2_Sm23_ChannelB[F("DutyCycle")] = config.Pwm.Tm2.Sm23.ChannelB.DutyCycle;
  Config_Pwm_Tm2_Sm23_ChannelB[F("PhaseShift")] = config.Pwm.Tm2.Sm23.ChannelB.PhaseShift;

  JsonObject Config_Pwm_Tm3_Sm31 = Config_Pwm[F("Tm3")][F("Sm31")].to<JsonObject>();
  Config_Pwm_Tm3_Sm31[F("DeadTime")] = config.Pwm.Tm3.Sm31.DeadTime;
  Config_Pwm_Tm3_Sm31[F("PwmFrequency")] = config.Pwm.Tm3.Sm31.PwmFrequency;

  JsonObject Config_Pwm_Tm3_Sm31_ChannelA = Config_Pwm_Tm3_Sm31[F("ChannelA")].to<JsonObject>();
  Config_Pwm_Tm3_Sm31_ChannelA[F("DutyCycle")] = config.Pwm.Tm3.Sm31.ChannelA.DutyCycle;
  Config_Pwm_Tm3_Sm31_ChannelA[F("PhaseShift")] = config.Pwm.Tm3.Sm31.ChannelA.PhaseShift;

  JsonObject Config_Pwm_Tm3_Sm31_ChannelB = Config_Pwm_Tm3_Sm31[F("ChannelB")].to<JsonObject>();
  Config_Pwm_Tm3_Sm31_ChannelB[F("DutyCycle")] = config.Pwm.Tm3.Sm31.ChannelB.DutyCycle;
  Config_Pwm_Tm3_Sm31_ChannelB[F("PhaseShift")] = config.Pwm.Tm3.Sm31.ChannelB.PhaseShift;

  JsonObject Config_Pwm_Tm4 = Config_Pwm[F("Tm4")].to<JsonObject>();

  JsonObject Config_Pwm_Tm4_Sm40 = Config_Pwm_Tm4[F("Sm40")].to<JsonObject>();
  Config_Pwm_Tm4_Sm40[F("DeadTime")] = config.Pwm.Tm4.Sm40.DeadTime;
  Config_Pwm_Tm4_Sm40[F("PwmFrequency")] = config.Pwm.Tm4.Sm40.PwmFrequency;

  JsonObject Config_Pwm_Tm4_Sm40_ChannelA = Config_Pwm_Tm4_Sm40[F("ChannelA")].to<JsonObject>();
  Config_Pwm_Tm4_Sm40_ChannelA[F("DutyCycle")] = config.Pwm.Tm4.Sm40.ChannelA.DutyCycle;
  Config_Pwm_Tm4_Sm40_ChannelA[F("PhaseShift")] = config.Pwm.Tm4.Sm40.ChannelA.PhaseShift;

  JsonObject Config_Pwm_Tm4_Sm41 = Config_Pwm_Tm4[F("Sm41")].to<JsonObject>();
  Config_Pwm_Tm4_Sm41[F("DeadTime")] = config.Pwm.Tm4.Sm41.DeadTime;
  Config_Pwm_Tm4_Sm41[F("PwmFrequency")] = config.Pwm.Tm4.Sm41.PwmFrequency;

  JsonObject Config_Pwm_Tm4_Sm41_ChannelA = Config_Pwm_Tm4_Sm41[F("ChannelA")].to<JsonObject>();
  Config_Pwm_Tm4_Sm41_ChannelA[F("DutyCycle")] = config.Pwm.Tm4.Sm41.ChannelA.DutyCycle;
  Config_Pwm_Tm4_Sm41_ChannelA[F("PhaseShift")] = config.Pwm.Tm4.Sm41.ChannelA.PhaseShift;

  JsonObject Config_Pwm_Tm4_Sm42 = Config_Pwm_Tm4[F("Sm42")].to<JsonObject>();
  Config_Pwm_Tm4_Sm42[F("DeadTime")] = config.Pwm.Tm4.Sm42.DeadTime;
  Config_Pwm_Tm4_Sm42[F("PwmFrequency")] = config.Pwm.Tm4.Sm42.PwmFrequency;

  JsonObject Config_Pwm_Tm4_Sm42_ChannelA = Config_Pwm_Tm4_Sm42[F("ChannelA")].to<JsonObject>();
  Config_Pwm_Tm4_Sm42_ChannelA[F("DutyCycle")] = config.Pwm.Tm4.Sm42.ChannelA.DutyCycle;
  Config_Pwm_Tm4_Sm42_ChannelA[F("PhaseShift")] = config.Pwm.Tm4.Sm42.ChannelA.PhaseShift;

  JsonObject Config_Pwm_Tm4_Sm42_ChannelB = Config_Pwm_Tm4_Sm42[F("ChannelB")].to<JsonObject>();
  Config_Pwm_Tm4_Sm42_ChannelB[F("DutyCycle")] = config.Pwm.Tm4.Sm42.ChannelB.DutyCycle;
  Config_Pwm_Tm4_Sm42_ChannelB[F("PhaseShift")] = config.Pwm.Tm4.Sm42.ChannelB.PhaseShift;

  Serial.println(F("Writing config file to disk"));

  // Serialize JSON to file
  if (serializeJson(doc, file) == 0) {
    Serial.println(F("Failed to write to file"));
  }

  file.flush();

  Serial.println(F("Config saved successfully"));

  // Close the file
  file.close();
}

// Prints the content of a file to the Serial
void printFile(const char *filename) {
  // Open file for reading
  FsFile file = sd.open(filename);
  if (!file) {
    Serial.println(F("Failed to read file"));
    return;
  }

  // Extract each characters by one by one
  while (file.available()) {
    Serial.print(static_cast<char>(file.read()));
  }
  Serial.println();

  // Close the file
  file.close();
}
