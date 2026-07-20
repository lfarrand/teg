// Tests for the Asymmetric Induction edge-timing math (pwm_timing.h)

#include <unity.h>
#include <pwm_timing.h>

void setUp() {}
void tearDown() {}

// Wrap literals so constexpr callees run at runtime and show up in coverage
static uint32_t rt(uint32_t v) {
  volatile uint32_t x = v;
  return x;
}

void test_100khz_half_duty_reference_case() {
  // 150 MHz bus, 100 kHz, 50% duty (32768/65536), pre 250 ns, post 500 ns
  const AsymmetricTimings t = computeAsymmetricTimings(rt(150000000), rt(100000), 32768, 250, 500, 0xFFFF);

  TEST_ASSERT_EQUAL_UINT8(0, t.prescalerIndex);
  TEST_ASSERT_EQUAL_INT16(0, t.periodStart);
  TEST_ASSERT_EQUAL_INT16(1499, t.periodEnd);  // 1500 ticks per period
  TEST_ASSERT_EQUAL_INT16(0, t.startChanA);
  TEST_ASSERT_EQUAL_INT16(750, t.stopChanA);   // 50% of 1500
  TEST_ASSERT_EQUAL_INT16(712, t.startChanB);  // 750 - round(0.15 * 250) = 750 - 38
  TEST_ASSERT_EQUAL_INT16(1424, t.stopChanB);  // 1499 - round(0.15 * 500) = 1499 - 75
}

void test_quarter_duty() {
  // 10 kHz -> 15000 ticks; 25% duty (16384/65536) -> pulse 3750
  const AsymmetricTimings t = computeAsymmetricTimings(rt(150000000), rt(10000), 16384, 0, 0, 0xFFFF);

  TEST_ASSERT_EQUAL_INT16(14999, t.periodEnd);
  TEST_ASSERT_EQUAL_INT16(3750, t.stopChanA);
  TEST_ASSERT_EQUAL_INT16(3750, t.startChanB); // zero pre-shift: B starts at A's stop
  TEST_ASSERT_EQUAL_INT16(14999, t.stopChanB); // zero post-shift: B stops at period end
}

void test_negative_shifts_move_edges_the_other_way() {
  const AsymmetricTimings t = computeAsymmetricTimings(rt(150000000), rt(100000), 32768, -250, -500, 0xFFFF);

  TEST_ASSERT_EQUAL_INT16(788, t.startChanB);  // 750 + 38
  TEST_ASSERT_EQUAL_INT16(1574, t.stopChanB);  // 1499 + 75 (past period end - caller's concern)
}

void test_prescaler_reported_for_low_frequencies() {
  // 1 kHz needs 150000 ticks at /1 -> prescaler /4 (index 2) to fit 16 bits.
  // NOTE: the VALx values still use the undivided clock (documented quirk kept
  // from the original code) - this test pins the prescaler selection only.
  const AsymmetricTimings t = computeAsymmetricTimings(rt(150000000), rt(1000), 32768, 0, 0, 0xFFFF);
  TEST_ASSERT_EQUAL_UINT8(2, t.prescalerIndex);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_100khz_half_duty_reference_case);
  RUN_TEST(test_quarter_duty);
  RUN_TEST(test_negative_shifts_move_edges_the_other_way);
  RUN_TEST(test_prescaler_reported_for_low_frequencies);
  return UNITY_END();
}
