#include "main.h"
#include <Arduino.h>
#include <SdFat.h>
#include <SPI.h>
#include <LittleFS.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <TeensyID.h>
#include <aWOT.h>
#include <TimeLib.h>
#include "pwm_utils.h"
#include "web_handlers.h"
#include "config_json.h"
#include "memory_utils.h"
#include "capture.h"
#include "thermal.h"
#include "waveform.h"
#include "metrics.h"
#include "meter.h"
#include "acmp.h"
#include "pll.h"
#include "mppt.h"
#include "mqtt.h"
#include "ota.h"
#include "event_log_api.h"
#include "mtp_service.h"
#include "utils.h"

const char* filename = "/settings.cfg";

MainConfig config;

// Teensy identifiers
uint8_t serial[4];
uint8_t uid64[8];

SdFs sd;

byte mac[] = {0x04, 0xE9, 0xE5, 0x14, 0x7C, 0xB0};

EthernetServer server(80);
Application app;

char timeServer[] = "uk.pool.ntp.org";
EthernetUDP ntpUDP;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

EthernetClient influxDbClient;

Print *stdPrint;

bool sdAvailable = false;
bool displayAvailable = false;

// Reset cause (SRC_SRSR, captured at boot then cleared so the next reset
// reports fresh) and the previous CrashReport, kept for /api/status and
// persisted to SD
static uint32_t bootResetStatus = 0;
static char crashText[1024] = "";

const char *resetCauseString() {
  // i.MX RT1062 SRC_SRSR bits (write-1-clear)
  if (bootResetStatus & (1u << 4)) return "watchdog";
  if (bootResetStatus & (1u << 7)) return "watchdog3";
  if (bootResetStatus & (1u << 1)) return "software/lockup";
  if (bootResetStatus & (1u << 8)) return "overtemperature";
  if (bootResetStatus & (1u << 3)) return "external";
  if (bootResetStatus & (1u << 0)) return "power-on";
  return "unknown";
}

const char *crashReportText() {
  return crashText;
}

namespace {
// Print adapter capturing CrashReport into a buffer
class BufferPrint : public Print {
 public:
  BufferPrint(char *buffer, size_t capacity) : buf(buffer), cap(capacity) {}
  size_t write(uint8_t c) override {
    if (len + 1 < cap) {
      buf[len++] = static_cast<char>(c);
      buf[len] = '\0';
      return 1;
    }
    return 0;
  }

 private:
  char *buf;
  size_t cap;
  size_t len = 0;
};
} // namespace

static void captureBootDiagnostics() {
  bootResetStatus = SRC_SRSR;
  SRC_SRSR = bootResetStatus; // write-1-clear for a fresh report next boot

  if (CrashReport) {
    BufferPrint bp(crashText, sizeof(crashText));
    bp.print(CrashReport);
  }

  Serial.print(F("Reset cause: "));
  Serial.println(resetCauseString());
  if (crashText[0] != '\0') {
    Serial.println(F("Previous crash report:"));
    Serial.print(crashText);
  }
}

static void persistCrashReport() {
  if (!sdAvailable || crashText[0] == '\0') {
    return;
  }
  FsFile f = sd.open("/crashlog.txt", O_RDWR | O_CREAT | O_AT_END);
  if (f) {
    f.print("=== reset cause: ");
    f.print(resetCauseString());
    f.println(" ===");
    f.print(crashText);
    f.println();
    f.close();
  }
}

void configureSdCard() {
  sdAvailable = sd.begin(FIFO_SDIO);
  if (!sdAvailable) {
    Serial.println(F("SD init failed - running on default config, saves disabled"));
  }
}

void loadSettings() {
  if (sdAvailable) {
    loadConfiguration(filename);
  }
}

void configureEthernet() {
  // Start DHCP without blocking the boot: wait briefly for an address (so the
  // IP can appear in the boot log), then continue regardless -
  // networkHousekeeping() reports when the lease eventually arrives, and
  // QNEthernet's DHCP client keeps retrying in the background.
  Ethernet.begin(mac);
  if (!Ethernet.waitForLocalIP(kDHCPTimeout)) {
    Serial.println(F("No DHCP lease yet - continuing, will report when up"));
  }
  server.begin();

  // mDNS: reachable as http://teg.local without knowing the DHCP address
  if (MDNS.begin("teg")) {
    MDNS.addService("_http", "_tcp", 80);
  }
}

static void networkHousekeeping() {
  static bool wasUp = false;
  const bool up = static_cast<uint32_t>(Ethernet.localIP()) != 0;
  if (up && !wasUp) {
    char buf[48];
    snprintf(buf, sizeof(buf), "Network up: %d.%d.%d.%d",
             Ethernet.localIP()[0], Ethernet.localIP()[1],
             Ethernet.localIP()[2], Ethernet.localIP()[3]);
    writeLog(buf);
  } else if (!up && wasUp) {
    writeLogLevel(EventWarn, "Network down");
  }
  wasUp = up;
}

