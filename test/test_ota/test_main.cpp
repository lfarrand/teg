// End-to-end tests for the OTA ingest state machine (ota_ingest.h) and the
// pre-commit image verifier (ota_verify.h): synthesize Teensy 4.1 boot
// images, stream them through the hex pipeline into a RAM-backed fake flash
// with real flash semantics (program = AND into erased 0xFF), and verify.

#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <ota_ingest.h>
#include <ota_verify.h>

void setUp() {}
void tearDown() {}

namespace {

constexpr uint32_t ImgSize = 0x2100; // > 0x2000 header minimum, spans 3 sectors
uint8_t fakeFlash[0x4000];
uint32_t eraseCount;
uint32_t writeCount;

void fakeErase(uint32_t addr) {
  const uint32_t off = addr - OtaBufferBase;
  if (off + OtaSectorSize <= sizeof(fakeFlash)) {
    memset(fakeFlash + off, 0xFF, OtaSectorSize);
  }
  eraseCount++;
}

void fakeWrite(uint32_t addr, const uint8_t *data, uint32_t len) {
  writeCount++;
  const uint32_t off = addr - OtaBufferBase;
  for (uint32_t i = 0; i < len && off + i < sizeof(fakeFlash); i++) {
    fakeFlash[off + i] &= data[i]; // flash programs 1 -> 0 only
  }
}

const OtaFlashOps ops = {&fakeErase, &fakeWrite};

void putU32(uint8_t *img, uint32_t off, uint32_t v) {
  img[off] = v & 0xFF;
  img[off + 1] = (v >> 8) & 0xFF;
  img[off + 2] = (v >> 16) & 0xFF;
  img[off + 3] = (v >> 24) & 0xFF;
}

// A structurally valid Teensy 4.1 image (values from bootdata.c / the RM)
void buildImage(uint8_t *img) {
  memset(img, 0xFF, ImgSize);
  putU32(img, 0x000, 0x42464346u); // FCFB tag
  putU32(img, 0x004, 0x56010000u); // FCFB version
  putU32(img, 0x050, 0x00800000u); // sflashA1Size: 8MB = Teensy 4.1
  putU32(img, 0x1000, 0x432000D1u); // IVT header as the core emits it
  putU32(img, 0x1004, 0x60001E81u); // entry, Thumb bit set
  putU32(img, 0x1008, 0);
  putU32(img, 0x100C, 0);
  putU32(img, 0x1010, 0x60001020u); // boot data pointer
  putU32(img, 0x1014, 0x60001000u); // IVT self
  putU32(img, 0x1018, 0);
  putU32(img, 0x1020, 0x60000000u); // boot data: start
  putU32(img, 0x1024, ImgSize);     // boot data: length
  putU32(img, 0x1028, 0);           // boot data: plugin
  memcpy(img + 0x1800, OTA_PROJECT_MARKER, strlen(OTA_PROJECT_MARKER)); // project identity
}

void emitRecord(char *out, uint16_t addr, uint8_t type, const uint8_t *payload, uint8_t len) {
  uint32_t sum = len + (addr >> 8) + (addr & 0xFF) + type;
  int n = sprintf(out, ":%02X%04X%02X", len, addr, type);
  for (uint8_t i = 0; i < len; i++) {
    n += sprintf(out + n, "%02X", payload[i]);
    sum += payload[i];
  }
  sprintf(out + n, "%02X\n", static_cast<uint8_t>(0x100 - (sum & 0xFF)) & 0xFF);
}

bool feedLine(OtaIngest &s, const char *line) {
  for (const char *c = line; *c != '\0'; c++) {
    if (!otaFeedByte(s, *c, ops)) {
      return false;
    }
  }
  return true;
}

// Stream a full image; skipOffset = one 16-byte record to omit (0xFFFFFFFF = none)
bool streamImage(OtaIngest &s, const uint8_t *img, uint32_t size,
                 uint32_t skipOffset = 0xFFFFFFFFu, bool sendEof = true) {
  char line[600];
  const uint8_t ela[2] = {0x60, 0x00};
  emitRecord(line, 0, 0x04, ela, 2);
  if (!feedLine(s, line)) {
    return false;
  }
  for (uint32_t off = 0; off < size; off += 16) {
    if (off == skipOffset) {
      continue;
    }
    emitRecord(line, static_cast<uint16_t>(off), 0x00, img + off, 16);
    if (!feedLine(s, line)) {
      return false;
    }
  }
  if (sendEof && !feedLine(s, ":00000001FF\n")) {
    return false;
  }
  return otaIngestFinish(s, ops);
}

// Full pipeline: returns the verifier's verdict (nullptr = pass)
const char *runPipeline(const uint8_t *img, uint32_t size) {
  memset(fakeFlash, 0x00, sizeof(fakeFlash)); // non-erased: missing erase corrupts
  eraseCount = 0;
  OtaIngest s;
  otaIngestInit(s);
  if (!streamImage(s, img, size)) {
    return s.error != nullptr ? s.error : "ingest failed";
  }
  return otaVerifyImage(fakeFlash, s.minAddr, s.maxAddr, s.bytesWritten, s.eofSeen);
}

} // namespace

