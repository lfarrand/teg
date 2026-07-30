#include "memory_utils.h"
#include "utils.h"
#include <LittleFS.h>

LittleFS_QSPIFlash flashFS;
static bool flashFSMounted = false;
EXTMEM uint8_t buf[1024];

bool flashFSAvailable() {
  return flashFSMounted;
}

bool testPsram() {
  memset(buf, 0xAA, sizeof(buf));
  return (buf[0] == 0xAA && buf[1023] == 0xAA);
}

void initMemory() {
  if (testPsram()) {
    writeLog("PSRAM detected (8MB)");
  } else {
    writeLog("PSRAM not detected!");
  }
  flashFSMounted = flashFS.begin();
  if (flashFSMounted) {
    writeLog("Flash detected (16MB)");
  } else {
    writeLog("Flash init failed");
  }
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
  Serial.println(buf);

  // Compact form for the dedicated OLED status line (21 chars max at size-1 font).
  // The minimum is the number worth watching, so that is what gets the space.
  snprintf(buf, sizeof(buf), "Stk%dk OCRAM %dk", stackMin / 1024, ocramFree / 1024);
  setStatusLine(buf);
}