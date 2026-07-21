// Tests for the teg-wave v1 custom waveform parser (waveform_parse.h)

#include <unity.h>
#include <stdio.h>
#include <waveform_parse.h>
#include <spwm_math.h>

static float pts[MaxWavePoints];
static int16_t levels[MaxWaveSegments];
static uint32_t micros[MaxWaveSegments];
static uint8_t type;

void setUp() { type = WaveTypeNone; }
void tearDown() {}

static int32_t parse(const char *text) {
  return parseWaveform(text, pts, MaxWavePoints, levels, micros, MaxWaveSegments, &type);
}

// ---------------------------------------------------------------------------
// Reference files
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
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, pts[0]);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, pts[1]);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, pts[2]);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.25f, pts[3]);
}

void test_reference_clamps_out_of_range_levels() {
  TEST_ASSERT_EQUAL_INT32(2, parse("type=reference\n5.0\n-3.5\n"));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, pts[0]);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -1.0f, pts[1]);
}

void test_reference_errors() {
  TEST_ASSERT_EQUAL_INT32(WaveErrNoType, parse("0.5\n0.7\n"));       // no type line
  TEST_ASSERT_EQUAL_INT32(WaveErrTooFew, parse("type=reference\n0.5\n")); // 1 point
  TEST_ASSERT_EQUAL_INT32(WaveErrBadValue, parse("type=reference\n0.1\nbanana\n"));
}

// ---------------------------------------------------------------------------
// Sequence files
// ---------------------------------------------------------------------------

void test_sequence_basic() {
  const int32_t n = parse(
    "# burst pattern\n"
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
  TEST_ASSERT_EQUAL_UINT32(500, micros[1]);
  TEST_ASSERT_EQUAL_UINT32(300, micros[2]);
}

void test_sequence_errors() {
  TEST_ASSERT_EQUAL_INT32(WaveErrBadDuration, parse("type=sequence\n1.0\n"));      // no duration
  TEST_ASSERT_EQUAL_INT32(WaveErrBadDuration, parse("type=sequence\n1.0, 0\n"));   // zero duration
  TEST_ASSERT_EQUAL_INT32(WaveErrBadValue, parse("type=sequence\nfoo, 100\n"));
  TEST_ASSERT_EQUAL_INT32(WaveErrTooFew, parse("type=sequence\n# nothing\n"));
}

void test_sequence_limit() {
  char big[4096] = "type=sequence\n";
  char *p = big + strlen(big);
  for (int i = 0; i < 65; i++) { // one more than MaxWaveSegments
    p += sprintf(p, "0.5, 100\n");
  }
  TEST_ASSERT_EQUAL_INT32(WaveErrTooMany, parse(big));
}

// ---------------------------------------------------------------------------
// Resampling and duration conversion
// ---------------------------------------------------------------------------

void test_resample_identity_when_sizes_match() {
  static float ramp[SpwmLutSize];
  static int16_t lut[SpwmLutSize];
  for (uint32_t i = 0; i < SpwmLutSize; i++) {
    ramp[i] = (static_cast<float>(i) / SpwmLutSize) * 2.0f - 1.0f;
  }
  resampleReference(ramp, SpwmLutSize, lut, SpwmLutSize);
  for (uint32_t i = 0; i < SpwmLutSize; i += 97) {
    TEST_ASSERT_INT16_WITHIN(1, static_cast<int16_t>(ramp[i] * 32767.0f), lut[i]);
  }
}

void test_resample_interpolates_and_wraps() {
  const float four[4] = {0.0f, 1.0f, 0.0f, -1.0f};
  static int16_t lut[8];
  resampleReference(four, 4, lut, 8);
  TEST_ASSERT_EQUAL_INT16(0, lut[0]);
  TEST_ASSERT_INT16_WITHIN(1, 16383, lut[1]);  // halfway 0 -> 1
  TEST_ASSERT_EQUAL_INT16(32767, lut[2]);
  TEST_ASSERT_INT16_WITHIN(1, -32767, lut[6]);
  TEST_ASSERT_INT16_WITHIN(1, -16383, lut[7]); // halfway -1 -> 0 (wraps to pts[0])
}

void test_micros_to_cycles() {
  TEST_ASSERT_EQUAL_UINT32(30, microsToCycles(1500, 20000)); // 1500us at 50us/cycle
  TEST_ASSERT_EQUAL_UINT32(1, microsToCycles(10, 20000));    // rounds up from 0.2, min 1
  TEST_ASSERT_EQUAL_UINT32(1, microsToCycles(1, 20000));     // never zero
  TEST_ASSERT_EQUAL_UINT32(2, microsToCycles(75, 20000));    // 1.5 rounds to 2
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_reference_basic_with_comments_and_blanks);
  RUN_TEST(test_reference_clamps_out_of_range_levels);
  RUN_TEST(test_reference_errors);
  RUN_TEST(test_sequence_basic);
  RUN_TEST(test_sequence_errors);
  RUN_TEST(test_sequence_limit);
  RUN_TEST(test_resample_identity_when_sizes_match);
  RUN_TEST(test_resample_interpolates_and_wraps);
  RUN_TEST(test_micros_to_cycles);
  return UNITY_END();
}
