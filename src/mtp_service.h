#ifndef MTP_SERVICE_H
#define MTP_SERVICE_H

// USB MTP file access to the SD card and QSPI flash.

#include <stdint.h>

void mtpBegin(); // call LAST in setup(); no-op unless Mtp.Enabled
void mtpTask();  // call from loop()

bool mtpEnabled();
bool mtpPaused();          // held off because the inverter is busy
uint32_t mtpPausedCount(); // how many passes have been withheld

#endif
