#ifndef OTA_INGEST_H
#define OTA_INGEST_H

// OTA hex-image ingest state machine: assembles lines from the streamed
// HTTP body, parses them (hex_parse.h), validates addresses, and routes the
// bytes into the staging buffer through flash callbacks with erase-on-demand
// sector tracking. No hardware dependencies - the callbacks let native tests
// run the whole machine against a RAM-backed fake flash.
//
// Layout: the running image occupies flash from OtaFlashBase; the staged
// image lands at a FIXED buffer base half-way up the 8MB part. Image bytes
// for absolute address A stage at OtaBufferBase + (A - OtaFlashBase).
// The cap keeps the buffer clear of the Teensy core's emulated-EEPROM
// region (0x607C0000+), so a future EEPROM.write() can never be destroyed
// by an update.

#include <stdint.h>
#include "hex_parse.h"
#include "ota_crc.h"

constexpr uint32_t OtaFlashBase = 0x60000000u;
constexpr uint32_t OtaBufferBase = 0x60400000u;
constexpr uint32_t OtaImageMax = 0x003C0000u; // buffer top 0x607C0000 (EEPROM reserve)
constexpr uint32_t OtaSectorSize = 4096u;
constexpr uint32_t OtaSectorCount = OtaImageMax / OtaSectorSize;

struct OtaFlashOps {
  // eraseSector: addr is sector-aligned, inside the buffer region
  void (*eraseSector)(uint32_t addr);
  // write: len <= 255, never crosses outside the buffer; flash semantics
  // (program 1->0 into erased 0xFF)
  void (*write)(uint32_t addr, const uint8_t *data, uint32_t len);
};

struct OtaIngest {
  HexParser hex;
  uint32_t minAddr = 0xFFFFFFFFu;
  uint32_t maxAddr = 0;
  uint32_t nextAddr = 0;   // contiguity: each record must continue the last
  bool haveData = false;
  uint64_t bytesWritten = 0;
  uint32_t crc = 0xFFFFFFFFu; // running CRC over the logical image bytes
  uint32_t lineCount = 0;
  char line[600];
  uint32_t lineLen = 0;
  uint8_t erased[(OtaSectorCount + 7) / 8] = {};
  const char *error = nullptr;
  bool eofSeen = false;
};

inline void otaIngestInit(OtaIngest &s) {
  s = OtaIngest{};
}

// Final CRC of the streamed image body (valid after otaIngestFinish)
inline uint32_t otaIngestCrc(const OtaIngest &s) {
  return s.crc ^ 0xFFFFFFFFu;
}

// Marker every build of THIS firmware embeds (see ota.cpp). The staged image
// must contain it: the FlexSPI flash-size word only distinguishes Teensy 4.0
// from 4.1, so without this any other Teensy 4.1 project's hex would verify
// and boot foreign code onto inverter hardware, losing OTA remotely.
#define OTA_PROJECT_MARKER "teg-inverter-fw:1"

inline bool otaSectorErased(const OtaIngest &s, uint32_t sector) {
  return (s.erased[sector >> 3] >> (sector & 7)) & 1;
}

inline void otaMarkErased(OtaIngest &s, uint32_t sector) {
  s.erased[sector >> 3] |= static_cast<uint8_t>(1u << (sector & 7));
}

// Handle one complete parsed line's worth of data
inline bool otaHandleData(OtaIngest &s, const OtaFlashOps &ops, uint32_t addr,
                          const uint8_t *data, uint8_t count) {
  if (count == 0) {
    // Legal-but-empty padding record. MUST bail before the sector loop:
    // (off + count - 1) would underflow to 0xFFFFFFFF and the erase loop
    // would sweep the whole chip - running firmware included.
    return true;
  }
  if (addr < OtaFlashBase) {
    s.error = "record below flash base";
    return false;
  }
  const uint32_t off = addr - OtaFlashBase;
  if (off >= OtaImageMax || count > OtaImageMax - off) {
    s.error = "image exceeds the staging buffer";
    return false;
  }
  // Strict contiguity: the toolchain emits dense ascending images, so every
  // record must continue exactly where the last one ended. This rejects
  // duplicates, overlaps and out-of-order records outright - without it, a
  // crafted stream could pass the byte-count density check while leaving
  // gap sectors carrying stale bytes from a previous upload attempt.
  if (s.haveData) {
    if (addr != s.nextAddr) {
      s.error = "non-contiguous data record";
      return false;
    }
  } else {
    s.haveData = true;
    s.minAddr = addr;
  }
  const uint32_t lastSec = (off + count - 1) / OtaSectorSize;
  // Erase-on-demand: every sector the record touches must be erased first
  for (uint32_t sec = off / OtaSectorSize; sec <= lastSec && sec < OtaSectorCount; sec++) {
    if (!otaSectorErased(s, sec)) {
      ops.eraseSector(OtaBufferBase + sec * OtaSectorSize);
      otaMarkErased(s, sec);
    }
  }
  ops.write(OtaBufferBase + off, data, count);
  // CRC over the wire bytes in address order (contiguity is enforced above,
  // so this equals a CRC of the final image body)
  s.crc = otaCrc32Update(s.crc, data, count);
  s.nextAddr = addr + count;
  if (addr + count - 1 > s.maxAddr) s.maxAddr = addr + count - 1;
  s.bytesWritten += count;
  return true;
}

// Feed one body byte; returns false when the stream is invalid (s.error set)
inline bool otaFeedByte(OtaIngest &s, char c, const OtaFlashOps &ops) {
  if (s.error != nullptr) {
    return false;
  }
  if (c != '\n') {
    if (s.lineLen < sizeof(s.line) - 1) {
      s.line[s.lineLen++] = c;
    } else {
      s.error = "hex line too long";
      return false;
    }
    return true;
  }
  s.line[s.lineLen] = '\0';
  s.lineLen = 0;
  s.lineCount++;

  uint32_t addr;
  uint8_t data[255];
  uint8_t count;
  switch (hexParseLine(s.hex, s.line, &addr, data, &count)) {
    case HexOk:
      return true;
    case HexData:
      return otaHandleData(s, ops, addr, data, count);
    case HexEof:
      s.eofSeen = true;
      return true;
    case HexErrChecksum:
      s.error = "hex checksum mismatch";
      return false;
    case HexErrAfterEof:
      s.error = "data after end-of-file record";
      return false;
    default:
      s.error = "malformed hex record";
      return false;
  }
}

// End of stream: a trailing unterminated line is processed, EOF is required
inline bool otaIngestFinish(OtaIngest &s, const OtaFlashOps &ops) {
  if (s.error != nullptr) {
    return false;
  }
  if (s.lineLen > 0 && !otaFeedByte(s, '\n', ops)) {
    return false;
  }
  if (!s.eofSeen) {
    s.error = "truncated upload (no end-of-file record)";
    return false;
  }
  if (s.bytesWritten == 0) {
    s.error = "no data records";
    return false;
  }
  return true;
}

#endif
