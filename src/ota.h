#ifndef OTA_H
#define OTA_H

// OTA firmware update: verify-before-commit staging flow. Once an upload
// starts, the inverter enters a safe state (outputs masked, tasks idled)
// that only a reboot leaves - commit reboots into the new image, abort
// invalidates the staged image and reboots into the current one.

#include <stdint.h>
#include <Arduino.h>

bool otaInProgress();     // safe state entered; cleared only by reset
bool otaReleaseEnabled(); // false in production unless unsafe lab flag is explicit
bool otaImageVerified();
uint32_t otaImageSize();
uint32_t otaReceivedBytes();
uint32_t otaLineCount();
const char *otaLastError(); // "" when none

bool otaIngestStream(Stream &in, uint32_t expectedBytes, const char **err,
                     void (*progress)());
bool otaRequestCommit(uint32_t confirmSize); // false unless verified + size echo matches
void otaRequestAbort();
void otaLoopTask(); // executes deferred commit/abort AFTER the HTTP response flushed

#endif
