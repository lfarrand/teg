// Tests for NTP packet construction/parsing (ntp_utils.h)

#include <unity.h>
#include <ntp_utils.h>

void setUp() {}
void tearDown() {}

void test_request_packet_layout() {
  uint8_t packet[NtpPacketSize];
  memset(packet, 0xFF, sizeof(packet)); // prove every byte is written
  constexpr uint64_t token = 0x0123456789ABCDEFULL;
  buildNtpRequest(packet, token);

  TEST_ASSERT_EQUAL_HEX8(0b11100011, packet[0]); // LI 3, version 4, mode 3
  TEST_ASSERT_EQUAL_HEX8(0, packet[1]);          // stratum
  TEST_ASSERT_EQUAL_HEX8(6, packet[2]);          // poll
  TEST_ASSERT_EQUAL_HEX8(0xEC, packet[3]);       // precision
  TEST_ASSERT_EQUAL_HEX8(49, packet[12]);
  TEST_ASSERT_EQUAL_HEX8(0x4E, packet[13]);
  TEST_ASSERT_EQUAL_HEX8(49, packet[14]);
  TEST_ASSERT_EQUAL_HEX8(52, packet[15]);
  for (int i = 16; i < 40; i++) {
    TEST_ASSERT_EQUAL_HEX8(0, packet[i]);
  }
  TEST_ASSERT_EQUAL_HEX64(token, ntpReadU64(packet + 40));
}

void test_parse_transmit_timestamp_big_endian() {
  uint8_t packet[NtpPacketSize] = {0};
  packet[40] = 0xEA;
  packet[41] = 0x60;
  packet[42] = 0x25;
  packet[43] = 0x80;
  TEST_ASSERT_EQUAL_UINT32(0xEA602580UL, parseNtpSeconds(packet));
}

void test_epoch_conversion() {
  // 1 Jan 1970 00:00:00 in NTP time is exactly the 1900->1970 offset
  TEST_ASSERT_EQUAL_UINT32(0, ntpToUnixEpoch(2208988800UL));
  // 2026-07-20 00:00:00 UTC = unix 1784505600
  TEST_ASSERT_EQUAL_UINT32(1784505600UL, ntpToUnixEpoch(1784505600UL + 2208988800UL));
}

void test_response_validation_binds_request_and_rejects_bad_server_state() {
  constexpr uint64_t token = 0xA55AA55A12345678ULL;
  uint8_t packet[NtpPacketSize] = {0};
  packet[0] = 0x24; // LI=0, v4, server mode
  packet[1] = 2;
  ntpWriteU64(packet + 24, token);
  const uint32_t seconds = SecondsFrom1900To1970 + 1784505600UL;
  packet[40] = static_cast<uint8_t>(seconds >> 24);
  packet[41] = static_cast<uint8_t>(seconds >> 16);
  packet[42] = static_cast<uint8_t>(seconds >> 8);
  packet[43] = static_cast<uint8_t>(seconds);
  uint32_t parsed = 0;
  TEST_ASSERT_TRUE(validateNtpResponse(packet, token, &parsed));
  TEST_ASSERT_EQUAL_UINT32(seconds, parsed);

  TEST_ASSERT_FALSE(validateNtpResponse(packet, token + 1));
  packet[0] = 0xE4; // leap indicator 3: server unsynchronised
  TEST_ASSERT_FALSE(validateNtpResponse(packet, token));
  packet[0] = 0x23; // client mode, not server
  TEST_ASSERT_FALSE(validateNtpResponse(packet, token));
  packet[0] = 0x24;
  packet[1] = 0; // kiss-o'-death / invalid time source
  TEST_ASSERT_FALSE(validateNtpResponse(packet, token));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_request_packet_layout);
  RUN_TEST(test_parse_transmit_timestamp_big_endian);
  RUN_TEST(test_epoch_conversion);
  RUN_TEST(test_response_validation_binds_request_and_rejects_bad_server_state);
  return UNITY_END();
}
