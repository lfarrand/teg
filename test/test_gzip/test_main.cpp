// Tests for the streaming gunzip wrapper (gzip_stream.h over vendored miniz)

#include <unity.h>
#include <string.h>
#include <gzip_stream.h>

// gzip.compress(b"type=reference\n0.5\n-0.5\n1.0\n", 9, mtime=0)
static const uint8_t kGz[] = {
  31,139,8,0,0,0,0,0,2,10,43,169,44,72,181,45,74,77,75,45,74,205,75,78,229,50,
  208,51,229,210,5,17,134,122,6,92,0,23,89,48,82,28,0,0,0};
static const char kExpected[] = "type=reference\n0.5\n-0.5\n1.0\n";

static char captured[256];
static size_t capturedLen;
static GzipInflater inflater;

void setUp() {
  capturedLen = 0;
  memset(captured, 0, sizeof(captured));
  inflater.begin();
}
void tearDown() {}

static bool capture(void *, const uint8_t *data, size_t len) {
  memcpy(captured + capturedLen, data, len);
  capturedLen += len;
  return true;
}

void test_inflate_whole_buffer() {
  TEST_ASSERT_TRUE(inflater.feed(kGz, sizeof(kGz), capture, nullptr));
  TEST_ASSERT_TRUE(inflater.finish());
  TEST_ASSERT_EQUAL_UINT32(strlen(kExpected), capturedLen);
  TEST_ASSERT_EQUAL_MEMORY(kExpected, captured, capturedLen);
}

void test_inflate_byte_at_a_time() {
  // The streaming ingest feeds arbitrary chunk sizes; 1 byte is the worst case
  for (size_t i = 0; i < sizeof(kGz); i++) {
    TEST_ASSERT_TRUE(inflater.feed(&kGz[i], 1, capture, nullptr));
  }
  TEST_ASSERT_TRUE(inflater.finish());
  TEST_ASSERT_EQUAL_MEMORY(kExpected, captured, strlen(kExpected));
}

void test_rejects_bad_magic() {
  uint8_t bad[sizeof(kGz)];
  memcpy(bad, kGz, sizeof(kGz));
  bad[0] = 0x42;
  TEST_ASSERT_FALSE(inflater.feed(bad, sizeof(bad), capture, nullptr));
  TEST_ASSERT_EQUAL_STRING("not a gzip stream", inflater.error());
}

void test_detects_truncation() {
  TEST_ASSERT_TRUE(inflater.feed(kGz, sizeof(kGz) - 6, capture, nullptr));
  TEST_ASSERT_FALSE(inflater.finish());
  TEST_ASSERT_EQUAL_STRING("truncated gzip stream", inflater.error());
}

void test_detects_crc_corruption() {
  uint8_t bad[sizeof(kGz)];
  memcpy(bad, kGz, sizeof(bad));
  bad[sizeof(bad) - 5] ^= 0xFF; // flip a CRC byte in the trailer
  // The corruption may surface during feed (trailer check) - either way,
  // finish() must not report success
  const bool fedOk = inflater.feed(bad, sizeof(bad), capture, nullptr);
  TEST_ASSERT_FALSE(fedOk && inflater.finish());
}

void test_magic_probe() {
  TEST_ASSERT_TRUE(GzipInflater::looksLikeGzip(kGz));
  const uint8_t text[2] = {'t', 'y'};
  TEST_ASSERT_FALSE(GzipInflater::looksLikeGzip(text));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_inflate_whole_buffer);
  RUN_TEST(test_inflate_byte_at_a_time);
  RUN_TEST(test_rejects_bad_magic);
  RUN_TEST(test_detects_truncation);
  RUN_TEST(test_detects_crc_corruption);
  RUN_TEST(test_magic_probe);
  return UNITY_END();
}
