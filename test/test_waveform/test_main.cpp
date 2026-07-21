// Tests for the teg-wave v1 custom waveform parser (waveform_parse.h)

#include <unity.h>
#include <stdio.h>
#include <waveform_parse.h>
#include <spwm_math.h>

static int16_t samples[8192]; // test-sized destination (firmware uses 2M in PSRAM)
static int16_t levels[MaxWaveSegments];
static uint32_t micros[MaxWaveSegments];
static uint8_t type;

void setUp() { type = WaveTypeNone; }
void tearDown() {}

static int32_t parse(const char *text) {
  return parseWaveform(text, samples, sizeof(samples) / sizeof(samples[0]), levels, micros,
                       MaxWaveSegments, &type);
}

// ---------------------------------------------------------------------------
// Text: reference files
// ---------------------------------------------------------------------------

void test_reference_basic_with_comments_and_blanks() {
  const int32_t n = parse(
    "# teg-wave v1\n"
    "type=reference\n"
    "\n"
    "0.0   # start\n"
    "0.5\n"
    "1.0\r\n"
    "-0.25\n");
  TEST_ASSERT_EQUAL_INT32(4, n);
  TEST_ASSERT_EQUAL_UINT8(WaveTypeReference, type);
  TEST_ASSERT_EQUAL_INT16(0, samples[0]);
  TEST_ASSERT_INT16_WITHIN(1, 16383, samples[1]);
  TEST_ASSERT_EQUAL_INT16(32767, samples[2]);
  TEST_ASSERT_INT16_WITHIN(1, -8192, samples[3]);
}

void test_reference_clamps_out_of_range_levels() {
  TEST_ASSERT_EQUAL_INT32(2, parse("type=reference\n5.0\n-3.5\n"));
  TEST_ASSERT_EQUAL_INT16(32767, samples[0]);
  TEST_ASSERT_EQUAL_INT16(-32767, samples[1]);
}

void test_reference_errors() {
  TEST_ASSERT_EQUAL_INT32(WaveErrNoType, parse("0.5\n0.7\n"));
  TEST_ASSERT_EQUAL_INT32(WaveErrTooFew, parse("type=reference\n0.5\n"));
  TEST_ASSERT_EQUAL_INT32(WaveErrBadValue, parse("type=reference\n0.1\nbanana\n"));
}

void test_incremental_line_feed_matches_whole_text() {
  // The streaming path feeds lines one at a time; results must be identical
  WaveParser p;
  p.samples = samples;
  p.maxSamples = 16;
  const char *lines[] = {"# hi", "type=reference", "0.25", "", "-0.75  # tail comment"};
  for (const char *l : lines) {
    TEST_ASSERT_EQUAL_INT32(0, waveParseLine(p, l, l + strlen(l)));
  }
  TEST_ASSERT_EQUAL_INT32(2, waveParseFinish(p));
  TEST_ASSERT_INT16_WITHIN(1, 8191, samples[0]);
  TEST_ASSERT_INT16_WITHIN(1, -24575, samples[1]);
}

// ---------------------------------------------------------------------------
// Text: sequence files
// ---------------------------------------------------------------------------

void test_sequence_basic() {
  const int32_t n = parse(
    "type=sequence\n"
    "1.0, 1500\n"
    "0.0, 500\n"
    "-1.0, 300\n");
  TEST_ASSERT_EQUAL_INT32(3, n);
  TEST_ASSERT_EQUAL_UINT8(WaveTypeSequence, type);
  TEST_ASSERT_EQUAL_INT16(32767, levels[0]);
  TEST_ASSERT_EQUAL_INT16(0, levels[1]);
  TEST_ASSERT_EQUAL_INT16(-32767, levels[2]);
  TEST_ASSERT_EQUAL_UINT32(1500, micros[0]);
  TEST_ASSERT_EQUAL_UINT32(300, micros[2]);
}

void test_sequence_errors_and_limit() {
  TEST_ASSERT_EQUAL_INT32(WaveErrBadDuration, parse("type=sequence\n1.0\n"));
  TEST_ASSERT_EQUAL_INT32(WaveErrBadDuration, parse("type=sequence\n1.0, 0\n"));
  TEST_ASSERT_EQUAL_INT32(WaveErrBadValue, parse("type=sequence\nfoo, 100\n"));
  TEST_ASSERT_EQUAL_INT32(WaveErrTooFew, parse("type=sequence\n# nothing\n"));

  char big[4096] = "type=sequence\n";
  char *p = big + strlen(big);
  for (int i = 0; i < 65; i++) {
    p += sprintf(p, "0.5, 100\n");
  }
  TEST_ASSERT_EQUAL_INT32(WaveErrTooMany, parse(big));
}

// ---------------------------------------------------------------------------
// Binary header
// ---------------------------------------------------------------------------