void test_valid_image_end_to_end() {
  uint8_t img[ImgSize];
  buildImage(img);
  TEST_ASSERT_NULL(runPipeline(img, ImgSize));
  TEST_ASSERT_EQUAL_UINT32(3, eraseCount); // 0x2100 spans exactly 3 sectors
  TEST_ASSERT_EQUAL_UINT8(0x46, fakeFlash[0]); // staged bytes readable
}

void test_erase_on_demand_is_mandatory() {
  // The fake flash starts at 0x00 (non-erased): if the machine failed to
  // erase before writing, AND-semantics would corrupt every byte and the
  // magic checks would fail. Passing test_valid_image_end_to_end proves the
  // ordering; this test pins the inverse - a written-without-erase buffer
  // fails verification.
  uint8_t img[ImgSize];
  buildImage(img);
  memset(fakeFlash, 0x00, sizeof(fakeFlash));
  OtaIngest s;
  otaIngestInit(s);
  // Mark all sectors pre-"erased" so no erase callbacks fire, then stream:
  for (uint32_t sec = 0; sec < 4; sec++) {
    otaMarkErased(s, sec);
  }
  eraseCount = 0;
  TEST_ASSERT_TRUE(streamImage(s, img, ImgSize));
  TEST_ASSERT_EQUAL_UINT32(0, eraseCount);
  TEST_ASSERT_NOT_NULL(otaVerifyImage(fakeFlash, s.minAddr, s.maxAddr, s.bytesWritten, s.eofSeen));
}

void test_wrong_board_rejected() {
  uint8_t img[ImgSize];
  buildImage(img);
  putU32(img, 0x050, 0x00200000u); // Teensy 4.0's 2MB marker
  const char *err = runPipeline(img, ImgSize);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_NOT_NULL(strstr(err, "wrong board"));
}

void test_corrupt_checksum_stops_ingest() {
  memset(fakeFlash, 0x00, sizeof(fakeFlash));
  OtaIngest s;
  otaIngestInit(s);
  TEST_ASSERT_TRUE(feedLine(s, ":0200000460009A\n"));
  TEST_ASSERT_FALSE(feedLine(s, ":10010000214601360121470136007EFE09D2190141\n"));
  TEST_ASSERT_NOT_NULL(s.error);
}

void test_address_gap_rejected() {
  // A missing record is caught during INGEST by the contiguity rule (before
  // any further sector is touched), not merely at verify time
  uint8_t img[ImgSize];
  buildImage(img);
  memset(fakeFlash, 0x00, sizeof(fakeFlash));
  eraseCount = 0;
  OtaIngest s;
  otaIngestInit(s);
  TEST_ASSERT_FALSE(streamImage(s, img, ImgSize, 0x800)); // omit one record
  TEST_ASSERT_NOT_NULL(strstr(s.error, "non-contiguous"));
}

void test_record_below_flash_base_rejected() {
  memset(fakeFlash, 0x00, sizeof(fakeFlash));
  OtaIngest s;
  otaIngestInit(s);
  // ELA 0x5FFF: addresses below 0x60000000
  char line[600];
  const uint8_t ela[2] = {0x5F, 0xFF};
  emitRecord(line, 0, 0x04, ela, 2);
  TEST_ASSERT_TRUE(feedLine(s, line));
  uint8_t payload[4] = {1, 2, 3, 4};
  emitRecord(line, 0, 0x00, payload, 4);
  TEST_ASSERT_FALSE(feedLine(s, line));
  TEST_ASSERT_NOT_NULL(strstr(s.error, "below flash base"));
}

void test_image_beyond_buffer_rejected() {
  memset(fakeFlash, 0x00, sizeof(fakeFlash));
  OtaIngest s;
  otaIngestInit(s);
  // ELA 0x603C -> offset 0x3C0000 = OtaImageMax: first byte is out of range
  TEST_ASSERT_TRUE(feedLine(s, ":02000004603C5E\n"));
  uint8_t payload[4] = {1, 2, 3, 4};
  char line[600];
  emitRecord(line, 0, 0x00, payload, 4);
  TEST_ASSERT_FALSE(feedLine(s, line));
  TEST_ASSERT_NOT_NULL(strstr(s.error, "exceeds"));
}

