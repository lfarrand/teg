#ifndef NTP_UTILS_H
#define NTP_UTILS_H

// NTP packet construction/parsing: no hardware dependencies, unit-tested natively.

#include <stdint.h>
#include <string.h>

constexpr int NtpPacketSize = 48;
constexpr uint32_t SecondsFrom1900To1970 = 2208988800UL;

inline void ntpWriteU64(uint8_t *dst, uint64_t value) {
  for (int i = 7; i >= 0; --i) {
    dst[i] = static_cast<uint8_t>(value);
    value >>= 8;
  }
}

inline uint64_t ntpReadU64(const uint8_t *src) {
  uint64_t value = 0;
  for (int i = 0; i < 8; ++i) value = (value << 8) | src[i];
  return value;
}

inline void buildNtpRequest(uint8_t *packet, uint64_t associationToken = 0) {
  memset(packet, 0, NtpPacketSize);
  packet[0] = 0b11100011; // LI=unsynchronized, version 4, mode 3 (client)
  packet[1] = 0;          // stratum
  packet[2] = 6;          // poll interval
  packet[3] = 0xEC;       // precision
  packet[12] = 49;        // reference ID "1N14"
  packet[13] = 0x4E;
  packet[14] = 49;
  packet[15] = 52;
  // Servers copy the client's transmit timestamp into the reply's originate
  // timestamp. A per-request token binds the otherwise connectionless UDP
  // reply to this request and rejects stale/unsolicited packets.
  ntpWriteU64(packet + 40, associationToken);
}

// Transmit timestamp seconds (big-endian at offset 40)
inline uint32_t parseNtpSeconds(const uint8_t *packet) {
  return (static_cast<uint32_t>(packet[40]) << 24) |
         (static_cast<uint32_t>(packet[41]) << 16) |
         (static_cast<uint32_t>(packet[42]) << 8) |
         static_cast<uint32_t>(packet[43]);
}

constexpr uint32_t ntpToUnixEpoch(uint32_t secsSince1900) {
  return secsSince1900 - SecondsFrom1900To1970;
}

inline bool validateNtpResponse(const uint8_t *packet, uint64_t associationToken,
                                uint32_t *secondsOut = nullptr) {
  const uint8_t li = packet[0] >> 6;
  const uint8_t version = (packet[0] >> 3) & 0x07;
  const uint8_t mode = packet[0] & 0x07;
  const uint8_t stratum = packet[1];
  if (li == 3 || (version != 3 && version != 4) || mode != 4 ||
      stratum == 0 || stratum > 15 ||
      ntpReadU64(packet + 24) != associationToken) {
    return false;
  }
  const uint32_t seconds = parseNtpSeconds(packet);
  if (seconds < SecondsFrom1900To1970) return false;
  if (secondsOut) *secondsOut = seconds;
  return true;
}

#endif
