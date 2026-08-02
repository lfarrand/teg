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

  TEST_ASSERT_TRUE(t.valid);
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

  TEST_ASSERT_TRUE(t.valid);
  TEST_ASSERT_EQUAL_INT16(14999, t.periodEnd);
  TEST_ASSERT_EQUAL_INT16(3750, t.stopChanA);
  TEST_ASSERT_EQUAL_INT16(3750, t.startChanB); // zero pre-shift: B starts at A's stop
  TEST_ASSERT_EQUAL_INT16(14999, t.stopChanB); // zero post-shift: B stops at period end
}

void test_negative_shifts_move_edges_the_other_way() {
  const AsymmetricTimings t = computeAsymmetricTimings(rt(150000000), rt(100000), 32768, -250, -500, 0xFFFF);

  // stop B would be 1574 while VAL1 is 1499. The compare can never be reached,
  // so the timing helper must refuse the entire waveform instead of narrowing it.
  TEST_ASSERT_FALSE(t.valid);
}

void test_low_frequency_uses_prescaled_clock() {
  // 1 kHz needs 150000 ticks at /1 -> prescaler /4 (index 2), so the counter
  // runs at 37.5 MHz and all VALx values must be in 37.5 MHz ticks
  const AsymmetricTimings t = computeAsymmetricTimings(rt(150000000), rt(1000), 32768, 250, 500, 0xFFFF);

  TEST_ASSERT_TRUE(t.valid);
  TEST_ASSERT_EQUAL_UINT8(2, t.prescalerIndex);
  TEST_ASSERT_EQUAL_INT16(37499, t.periodEnd);  // 37500 ticks per period, fits 16 bits
  TEST_ASSERT_EQUAL_INT16(18750, t.stopChanA);  // 50% duty
  TEST_ASSERT_EQUAL_INT16(18741, t.startChanB); // 18750 - round(0.0375 * 250) = 18750 - 9
  TEST_ASSERT_EQUAL_INT16(37480, t.stopChanB);  // 37499 - round(0.0375 * 500) = 37499 - 19
}

void test_rejects_zero_width_and_out_of_period_edges() {
  TEST_ASSERT_FALSE(computeAsymmetricTimings(rt(150000000), rt(100000), 0, 0, 0,
                                             0xFFFF).valid);
  TEST_ASSERT_FALSE(computeAsymmetricTimings(rt(150000000), rt(100000), 32768,
                                             6000, 0, 0xFFFF).valid);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_100khz_half_duty_reference_case);
  RUN_TEST(test_quarter_duty);
  RUN_TEST(test_negative_shifts_move_edges_the_other_way);
  RUN_TEST(test_low_frequency_uses_prescaled_clock);
  RUN_TEST(test_rejects_zero_width_and_out_of_period_edges);
  return UNITY_END();
}