void test_truncated_upload_rejected() {
  uint8_t img[ImgSize];
  buildImage(img);
  memset(fakeFlash, 0x00, sizeof(fakeFlash));
  OtaIngest s;
  otaIngestInit(s);
  TEST_ASSERT_FALSE(streamImage(s, img, ImgSize, 0xFFFFFFFFu, false)); // no EOF
  TEST_ASSERT_NOT_NULL(strstr(s.error, "truncated"));
}

void test_max_length_truncated_record_is_rejected_without_flash_callbacks() {
  OtaIngest s;
  otaIngestInit(s);
  eraseCount = 0;
  writeCount = 0;

  // Fill the largest accepted line with whitespace followed by ':'. The
  // terminator lands at line[599], so the parser must not inspect line[600].
  for (uint32_t i = 0; i < sizeof(s.line) - 2; i++) {
    TEST_ASSERT_TRUE(otaFeedByte(s, ' ', ops));
  }
  TEST_ASSERT_TRUE(otaFeedByte(s, ':', ops));
  TEST_ASSERT_EQUAL_UINT32(sizeof(s.line) - 1, s.lineLen);

  TEST_ASSERT_FALSE(otaIngestFinish(s, ops));
  TEST_ASSERT_EQUAL_STRING("malformed hex record", s.error);
  TEST_ASSERT_EQUAL_UINT32(0, eraseCount);
  TEST_ASSERT_EQUAL_UINT32(0, writeCount);
  TEST_ASSERT_EQUAL_UINT64(0, s.bytesWritten);
}

void test_corrupt_ivt_rejected() {
  uint8_t img[ImgSize];
  buildImage(img);
  putU32(img, 0x1000, 0x432000D2u); // wrong tag
  const char *err = runPipeline(img, ImgSize);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_NOT_NULL(strstr(err, "vector table"));
}

void test_entry_outside_image_rejected() {
  uint8_t img[ImgSize];
  buildImage(img);
  putU32(img, 0x1004, 0x60800001u); // Thumb bit ok, address beyond image
  const char *err = runPipeline(img, ImgSize);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_NOT_NULL(strstr(err, "entry point"));
}

void test_verifier_reads_staged_buffer_not_parse_state() {
  // Corrupting the staged buffer AFTER a clean ingest must fail verification:
  // what gets verified is exactly what the commit would copy
  uint8_t img[ImgSize];
  buildImage(img);
  TEST_ASSERT_NULL(runPipeline(img, ImgSize));
  fakeFlash[0x050] ^= 0xFF; // flip the board marker in the staged copy
  const char *err =
    otaVerifyImage(fakeFlash, 0x60000000u, 0x60000000u + ImgSize - 1, ImgSize, true);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_NOT_NULL(strstr(err, "wrong board"));
}

void test_boot_data_length_mismatch_rejected() {
  uint8_t img[ImgSize];
  buildImage(img);
  putU32(img, 0x1024, ImgSize - 16); // boot data length disagrees
  const char *err = runPipeline(img, ImgSize);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_NOT_NULL(strstr(err, "length"));
}

void test_boot_data_pointer_near_wraparound_rejected() {
  // The bound check used to be `bootDataPtr + 12 > OtaFlashBase + imageSize`. For a
  // pointer near UINT32_MAX that sum WRAPS to a small value and sails past the
  // comparison - after which bootDataPtr - OtaFlashBase becomes a huge offset and the
  // boot-data reads run far outside the image. A crafted image could reach it.
  uint8_t img[ImgSize];
  buildImage(img);
  putU32(img, 0x1010, 0xFFFFFFF8u); // + 12 wraps to 4
  const char *err = runPipeline(img, ImgSize);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_NOT_NULL(strstr(err, "boot data pointer"));
}

void test_boot_data_pointer_just_past_the_end_rejected() {
  // The ordinary in-range failure, to prove the rewritten comparison still catches it
  // and did not merely stop overflowing.
  uint8_t img[ImgSize];
  buildImage(img);
  putU32(img, 0x1010, 0x60000000u + ImgSize - 4); // 12 bytes will not fit
  const char *err = runPipeline(img, ImgSize);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_NOT_NULL(strstr(err, "boot data pointer"));
}

