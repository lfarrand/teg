// Tests for NTP packet construction/parsing (ntp_utils.h)

#include <unity.h>
#include <ntp_utils.h>

void setUp() {}
void tearDown() {}

void test_request_packet_layout() {
  uint8_t packet[NtpPacketSize];
  memset(packet, 0xFF, sizeof(packet)); // prove every byte is written
  buildNtpRequest(packet);

  TEST_ASSERT_EQUAL_HEX8(0b11100011, packet[0]); // LI 3, version 4, mode 3
  TEST_ASSERT_EQUAL_HEX8(0, packet[1]);          // stratum
  TEST_ASSERT_EQUAL_HEX8(6, packet[2]);          // poll
  TEST_ASSERT_EQUAL_HEX8(0xEC, packet[3]);       // precision
  TEST_ASSERT_EQUAL_HEX8(49, packet[12]);
  TEST_ASSERT_EQUAL_HEX8(0x4E, packet[13]);
  TEST_ASSERT_EQUAL_HEX8(49, packet[14]);
  TEST_ASSERT_EQUAL_HEX8(52, packet[15]);
  for (int i = 16; i < NtpPacketSize; i++) {
    TEST_ASSERT_EQUAL_HEX8(0, packet[i]);
  }
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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_request_packet_layout);
  RUN_TEST(test_parse_transmit_timestamp_big_endian);
  RUN_TEST(test_epoch_conversion);
  return UNITY_END();
}
