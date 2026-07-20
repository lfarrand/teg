// Host-native unit tests for spwm_math.h (run with: pio test -e native)

#include <unity.h>
#include <spwm_math.h>

static uint16_t lut[SpwmLutSize];

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// DDS tuning word
// ---------------------------------------------------------------------------

void test_phase_increment_exact_ratio() {
  // 50 Hz on 20 kHz: increment = floor(50 * 2^32 / 20000)
  TEST_ASSERT_EQUAL_UINT32(10737418U, spwmPhaseIncrement(20000, 50));
  // 400 samples/cycle: 400 * increment must land within one LSB of a full cycle
  const uint64_t fullCycle = 400ULL * spwmPhaseIncrement(20000, 50);
  TEST_ASSERT_UINT64_WITHIN(400, 1ULL << 32, fullCycle);
}

void test_phase_increment_non_integer_ratio() {
  // 60 Hz on 20 kHz (ratio 333.33) — the old integer-sample scheme quantized
  // this to 60.06 Hz; the DDS must stay within tuning resolution (~4.7 uHz)
  const uint32_t inc = spwmPhaseIncrement(20000, 60);
  const uint64_t milliHz = spwmActualMilliHz(inc, 20000);
  TEST_ASSERT_UINT64_WITHIN(1, 60000, milliHz); // 60.000 Hz +/- 1 mHz
}

void test_phase_increment_zero_guards() {
  TEST_ASSERT_EQUAL_UINT32(0, spwmPhaseIncrement(0, 50));
  TEST_ASSERT_EQUAL_UINT32(0, spwmPhaseIncrement(20000, 0));
}

void test_phase_accumulator_wraps_seamlessly() {
  // Crossing the 2^32 boundary must be an ordinary step, not a discontinuity
  const uint32_t inc = spwmPhaseIncrement(20000, 50);
  uint32_t phase = 0U - inc / 2; // just below the wrap
  const uint16_t before = spwmDutyFromPhase(lut, phase);
  phase += inc; // wraps
  const uint16_t after = spwmDutyFromPhase(lut, phase);
  // The wrap step must look like any other step: at 400 samples/cycle the
  // largest per-sample change (at the zero crossing) is 32767*2*pi/400 ~= 515
  TEST_ASSERT_INT_WITHIN(520, before, after);
}

// ---------------------------------------------------------------------------
// Sine LUT
// ---------------------------------------------------------------------------

void test_lut_key_points() {
  TEST_ASSERT_EQUAL_UINT16(32768, lut[0]);                    // sin(0)
  TEST_ASSERT_EQUAL_UINT16(65535, lut[SpwmLutSize / 4]);      // +peak
  TEST_ASSERT_EQUAL_UINT16(32768, lut[SpwmLutSize / 2]);      // sin(pi)
  TEST_ASSERT_EQUAL_UINT16(1, lut[3 * SpwmLutSize / 4]);      // -peak
}

void test_lut_bounds() {
  for (uint32_t i = 0; i < SpwmLutSize; i++) {
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(1, lut[i]);
    // <= 65535 is implied by the type; the >= 1 bound is what keeps
    // channel B's complement (65536 - dutyA) inside uint16 range
  }
}

void test_lut_half_cycle_symmetry() {
  // sin(x + pi) = -sin(x): entries half a cycle apart must complement to 65536
  for (uint32_t i = 0; i < SpwmLutSize / 2; i++) {
    const uint32_t sum = (uint32_t)lut[i] + (uint32_t)lut[i + SpwmLutSize / 2];
    TEST_ASSERT_EQUAL_UINT32(65536, sum);
  }
}

// ---------------------------------------------------------------------------
// Phase -> duty (index + interpolation)
// ---------------------------------------------------------------------------

void test_duty_entry_aligned_phase_hits_table() {
  for (uint32_t idx = 0; idx < SpwmLutSize; idx += 37) {
    const uint32_t phase = idx << (32 - SpwmLutBits);
    TEST_ASSERT_EQUAL_UINT16(lut[idx], spwmDutyFromPhase(lut, phase));
  }
}

