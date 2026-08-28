#ifndef MTP_SERVICE_H
#define MTP_SERVICE_H

// USB MTP file access to the SD card and QSPI flash.

#include <stdint.h>

void mtpBegin(); // call LAST in setup(); no-op unless Mtp.Enabled
void mtpTask();  // call from loop()

bool mtpEnabled();
bool mtpPaused();          // held off because the inverter is busy
uint32_t mtpPausedCount(); // how many passes have been withheld
// False while MTP.begin() has armed the 20 Hz FS-from-IRQ timer and the first
// MTP.loop() has not torn it down.
bool mtpAllowsPwmRelease();

#endif
