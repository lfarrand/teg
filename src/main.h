#ifndef TEG_MAIN_H
#define TEG_MAIN_H

#include <Arduino.h>
#include <SdFat.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <aWOT.h>
#include <LittleFS.h>
// The Ethernet types below used to arrive transitively, via a QNEthernet include inside
// aWOT.h. aWOT no longer depends on QNEthernet (it only ever needed it for writeFully(),
// now replaced by a bounded equivalent), so the firmware declares the dependency it
// actually has. Nothing here changes which stack is used - QNEthernet is still the one.
#include <QNEthernet.h>
using namespace qindesign::network;
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

// Run the control tasks that must not stall, from inside a long-running request
// handler. Kicks the watchdog too, so it replaces a bare kickWatchdog() call in any
// loop that holds the pass for more than a few hundred milliseconds.
//
// Deliberately excludes everything that touches the network or USB: calling those
// from inside a handler would re-enter the stack that is mid-response. See the
// definition in main.cpp for what is in and out, and why.
void serviceControlTasks();

// Boot diagnostics for /api/status: why the last reset happened, and the
// previous CrashReport text ("" if none)
const char *resetCauseString();
const char *crashReportText();

#endif //TEG_MAIN_H