void test_zero_length_record_cannot_sweep_the_chip() {
  // A legal, correctly-checksummed type-00 record with LL=00 at the flash
  // base once underflowed the erase-loop bound to 0xFFFFFFFF: the erase
  // callback would have swept ~1M sectors - the running firmware and the
  // EEPROM region included - with the watchdog kicked throughout, before
  // verification ever ran. It must be a no-op.
  memset(fakeFlash, 0x00, sizeof(fakeFlash));
  eraseCount = 0;
  OtaIngest s;
  otaIngestInit(s);
  char line[600];
  const uint8_t ela[2] = {0x60, 0x00};
  emitRecord(line, 0, 0x04, ela, 2);
  TEST_ASSERT_TRUE(feedLine(s, line));
  emitRecord(line, 0, 0x00, nullptr, 0); // the payload-less data record
  TEST_ASSERT_TRUE(feedLine(s, line));
  TEST_ASSERT_EQUAL_UINT32(0, eraseCount);
  TEST_ASSERT_EQUAL_UINT64(0, s.bytesWritten);
  TEST_ASSERT_NULL(s.error);
}

void test_duplicate_and_out_of_order_records_rejected() {
  // Density-by-byte-count alone was satisfiable with duplicates while a gap
  // elsewhere kept stale bytes from a previous upload: strict contiguity
  // rejects duplicates, overlaps and reordering outright.
  uint8_t img[ImgSize];
  buildImage(img);
  memset(fakeFlash, 0x00, sizeof(fakeFlash));
  OtaIngest s;
  otaIngestInit(s);
  char line[600];
  const uint8_t ela[2] = {0x60, 0x00};
  TEST_ASSERT_TRUE((emitRecord(line, 0, 0x04, ela, 2), feedLine(s, line)));
  emitRecord(line, 0, 0x00, img, 16);
  TEST_ASSERT_TRUE(feedLine(s, line));
  emitRecord(line, 0, 0x00, img, 16); // same address again
  TEST_ASSERT_FALSE(feedLine(s, line));
  TEST_ASSERT_NOT_NULL(strstr(s.error, "non-contiguous"));

  otaIngestInit(s);
  TEST_ASSERT_TRUE((emitRecord(line, 0, 0x04, ela, 2), feedLine(s, line)));
  emitRecord(line, 0, 0x00, img, 16);
  TEST_ASSERT_TRUE(feedLine(s, line));
  emitRecord(line, 0x40, 0x00, img + 0x40, 16); // skips ahead
  TEST_ASSERT_FALSE(feedLine(s, line));
}

void test_foreign_project_image_rejected() {
  // Structurally perfect Teensy 4.1 image from another project: no marker
  uint8_t img[ImgSize];
  buildImage(img);
  memset(img + 0x1800, 0xAA, strlen(OTA_PROJECT_MARKER));
  const char *err = runPipeline(img, ImgSize);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_NOT_NULL(strstr(err, "marker"));
}

void test_staged_crc_detects_flash_write_failure() {
  uint8_t img[ImgSize];
  buildImage(img);
  memset(fakeFlash, 0x00, sizeof(fakeFlash));
  OtaIngest s;
  otaIngestInit(s);
  TEST_ASSERT_TRUE(streamImage(s, img, ImgSize));
  const uint32_t crc = otaIngestCrc(s);
  TEST_ASSERT_NULL(otaVerifyStagedCrc(fakeFlash, ImgSize, crc));
  fakeFlash[0x1500] ^= 0x01; // simulate a silently-failed program op
  TEST_ASSERT_NOT_NULL(otaVerifyStagedCrc(fakeFlash, ImgSize, crc));
}

void test_crc32_known_value() {
  // IEEE 802.3 CRC-32 of "123456789" is 0xCBF43926
  const uint8_t v[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, otaCrc32(v, sizeof(v)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_valid_image_end_to_end);
  RUN_TEST(test_zero_length_record_cannot_sweep_the_chip);
  RUN_TEST(test_duplicate_and_out_of_order_records_rejected);
  RUN_TEST(test_foreign_project_image_rejected);
  RUN_TEST(test_staged_crc_detects_flash_write_failure);
  RUN_TEST(test_crc32_known_value);
  RUN_TEST(test_erase_on_demand_is_mandatory);
  RUN_TEST(test_wrong_board_rejected);
  RUN_TEST(test_corrupt_checksum_stops_ingest);
  RUN_TEST(test_address_gap_rejected);
  RUN_TEST(test_record_below_flash_base_rejected);
  RUN_TEST(test_image_beyond_buffer_rejected);
  RUN_TEST(test_truncated_upload_rejected);
  RUN_TEST(test_max_length_truncated_record_is_rejected_without_flash_callbacks);
  RUN_TEST(test_boot_data_length_mismatch_rejected);
  RUN_TEST(test_boot_data_pointer_near_wraparound_rejected);
  RUN_TEST(test_boot_data_pointer_just_past_the_end_rejected);
  RUN_TEST(test_corrupt_ivt_rejected);
  RUN_TEST(test_entry_outside_image_rejected);
  RUN_TEST(test_verifier_reads_staged_buffer_not_parse_state);
  return UNITY_END();
}
