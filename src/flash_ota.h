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
[[noreturn]] void otaFlashCommit(uint32_t imageSize, uint32_t expectedCrc);

#endif
