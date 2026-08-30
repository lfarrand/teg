#ifndef OTA_H
#define OTA_H

// OTA firmware update: verify-before-commit staging flow. Once an upload
// starts, the inverter enters a safe state (outputs masked, tasks idled)
// that only a reboot leaves - commit reboots into the new image, abort
// invalidates the staged image and reboots into the current one.

#include <stdint.h>
#include <Arduino.h>

#ifdef TEG_ENABLE_UNSAFE_LAB_OTA
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
#else
inline bool otaInProgress() { return false; }
inline bool otaReleaseEnabled() { return false; }
inline bool otaImageVerified() { return false; }
inline uint32_t otaImageSize() { return 0; }
inline uint32_t otaReceivedBytes() { return 0; }
inline uint32_t otaLineCount() { return 0; }
inline const char *otaLastError() { return ""; }
inline bool otaIngestStream(Stream &, uint32_t, const char **, void (*)()) { return false; }
inline bool otaRequestCommit(uint32_t) { return false; }
inline void otaRequestAbort() {}
inline void otaLoopTask() {}
#endif

#endif