void test_binary_header_roundtrip() {
  uint8_t hdr[WaveBinaryHeaderSize];
  waveBinaryHeaderWrite(hdr, WaveTypeReference, 2097152);
  uint8_t t;
  TEST_ASSERT_EQUAL_INT32(2097152, waveBinaryHeaderRead(hdr, &t));
  TEST_ASSERT_EQUAL_UINT8(WaveTypeReference, t);

  waveBinaryHeaderWrite(hdr, WaveTypeSequence, 7);
  TEST_ASSERT_EQUAL_INT32(7, waveBinaryHeaderRead(hdr, &t));
  TEST_ASSERT_EQUAL_UINT8(WaveTypeSequence, t);
}

void test_binary_header_rejects_bad_input() {
  uint8_t hdr[WaveBinaryHeaderSize];
  uint8_t t;
  waveBinaryHeaderWrite(hdr, WaveTypeReference, 100);
  hdr[0] = 'X'; // bad magic
  TEST_ASSERT_EQUAL_INT32(WaveErrBadBinary, waveBinaryHeaderRead(hdr, &t));

  waveBinaryHeaderWrite(hdr, WaveTypeReference, MaxStreamSamples + 1); // too long even to stream
  TEST_ASSERT_EQUAL_INT32(WaveErrBadBinary, waveBinaryHeaderRead(hdr, &t));

  waveBinaryHeaderWrite(hdr, WaveTypeReference, MaxWaveSamples + 1); // beyond PSRAM but streamable
  TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(MaxWaveSamples + 1), waveBinaryHeaderRead(hdr, &t));

  waveBinaryHeaderWrite(hdr, WaveTypeSequence, MaxWaveSegments + 1);
  TEST_ASSERT_EQUAL_INT32(WaveErrBadBinary, waveBinaryHeaderRead(hdr, &t));

  waveBinaryHeaderWrite(hdr, 9, 100); // unknown type
  TEST_ASSERT_EQUAL_INT32(WaveErrBadBinary, waveBinaryHeaderRead(hdr, &t));
}

// ---------------------------------------------------------------------------
// Resampling and duration conversion
// ---------------------------------------------------------------------------

void test_resample_identity_when_sizes_match() {
  static int16_t ramp[SpwmLutSize];
  static int16_t lut[SpwmLutSize];
  for (uint32_t i = 0; i < SpwmLutSize; i++) {
    ramp[i] = static_cast<int16_t>((static_cast<int32_t>(i) * 65535 / SpwmLutSize) - 32767);
  }
  resampleReference(ramp, SpwmLutSize, lut, SpwmLutSize);
  for (uint32_t i = 0; i < SpwmLutSize; i += 97) {
    TEST_ASSERT_INT16_WITHIN(1, ramp[i], lut[i]);
  }
}

void test_resample_interpolates_and_wraps() {
  const int16_t four[4] = {0, 32767, 0, -32767};
  static int16_t lut[8];
  resampleReference(four, 4, lut, 8);
  TEST_ASSERT_EQUAL_INT16(0, lut[0]);
  TEST_ASSERT_INT16_WITHIN(1, 16383, lut[1]);  // halfway 0 -> +1
  TEST_ASSERT_EQUAL_INT16(32767, lut[2]);
  TEST_ASSERT_INT16_WITHIN(1, -32767, lut[6]);
  TEST_ASSERT_INT16_WITHIN(1, -16383, lut[7]); // halfway -1 -> 0 (wraps to points[0])
}

void test_resample_downsamples_long_input() {
  // 8192-point triangle down to 8 points: peaks must land where expected
  static int16_t tri[8192];
  for (int i = 0; i < 8192; i++) {
    tri[i] = static_cast<int16_t>(i < 4096 ? (i * 32767) / 4096 : ((8192 - i) * 32767) / 4096);
  }
  static int16_t lut[8];
  resampleReference(tri, 8192, lut, 8);
  TEST_ASSERT_EQUAL_INT16(0, lut[0]);
  TEST_ASSERT_INT16_WITHIN(20, 8192, lut[1]);  // quarter of the way up the ramp
  TEST_ASSERT_INT16_WITHIN(20, 16384, lut[2]); // halfway up
  TEST_ASSERT_INT16_WITHIN(20, 32767, lut[4]); // peak at the midpoint
}

void test_micros_to_cycles() {
  TEST_ASSERT_EQUAL_UINT32(30, microsToCycles(1500, 20000));
  TEST_ASSERT_EQUAL_UINT32(1, microsToCycles(10, 20000));
  TEST_ASSERT_EQUAL_UINT32(1, microsToCycles(1, 20000));
  TEST_ASSERT_EQUAL_UINT32(2, microsToCycles(75, 20000));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_reference_basic_with_comments_and_blanks);
  RUN_TEST(test_reference_clamps_out_of_range_levels);
  RUN_TEST(test_reference_errors);
  RUN_TEST(test_incremental_line_feed_matches_whole_text);
  RUN_TEST(test_sequence_basic);
  RUN_TEST(test_sequence_errors_and_limit);
  RUN_TEST(test_binary_header_roundtrip);
  RUN_TEST(test_binary_header_rejects_bad_input);
  RUN_TEST(test_resample_identity_when_sizes_match);
  RUN_TEST(test_resample_interpolates_and_wraps);
  RUN_TEST(test_resample_downsamples_long_input);
  RUN_TEST(test_micros_to_cycles);
  return UNITY_END();
}
