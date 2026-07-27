#ifndef OTA_CRC_H
#define OTA_CRC_H

// CRC-32 (IEEE 802.3, reflected, init/final 0xFFFFFFFF) for OTA image
// integrity. Table-free so it costs no flash-resident lookup table - the
// commit path must not read flash for anything but the image itself.

#include <stdint.h>

inline uint32_t otaCrc32Update(uint32_t crc, const uint8_t *data, uint32_t len) {
  for (uint32_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));
    }
  }
  return crc;
}

inline uint32_t otaCrc32(const uint8_t *data, uint32_t len) {
  return otaCrc32Update(0xFFFFFFFFu, data, len) ^ 0xFFFFFFFFu;
}

#endif
