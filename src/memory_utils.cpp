#include "memory_utils.h"
#include "config_json.h"
#include "utils.h"
#include <LittleFS.h>

extern MainConfig config;

LittleFS_QSPIFlash flashFS;
static bool flashFSMounted = false;
static bool psramReady = false;
EXTMEM uint8_t buf[1024];

extern "C" uint8_t external_psram_size;
extern void kickWatchdog();

bool flashFSAvailable() {
  return flashFSMounted;
}

static bool fillAndCheckPsram(uint8_t pattern) {
  volatile uint8_t *const vp = buf;
  memset(buf, pattern, sizeof(buf));
  for (size_t i = 0; i < sizeof(buf); ++i) {
    if (vp[i] != pattern) {
      return false;
    }
  }
  return true;
}

bool testPsram() {
  if (external_psram_size < 8) {
    return false;
  }
  if (!fillAndCheckPsram(0xAA)) {
    return false;
  }
  kickWatchdog();
  return fillAndCheckPsram(0x55);
}

bool psramAvailable() {
  return psramReady;
}

bool initMemory() {
  psramReady = testPsram();
  if (psramReady) {
    writeLog("PSRAM detected (8MB)");
  } else {
    writeLogLevel(EventError, "PSRAM missing/undersized; PWM output permanently inhibited");
  }
  flashFSMounted = flashFS.begin();
  if (flashFSMounted) {
    writeLog("Flash detected (16MB)");
  } else {
    writeLog("Flash init failed");
  }
  return psramReady;
}

void reportMemoryUsage() {
  // Sampling the low-water mark also refreshes it, so call it before reading the
  // instantaneous figure - the two then agree about this moment.
  int stackMin = getStackLowWater();
  int dtcmFree = getFreeMemory();
  int ocramFree = freeram();

  extern volatile uint32_t vIsrCycles; // last SPWM ISR duration, 600MHz DWT cycles

  char buf[96];
  snprintf(buf, sizeof(buf), "Stack: %d now / %d min | OCRAM Free: %d | SPWM ISR: %lu cycles",
           dtcmFree, stackMin, ocramFree, vIsrCycles);
  if (config.Pwm.Verbose) {
    Serial.println(buf);
  }

  // Compact form for the dedicated OLED status line (21 chars max at size-1 font).
  // The minimum is the number worth watching, so that is what gets the space.
  snprintf(buf, sizeof(buf), "Stk%dk OCRAM %dk", stackMin / 1024, ocramFree / 1024);
  setStatusLine(buf);
}
