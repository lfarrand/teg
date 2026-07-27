// OTA flash primitives for Teensy 4.1 (i.MX RT1062, 8MB W25Q64 QSPI).
//
// Lineage: the staging/copy-down approach follows FlasherX
// (https://github.com/joepasquariello/FlasherX) - original by Niels A.
// Moseley; OTA modifications by Jon Zeeff and Deb Hollenback; Paul
// Stoffregen's Teensy 4.x flash routines; Frank Boesing's T3.x flash
// routines adapted by Joe Pasquariello. That code is in the public domain;
// names retained per the authors' requests. This is a T4.1-only rewrite of
// the pieces we need, on top of the Teensy core's eepromemu flash
// primitives, with two behavioral changes: WDOG1 is serviced inside the
// copy loops (stock flash_move never feeds it, and a ~500KB copy can take
// 10-60s against our 8s watchdog - a mid-copy watchdog reset corrupts the
// base image), and page-sized programming (256B) instead of 4-byte words
// for ~30x fewer IRQ-masked windows.
//
// Safety model: each eepromemu op masks interrupts for its own duration and
// stalls AHB flash fetch; all Teensy code executes from ITCM so execution
// continues, but flash-resident data (PROGMEM/FLASHMEM) is unreadable
// during ops. otaFlashCommit destroys the boot headers in its first sector
// erase - from that instant until the reset, a power loss leaves the board
// recoverable ONLY via the USB bootloader pushbutton. This is the inherent
// FlasherX-pattern window; verify-before-commit narrows it to the copy
// itself but cannot remove it (no A/B boot on the RT1062).

#include "flash_ota.h"
#include "ota_ingest.h" // buffer layout constants
#include "ota_crc.h"
#include <Arduino.h>

extern "C" {
// cores/teensy4/eeprom.c - each op: __disable_irq, FlexSPI IP command,
// D-cache purge, busy-wait on the flash status register, __enable_irq
void eepromemu_flash_write(void *addr, const void *data, uint32_t len);
void eepromemu_flash_erase_sector(void *addr);
}

void otaFlashEraseSectorHW(uint32_t addr) {
  eepromemu_flash_erase_sector(reinterpret_cast<void *>(addr));
}

// Split at 256-byte page boundaries (a program op must not cross a page);
// data must be in RAM - an AHB flash read during the IP program deadlocks
// the FlexSPI
void otaFlashWriteHW(uint32_t addr, const uint8_t *data, uint32_t len) {
  while (len > 0) {
    const uint32_t space = 256u - (addr & 255u);
    const uint32_t n = len < space ? len : space;
    eepromemu_flash_write(reinterpret_cast<void *>(addr), data, n);
    addr += n;
    data += n;
    len -= n;
  }
}

FASTRUN static bool sectorNotErased(uint32_t addr) {
  const uint32_t *p = reinterpret_cast<const uint32_t *>(addr);
  for (uint32_t i = 0; i < OtaSectorSize / 4; i++) {
    if (p[i] != 0xFFFFFFFFu) {
      return true;
    }
  }
  return false;
}

// Plain register writes - safe from ITCM with the FlexSPI busy
FASTRUN static void wdogKick() {
  WDOG1_WSR = 0x5555;
  WDOG1_WSR = 0xAAAA;
}

// CRC over a flash range, chunked through RAM with watchdog kicks
FASTRUN static uint32_t flashCrc(uint32_t addr, uint32_t len) {
  uint8_t buf[256];
  uint32_t crc = 0xFFFFFFFFu;
  for (uint32_t off = 0; off < len; off += sizeof(buf)) {
    wdogKick();
    const uint32_t n = len - off < sizeof(buf) ? len - off : sizeof(buf);
    const uint8_t *s = reinterpret_cast<const uint8_t *>(addr + off);
    for (uint32_t i = 0; i < n; i++) {
      buf[i] = s[i];
    }
    crc = otaCrc32Update(crc, buf, n);
  }
  return crc ^ 0xFFFFFFFFu;
}

FASTRUN void otaFlashCommit(uint32_t imageSize, uint32_t expectedCrc) {
  const uint32_t dst = OtaFlashBase;
  const uint32_t src = OtaBufferBase;
  uint8_t page[256];

  // The core's flash primitives re-enable interrupts when each op finishes,
  // so IRQs are live between ops with the boot sectors already erased. Any
  // handler that fetches FLASHMEM code or reads PROGMEM would fault
  // unrecoverably - mask everything that could still fire. (systick is a
  // system handler, not NVIC-maskable, but the core's systick_isr is
  // ITCM-resident and touches no flash.)
  NVIC_DISABLE_IRQ(IRQ_ENET);
  NVIC_DISABLE_IRQ(IRQ_USB1);
  NVIC_DISABLE_IRQ(IRQ_USB2);
  NVIC_DISABLE_IRQ(IRQ_GPIO6789);
  NVIC_DISABLE_IRQ(IRQ_FLEXPWM2_0);
  NVIC_DISABLE_IRQ(IRQ_FLEXPWM2_FAULT);

  // Phase 1: erase + program the base image, sector by sector, ascending,
  // then verify by read-back. dst trails src by 4MB so the staged source is
  // never clobbered; that means a failed copy can be retried in full.
  for (int attempt = 0; attempt < 3; attempt++) {
    for (uint32_t off = 0; off < imageSize; off += OtaSectorSize) {
      wdogKick();
      if (sectorNotErased(dst + off)) {
        eepromemu_flash_erase_sector(reinterpret_cast<void *>(dst + off));
      }
      const uint32_t remain = imageSize - off;
      const uint32_t secLen = remain < OtaSectorSize ? remain : OtaSectorSize;
      for (uint32_t p = 0; p < secLen; p += 256) {
        const uint32_t n = secLen - p < 256 ? secLen - p : 256;
        const uint8_t *s = reinterpret_cast<const uint8_t *>(src + off + p);
        for (uint32_t i = 0; i < n; i++) {
          page[i] = s[i]; // AHB read of the source BETWEEN ops, staged to RAM
        }
        eepromemu_flash_write(reinterpret_cast<void *>(dst + off + p), page, n);
      }
    }
    if (flashCrc(dst, imageSize) == expectedCrc) {
      break; // committed image matches what was verified
    }
    // Mismatch: a program/erase op silently failed (the core primitives
    // report nothing). The staged source is intact, so wipe and redo.
    for (uint32_t off = 0; off < imageSize; off += OtaSectorSize) {
      wdogKick();
      eepromemu_flash_erase_sector(reinterpret_cast<void *>(dst + off));
    }
  }

  // Phase 2: wipe everything above the new image up to the EEPROM reserve -
  // the old image's tail and the staging buffer - leaving flash as a fresh
  // TeensyDuino load would. Benign if interrupted: the new image already
  // boots.
  const uint32_t wipeStart = (dst + imageSize + OtaSectorSize - 1) & ~(OtaSectorSize - 1);
  for (uint32_t a = wipeStart; a < 0x607C0000u; a += OtaSectorSize) {
    wdogKick();
    if (sectorNotErased(a)) {
      eepromemu_flash_erase_sector(reinterpret_cast<void *>(a));
    }
  }

  wdogKick();
  SCB_AIRCR = 0x05FA0004; // SYSRESETREQ: full chip reset, boot ROM re-reads flash
  for (;;) {
  }
}
