#include "memory_utils.h"
#include "utils.h"
#include <LittleFS.h>

LittleFS_QSPIFlash flashFS;
EXTMEM uint8_t buf[1024];

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
  if (flashFS.begin()) {
    writeLog("Flash detected (16MB)");
  } else {
    writeLog("Flash init failed");
  }
}

void reportMemoryUsage() {
  char buf[100];
  snprintf(buf, sizeof(buf), "DTCM Free: %d | OCRAM Free: %d | PSRAM: %s",
           getFreeMemory(), freeram(), testPsram() ? "OK" : "Fail");
  writeLog(buf);
}