// Tests for the streaming Intel HEX parser (hex_parse.h)

#include <unity.h>
#include <string.h>
#include <hex_parse.h>

void setUp() {}
void tearDown() {}

static HexParser p;
static uint32_t addr;
static uint8_t data[255];
static uint8_t count;

static HexResult parse(const char *line) {
  return hexParseLine(p, line, &addr, data, &count);
}

void test_valid_data_record() {
  p = HexParser{};
  // 16 bytes at 0x0100 (checksum computed for this exact record)
  TEST_ASSERT_EQUAL_UINT8(HexData,
    parse(":10010000214601360121470136007EFE09D2190140"));
  TEST_ASSERT_EQUAL_UINT32(0x0100, addr);
  TEST_ASSERT_EQUAL_UINT8(16, count);
  TEST_ASSERT_EQUAL_UINT8(0x21, data[0]);
  TEST_ASSERT_EQUAL_UINT8(0x01, data[15]);
}

void test_ext_addr_then_data() {
  p = HexParser{};
  // Type 04: upper word 0x6000 -> base 0x60000000. Sum: 02+00+00+04+60+00 = 0x66 -> CC = 0x9A
  TEST_ASSERT_EQUAL_UINT8(HexOk, parse(":0200000460009A"));
  // 4 bytes at offset 0x0000: FCFB tag. Sum: 04+00+00+00+46+43+46+42 = 0x115 -> CC = 0xEB
  TEST_ASSERT_EQUAL_UINT8(HexData, parse(":0400000046434642EB"));
  TEST_ASSERT_EQUAL_UINT32(0x60000000u, addr);
  TEST_ASSERT_EQUAL_UINT8(4, count);
  TEST_ASSERT_EQUAL_UINT8(0x46, data[0]);
}

void test_bad_checksum() {
  p = HexParser{};
  TEST_ASSERT_EQUAL_UINT8(HexErrChecksum,
    parse(":10010000214601360121470136007EFE09D2190141"));
}

void test_bad_hex_digit() {
  p = HexParser{};
  TEST_ASSERT_EQUAL_UINT8(HexErrFormat, parse(":0G00000000FF"));
}

void test_short_line() {
  p = HexParser{};
  // Declares 16 data bytes but the line ends early
  TEST_ASSERT_EQUAL_UINT8(HexErrFormat, parse(":1001000021460136"));
}

void test_trailing_garbage() {
  p = HexParser{};
  TEST_ASSERT_EQUAL_UINT8(HexErrFormat, parse(":00000001FFxyz"));
}

void test_eof_and_after_eof() {
  p = HexParser{};
  TEST_ASSERT_EQUAL_UINT8(HexEof, parse(":00000001FF"));
  TEST_ASSERT_TRUE(p.eofSeen);
  TEST_ASSERT_EQUAL_UINT8(HexErrAfterEof, parse(":02000004600098"));
}

void test_blank_lines_and_whitespace() {
  p = HexParser{};
  TEST_ASSERT_EQUAL_UINT8(HexOk, parse(""));
  TEST_ASSERT_EQUAL_UINT8(HexOk, parse("\r\n"));
  TEST_ASSERT_EQUAL_UINT8(HexOk, parse("   "));
  // CRLF-terminated valid record
  TEST_ASSERT_EQUAL_UINT8(HexEof, parse(":00000001FF\r\n"));
}

void test_lowercase_hex() {
  p = HexParser{};
  TEST_ASSERT_EQUAL_UINT8(HexOk, parse(":02000004600a90"));
  TEST_ASSERT_EQUAL_UINT32(0x600A0000u, p.extAddr);
}

void test_start_linear_address() {
  p = HexParser{};
  // Type 05, entry 0x60001004. Sum: 04+00+00+05+60+00+10+04 = 0x7D -> CC = 0x83
  TEST_ASSERT_EQUAL_UINT8(HexOk, parse(":040000056000100483"));
  TEST_ASSERT_TRUE(p.startSeen);
  TEST_ASSERT_EQUAL_UINT32(0x60001004u, p.startAddr);
}

void test_unknown_record_type() {
  p = HexParser{};
  // Type 0x06 doesn't exist. Sum: 00+00+00+06 = 0x06 -> CC = 0xFA
  TEST_ASSERT_EQUAL_UINT8(HexErrFormat, parse(":00000006FA"));
}

void test_legacy_segment_ignored() {
  p = HexParser{};
  // Type 03 (start segment): checksummed then ignored.
  // Sum: 04+00+00+03+00+00+3E+00 = 0x45 -> CC = 0xBB
  TEST_ASSERT_EQUAL_UINT8(HexOk, parse(":0400000300003E00BB"));
}

void test_not_a_record() {
  p = HexParser{};
  TEST_ASSERT_EQUAL_UINT8(HexErrFormat, parse("hello"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_valid_data_record);
  RUN_TEST(test_ext_addr_then_data);
  RUN_TEST(test_bad_checksum);
  RUN_TEST(test_bad_hex_digit);
  RUN_TEST(test_short_line);
  RUN_TEST(test_trailing_garbage);
  RUN_TEST(test_eof_and_after_eof);
  RUN_TEST(test_blank_lines_and_whitespace);
  RUN_TEST(test_lowercase_hex);
  RUN_TEST(test_start_linear_address);
  RUN_TEST(test_unknown_record_type);
  RUN_TEST(test_legacy_segment_ignored);
  RUN_TEST(test_not_a_record);
  return UNITY_END();
}