// WDOG1: if loop() stops being serviced for ~8 seconds (firmware hang), the
// hardware resets the chip into the known-safe boot path instead of leaving
// the PWM free-running with stale state.
static void enableWatchdog() {
  WDOG1_WMCR = 0; // no power-down counter
  WDOG1_WCR = WDOG_WCR_WT(15) | WDOG_WCR_WDE | WDOG_WCR_SRS | WDOG_WCR_WDA; // 8s timeout
}

void kickWatchdog() {
  WDOG1_WSR = 0x5555;
  WDOG1_WSR = 0xAAAA;
}

void configureNtp() {
  // Deliberately NO setSyncProvider: TimeLib would re-invoke the provider
  // from now(), turning any call to it (including a log timestamp) into a
  // blocking DNS + NTP round trip. ntpTask() drives synchronization from
  // loop() instead and pushes the result into TimeLib and the RTC.
  ntpUDP.begin(8888);
}

void setup() {
  Serial.begin(9600);

  captureBootDiagnostics();

  stdPrint = &Serial;

  pinMode(8, INPUT);
  pinMode(7, INPUT);
  pinMode(4, INPUT);
  pinMode(33, INPUT);
  pinMode(5, INPUT);
  pinMode(6, INPUT);
  pinMode(9, INPUT);
  pinMode(36, INPUT);
  pinMode(37, INPUT);
  pinMode(29, INPUT);
  pinMode(28, INPUT);
  pinMode(22, INPUT);
  pinMode(23, INPUT);
  pinMode(2, INPUT);
  pinMode(3, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(TriggerPin, OUTPUT);

  displayAvailable = display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
  if (displayAvailable) {
    display.display();
  } else {
    Serial.println(F("SSD1306 init failed - continuing without display"));
  }

  teensySN(serial);
  teensyUID64(uid64);

  delay(2000);

  Serial.printf("USB Serial: %u \n", teensyUsbSN());
  Serial.printf("Teensy Serial: %02X-%02X-%02X-%02X \n", serial[0], serial[1], serial[2], serial[3]);
  Serial.printf("UID 64-bit: %02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n", uid64[0], uid64[1], uid64[2], uid64[3],
                uid64[4], uid64[5], uid64[6], uid64[7]);

  eventLogBegin(); // seed the clock from the RTC before anything logs

  configureSdCard();

  persistCrashReport();

  loadSettings();

  waveformLoadFromSd();

  initMemory();

  configureEthernet();

  configureWebServer();

  configureNtp();

  configurePwm();

  attachInterruptVectors();

  if (config.Pwm.SyncPwm) {
    enableXbar();
  }

  configureFaultProtection();

  acmpConfigure(); // after configurePwm so submodule init can't disturb the armed fault channel

  captureConfigure();

  pllConfigure();

  mpptConfigure();

  thermalConfigure();

  printStats();

  reportMemoryUsage();

  enableWatchdog();

  // LAST: MTP.begin() arms an interval timer whose handler queries the
  // filesystem from interrupt context, so nothing else may touch the card
  // after this point in setup()
  mtpBegin();

  digitalWriteFast(LED_BUILTIN, HIGH);
}

void loop() {
  kickWatchdog();

  mtpTask(); // first: a host transfer holds the pass, and it feeds the watchdog

  processWebServer();

  networkHousekeeping();

  ntpTask();

  // During an OTA the outputs are masked and flash contents are in flux:
  // idle every control/telemetry task in one place (they would only publish
  // garbage); the web server stays up for the commit/abort endpoints
  if (!otaInProgress()) {
    runFeedbackLoop();

    pllTask();

    thermalTask();

    waveformStreamTask();

    meterTask();

    mpptTask();

    acmpTask();

    metricsTask();

    mqttTask();
  }

  otaLoopTask(); // deferred commit/abort, after the HTTP response flushed

  static bool faultReported = false;
  if (vFaultTripped && !faultReported) {
    writeLogLevel(EventError, "FAULT TRIP: all PWM outputs disabled (save settings to clear)");
    faultReported = true;
  } else if (!vFaultTripped) {
    faultReported = false;
  }

  if (configSaveNeeded) {
    configSaveNeeded = false;
    saveConfiguration(filename);
    if (config.Pwm.Verbose) {
      printFile(filename);
    }
  }

  static unsigned long lastRamCheck = 0;
  if (millis() - lastRamCheck >= 5000) {
    reportMemoryUsage();
    lastRamCheck = millis();
  }

  flushDisplay();

  delay(1);
}