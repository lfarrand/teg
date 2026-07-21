#ifndef TEG_MAIN_H
#define TEG_MAIN_H

#include <Arduino.h>
#include <SdFat.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <aWOT.h>
#include <LittleFS.h>
#include "config_json.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_ADDRESS 0x3C
#define OLED_RESET (-1)

extern const char* filename;
static constexpr uint32_t kDHCPTimeout = 5000; // boot-time wait only; DHCP keeps retrying in background
extern MainConfig config;
extern SdFs sd;
extern byte mac[];
extern EthernetServer server;
extern Application app;
extern char timeServer[];
extern EthernetUDP ntpUDP;
extern Adafruit_SSD1306 display;
extern EthernetClient influxDbClient;
extern Print *stdPrint;

// Degraded-mode flags: the firmware boots and runs PWM regardless of which
// peripherals are present; consumers must check these before using them.
extern bool sdAvailable;
extern bool displayAvailable;

void configureSdCard();

void loadSettings();

void configureEthernet();

void configureNtp();

// Service the hardware watchdog; long-running handlers (multi-MB waveform
// uploads) must call this periodically
void kickWatchdog();

#endif //TEG_MAIN_H