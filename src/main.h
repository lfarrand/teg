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
static constexpr uint32_t kDHCPTimeout = 15000;
extern MainConfig config;
extern SdFs sd;
extern byte mac[];
extern EthernetServer server;
extern Application app;
extern char timeServer[];
extern EthernetUDP ntpUDP;
extern Adafruit_SSD1306 display;
extern LittleFS_QSPIFlash flashFS;
extern char influxDbServerAddress[];
extern int influxDbPort;
extern EthernetClient influxDbClient;
extern Print *stdPrint;

void configureSdCard();

void loadSettings();

void configureEthernet();

void configureNtp();

#endif //TEG_MAIN_H