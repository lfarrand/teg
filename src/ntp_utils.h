#ifndef NTP_UTILS_H
#define NTP_UTILS_H

// NTP packet construction/parsing: no hardware dependencies, unit-tested natively.

#include <stdint.h>
#include <string.h>

constexpr int NtpPacketSize = 48;
constexpr uint32_t SecondsFrom1900To1970 = 2208988800UL;

inline void buildNtpRequest(uint8_t *packet) {
  memset(packet, 0, NtpPacketSize);
  packet[0] = 0b11100011; // LI=unsynchronized, version 4, mode 3 (client)
  packet[1] = 0;          // stratum
  packet[2] = 6;          // poll interval
  packet[3] = 0xEC;       // precision
  packet[12] = 49;        // reference ID "1N14"
  packet[13] = 0x4E;
  packet[14] = 49;
  packet[15] = 52;
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

#endif
