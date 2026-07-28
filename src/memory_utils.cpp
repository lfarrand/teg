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
  int dtcmFree = getFreeMemory();
  int ocramFree = freeram();

  extern volatile uint32_t vIsrCycles; // last SPWM ISR duration, 600MHz DWT cycles

  char buf[80];
  snprintf(buf, sizeof(buf), "DTCM Free: %d | OCRAM Free: %d | SPWM ISR: %lu cycles",
           dtcmFree, ocramFree, vIsrCycles);
  Serial.println(buf);

  // Compact form for the dedicated OLED status line (21 chars max at size-1 font)
  snprintf(buf, sizeof(buf), "DTCM %dk OCRAM %dk", dtcmFree / 1024, ocramFree / 1024);
  setStatusLine(buf);
}