void test_duty_interpolation_bounded_by_neighbours() {
  for (uint32_t idx = 0; idx < SpwmLutSize; idx += 13) {
    const uint16_t a = lut[idx];
    const uint16_t b = lut[(idx + 1) & (SpwmLutSize - 1)];
    const uint16_t lo = a < b ? a : b;
    const uint16_t hi = a < b ? b : a;
    for (uint32_t frac = 0; frac < 256; frac += 51) {
      const uint32_t phase = (idx << (32 - SpwmLutBits)) | (frac << (32 - SpwmLutBits - 8));
      const uint16_t duty = spwmDutyFromPhase(lut, phase);
      TEST_ASSERT_GREATER_OR_EQUAL_UINT16(lo, duty);
      TEST_ASSERT_LESS_OR_EQUAL_UINT16(hi, duty);
    }
  }
}

void test_duty_monotone_over_rising_quarter() {
  // First quarter cycle of a sine is non-decreasing
  uint16_t prev = 0;
  for (uint32_t phase = 0; phase < (1UL << 30); phase += (1UL << 30) / 999) {
    const uint16_t duty = spwmDutyFromPhase(lut, phase);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(prev, duty);
    prev = duty;
  }
}

void test_channel_b_complement_in_range() {
  const uint32_t inc = spwmPhaseIncrement(20000, 50);
  uint32_t phase = 0;
  for (int i = 0; i < 400; i++) {
    const uint16_t dutyA = spwmDutyFromPhase(lut, phase);
    const uint16_t dutyB = (uint16_t)(65535U - dutyA); // ISR complement (modulationFinalDuty)
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(1, dutyA);
    TEST_ASSERT_EQUAL_UINT32(65535, (uint32_t)dutyA + (uint32_t)dutyB);
    phase += inc;
  }
}

// ---------------------------------------------------------------------------
// Prescaler selection (F_BUS = 150 MHz on a 600 MHz Teensy 4.1)
// ---------------------------------------------------------------------------

void test_prescaler_typical_frequencies() {
  TEST_ASSERT_EQUAL_UINT8(0, bestPrescalerIndex(150000000, 20000, 0xFFFF)); // 7500 ticks
  TEST_ASSERT_EQUAL_UINT8(0, bestPrescalerIndex(150000000, 2289, 0xFFFF));  // 65531 ticks, just fits
  TEST_ASSERT_EQUAL_UINT8(1, bestPrescalerIndex(150000000, 2288, 0xFFFF));  // 65559 ticks, needs /2
  TEST_ASSERT_EQUAL_UINT8(5, bestPrescalerIndex(150000000, 100, 0xFFFF));   // /32 -> 46875 ticks
  TEST_ASSERT_EQUAL_UINT8(7, bestPrescalerIndex(150000000, 30, 0xFFFF));    // /128 -> 39062 ticks
}

void test_prescaler_clamps_at_max() {
  TEST_ASSERT_EQUAL_UINT8(7, bestPrescalerIndex(150000000, 1, 0xFFFF)); // can't fit; clamp
  TEST_ASSERT_EQUAL_UINT8(7, bestPrescalerIndex(150000000, 0, 0xFFFF)); // guard
}

// ---------------------------------------------------------------------------

int main() {
  fillSpwmLut(lut, SpwmLutSize);

  UNITY_BEGIN();
  RUN_TEST(test_phase_increment_exact_ratio);
  RUN_TEST(test_phase_increment_non_integer_ratio);
  RUN_TEST(test_phase_increment_zero_guards);
  RUN_TEST(test_phase_accumulator_wraps_seamlessly);
  RUN_TEST(test_lut_key_points);
  RUN_TEST(test_lut_bounds);
  RUN_TEST(test_lut_half_cycle_symmetry);
  RUN_TEST(test_duty_entry_aligned_phase_hits_table);
  RUN_TEST(test_duty_interpolation_bounded_by_neighbours);
  RUN_TEST(test_duty_monotone_over_rising_quarter);
  RUN_TEST(test_channel_b_complement_in_range);
  RUN_TEST(test_prescaler_typical_frequencies);
  RUN_TEST(test_prescaler_clamps_at_max);
  return UNITY_END();
}
