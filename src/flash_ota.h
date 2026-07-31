#ifndef FLASH_OTA_H
#define FLASH_OTA_H

// Flash primitives for OTA staging and commit. See flash_ota.cpp for the
// FlasherX lineage and attribution.

#include <stdint.h>

void otaFlashEraseSectorHW(uint32_t addr); // 4K-aligned
void otaFlashWriteHW(uint32_t addr, const uint8_t *data, uint32_t len);

// Copy the staged image down over the running firmware, verify it by
// read-back CRC (retrying the copy on mismatch while the source is still
// intact), wipe everything above it up to the EEPROM reserve, and reset.
// NEVER RETURNS. Kicks WDOG1 per sector - the copy takes seconds to tens of
// seconds. Masks the remaining peripheral interrupts first: between flash
// ops the core primitives re-enable IRQs, and any ISR that fetches
// FLASHMEM code or reads PROGMEM data after the low sectors are erased
// would fault with no recovery.
// Copies the staged image down over the base image and resets. Returns only on
// FAILURE - if the read-back CRC never matched after every retry, it leaves the
// best-effort image in place and returns false rather than wiping and resetting into
// a blank base image, which would brick the board. On success it does not return.
bool otaFlashCommit(uint32_t imageSize, uint32_t expectedCrc);

#endif
