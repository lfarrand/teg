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

char influxDbServerAddress[] = "ub-1.lan";
int influxDbPort = 8086;
EthernetClient influxDbClient;

Print *stdPrint;

void configureSdCard() {
  if (!sd.begin(FIFO_SDIO)) {
    Serial.println(F("SD card initialization failed!"));
    for (;;);
  }
}

void loadSettings() {
  loadConfiguration(filename);
}

void configureEthernet() {
  if (Ethernet.begin(mac, kDHCPTimeout) == 0) {
    Serial.println(F("Failed to configure Ethernet using DHCP"));
    for (;;);
  }
  server.begin();
}

void configureNtp() {
  ntpUDP.begin(8888);
  setSyncProvider(getNtpTime);
  setSyncInterval(300);
}

void setup() {
  Serial.begin(9600);

  if (Serial && CrashReport) {
    Serial.println("\n" __FILE__ " " __DATE__ " " __TIME__);
    Serial.print(CrashReport);
  }

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

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  display.display();

  teensySN(serial);
  teensyUID64(uid64);

  delay(2000);

  Serial.printf("USB Serial: %u \n", teensyUsbSN());
  Serial.printf("Teensy Serial: %02X-%02X-%02X-%02X \n", serial[0], serial[1], serial[2], serial[3]);
  Serial.printf("UID 64-bit: %02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n", uid64[0], uid64[1], uid64[2], uid64[3],
                uid64[4], uid64[5], uid64[6], uid64[7]);

  configureSdCard();

  loadSettings();

  initMemory();

  configureEthernet();

  configureWebServer();

  configureNtp();

  configurePwm();

  attachInterruptVectors();

  if (config.Pwm.SyncPwm) {
    enableXbar();
  }

  printStats();

  reportMemoryUsage();

  digitalWriteFast(LED_BUILTIN, HIGH);
}

void loop() {
  processWebServer();

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