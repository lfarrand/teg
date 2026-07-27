#ifndef OTA_VERIFY_H
#define OTA_VERIFY_H

// Pre-commit structural verification of a staged Teensy 4.1 firmware image.
// Every check reads back from the STAGED BUFFER (not parse-time state), so
// what gets verified is exactly what the commit will copy. Offsets and
// values are from the i.MX RT1060 RM rev4 (boot chapter) cross-checked
// against cores/teensy4/bootdata.c and the linker script, and validated
// against this project's actual build.
//
// The checks reject: garbage/truncated files, images whose base is not the
// T4.1 flash origin, images too large for the buffer, non-Teensy images
// (no FlexSPI config block / IVT), and - critically - right-family-wrong-
// board builds: a Teensy 4.0 image is structurally identical EXCEPT the
// FlexSPI config's flash-size word (2MB vs 8MB), and would mostly boot,
// making it the most dangerous wrong image. There is no other intrinsic
// board marker in the hex.

#include <stdint.h>
#include <string.h>
#include "ota_ingest.h"

inline uint32_t otaReadU32(const uint8_t *img, uint32_t off) {
  return static_cast<uint32_t>(img[off]) | (static_cast<uint32_t>(img[off + 1]) << 8) |
         (static_cast<uint32_t>(img[off + 2]) << 16) |
         (static_cast<uint32_t>(img[off + 3]) << 24);
}

// img points at the staged image (memory-mapped buffer on target, RAM copy
// in tests). Returns nullptr when the image passes, else the failure reason.
inline const char *otaVerifyImage(const uint8_t *img, uint32_t minAddr, uint32_t maxAddr,
                                  uint64_t bytesWritten, bool eofSeen) {
  if (!eofSeen) {
    return "no end-of-file record";
  }
  if (minAddr != OtaFlashBase) {
    return "image does not start at the flash base";
  }
  const uint32_t imageSize = maxAddr - OtaFlashBase + 1;
  if (imageSize <= 0x2000) {
    return "image too small to contain boot headers";
  }
  if (imageSize > OtaImageMax) {
    return "image exceeds the staging buffer";
  }
  if (bytesWritten != imageSize) {
    return "image has address gaps"; // this toolchain emits dense images
  }
  if (otaReadU32(img, 0x000) != 0x42464346u) { // 'FCFB'
    return "missing FlexSPI config block (not a Teensy image)";
  }
  // sflashA1Size: the only intrinsic board marker. 0x00800000 = 8MB = Teensy
  // 4.1; a Teensy 4.0 build says 0x00200000 here and passes everything else.
  if (otaReadU32(img, 0x050) != 0x00800000u) {
    return "flash size marker is not Teensy 4.1 (wrong board build)";
  }
  // IVT at +0x1000: tag 0xD1, length 0x0020 big-endian, version 0x4x.
  // Teensy emits 0x432000D1 - compare masked, the ROM only checks the tag.
  const uint32_t ivt = otaReadU32(img, 0x1000);
  if ((ivt & 0x00FFFFFFu) != 0x002000D1u || ((ivt >> 24) & 0xF0u) != 0x40u) {
    return "missing image vector table";
  }
  const uint32_t entry = otaReadU32(img, 0x1004);
  if ((entry & 1u) == 0) {
    return "entry point is not Thumb";
  }
  if ((entry & ~1u) < OtaFlashBase || (entry & ~1u) >= OtaFlashBase + imageSize) {
    return "entry point outside the image";
  }
  if (otaReadU32(img, 0x1014) != OtaFlashBase + 0x1000) { // IVT self-pointer
    return "IVT self-pointer mismatch";
  }
  const uint32_t bootDataPtr = otaReadU32(img, 0x1010);
  if (bootDataPtr < OtaFlashBase || bootDataPtr + 12 > OtaFlashBase + imageSize) {
    return "boot data pointer outside the image";
  }
  const uint32_t bd = bootDataPtr - OtaFlashBase;
  if (otaReadU32(img, bd) != OtaFlashBase) {
    return "boot data start mismatch";
  }
  if (otaReadU32(img, bd + 4) != imageSize) {
    return "boot data length does not match the image";
  }
  if (otaReadU32(img, bd + 8) != 0) {
    return "boot data plugin flag set";
  }
  // Project identity: reject another Teensy 4.1 project's firmware
  {
    const char *marker = OTA_PROJECT_MARKER;
    const uint32_t mlen = static_cast<uint32_t>(strlen(marker));
    bool found = false;
    for (uint32_t i = 0; i + mlen <= imageSize && !found; i++) {
      found = memcmp(img + i, marker, mlen) == 0;
    }
    if (!found) {
      return "image is not a build of this firmware (marker missing)";
    }
  }
  return nullptr;
}

// Read-back integrity: the staged flash must hold exactly what was streamed
inline const char *otaVerifyStagedCrc(const uint8_t *img, uint32_t imageSize,
                                      uint32_t expectedCrc) {
  return otaCrc32(img, imageSize) == expectedCrc ? nullptr
                                                 : "staged image failed read-back CRC";
}

#endif
