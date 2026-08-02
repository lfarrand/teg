#ifndef HEX_PARSE_H
#define HEX_PARSE_H

// Streaming Intel HEX parser for the OTA upload: one line per call, no
// dynamic allocation, no hardware dependencies, unit-tested natively.
//
// Record format  :LLAAAATT[DD...]CC
//   LL count of data bytes, AAAA 16-bit address, TT type, CC checksum
//   (two's complement of the sum of every byte from LL through the last DD).
// Types handled: 00 data, 01 EOF, 04 extended linear address (upper 16 bits
// of the 32-bit target address), 05 start linear address (entry point,
// recorded but not required), 02/03 legacy segment records (checksummed,
// then ignored - GNU objcopy can emit 03).

#include <stdint.h>

enum HexResult : uint8_t {
  HexOk = 0,       // line consumed, nothing to write (ignored record/blank)
  HexData = 1,     // *addr/*data/*count filled with bytes to write
  HexEof = 2,      // end-of-file record seen
  HexErrFormat = 3,   // not a record / bad hex digit / wrong length
  HexErrChecksum = 4,
  HexErrAfterEof = 5, // records after EOF indicate a corrupt stream
};

struct HexParser {
  uint32_t extAddr = 0;    // from type-04, already shifted <<16
  uint32_t startAddr = 0;  // from type-05
  bool startSeen = false;
  bool eofSeen = false;
};

inline int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

inline int hexByteAt(const char *s) {
  const int hi = hexNibble(s[0]);
  if (hi < 0) {
    return -1;
  }
  const int lo = hexNibble(s[1]);
  if (lo < 0) {
    return -1;
  }
  return (hi << 4) | lo;
}

// Parse one line (NUL-terminated; trailing CR/LF/spaces tolerated). On
// HexData, *addr is the absolute 32-bit address, data[] holds *count bytes
// (data must have room for 255).
inline HexResult hexParseLine(HexParser &p, const char *line, uint32_t *addr, uint8_t *data,
                              uint8_t *count) {
  // Skip leading whitespace; blank lines are tolerated
  while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n') {
    line++;
  }
  if (*line == '\0') {
    return HexOk;
  }
  if (*line != ':') {
    return HexErrFormat;
  }
  if (p.eofSeen) {
    return HexErrAfterEof;
  }
  line++;

  const int len = hexByteAt(line);
  if (len < 0) {
    return HexErrFormat;
  }
  // Record body: LL AAAA TT + LL data bytes + CC = 5 + LL byte pairs
  const int pairs = 5 + len;
  uint8_t bytes[5 + 255];
  uint32_t sum = 0;
  for (int i = 0; i < pairs; i++) {
    const int b = hexByteAt(line + 2 * i);
    if (b < 0) {
      return HexErrFormat; // short line or bad digit
    }
    bytes[i] = static_cast<uint8_t>(b);
    sum += static_cast<uint32_t>(b);
  }
  // Nothing but whitespace may follow the record
  const char *tail = line + 2 * pairs;
  while (*tail == ' ' || *tail == '\t' || *tail == '\r' || *tail == '\n') {
    tail++;
  }
  if (*tail != '\0') {
    return HexErrFormat;
  }
  if ((sum & 0xFF) != 0) {
    return HexErrChecksum;
  }

  const uint16_t offset = static_cast<uint16_t>((bytes[1] << 8) | bytes[2]);
  const uint8_t type = bytes[3];
  const uint8_t *payload = bytes + 4;

  switch (type) {
    case 0x00: // data
      *addr = p.extAddr | offset;
      *count = static_cast<uint8_t>(len);
      for (int i = 0; i < len; i++) {
        data[i] = payload[i];
      }
      return HexData;
    case 0x01: // EOF
      if (len != 0) {
        return HexErrFormat;
      }
      p.eofSeen = true;
      return HexEof;
    case 0x04: // extended linear address
      if (len != 2) {
        return HexErrFormat;
      }
      p.extAddr = (static_cast<uint32_t>(payload[0]) << 24) |
                  (static_cast<uint32_t>(payload[1]) << 16);
      return HexOk;
    case 0x05: // start linear address (entry point)
      if (len != 4) {
        return HexErrFormat;
      }
      p.startAddr = (static_cast<uint32_t>(payload[0]) << 24) |
                    (static_cast<uint32_t>(payload[1]) << 16) |
                    (static_cast<uint32_t>(payload[2]) << 8) | payload[3];
      p.startSeen = true;
      return HexOk;
    case 0x02: // legacy segment records: validated above, then ignored
    case 0x03:
      return HexOk;
    default:
      return HexErrFormat;
  }
}

#endif
