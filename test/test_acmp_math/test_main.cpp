// Tests for the ACMP overcurrent math and routing tables (acmp_math.h)

#include <unity.h>
#include <acmp_math.h>

void setUp() {}
void tearDown() {}

void test_dac_code_round_trip() {
  // Every code maps to a distinct millivolt value that converts back exactly
  for (uint8_t code = 0; code < 64; code++) {
    TEST_ASSERT_EQUAL_UINT8(code, acmpThresholdToDacCode(acmpDacCodeToMv(code)));
  }
}

void test_dac_code_monotonic() {
  uint8_t prev = 0;
  for (uint16_t mv = 0; mv <= 3300; mv += 25) {
    const uint8_t code = acmpThresholdToDacCode(mv);
    TEST_ASSERT_TRUE(code >= prev);
    prev = code;
  }
}

void test_dac_code_edges() {
  TEST_ASSERT_EQUAL_UINT8(0, acmpThresholdToDacCode(0));
  TEST_ASSERT_EQUAL_UINT8(63, acmpThresholdToDacCode(3300));
  TEST_ASSERT_EQUAL_UINT8(63, acmpThresholdToDacCode(65535)); // clamped
  // Default threshold 2475mV lands exactly on code 47: (47+1)*3300/64 = 2475
  TEST_ASSERT_EQUAL_UINT8(47, acmpThresholdToDacCode(2475));
  TEST_ASSERT_EQUAL_UINT16(2475, acmpDacCodeToMv(47));
  // Mid-rail 1650mV = code 31 exactly
  TEST_ASSERT_EQUAL_UINT8(31, acmpThresholdToDacCode(1650));
  TEST_ASSERT_EQUAL_UINT16(1650, acmpDacCodeToMv(31));
  TEST_ASSERT_EQUAL_UINT16(3300, acmpDacCodeToMv(63));
  TEST_ASSERT_EQUAL_UINT16(3300, acmpDacCodeToMv(200)); // clamped code
  TEST_ASSERT_EQUAL_UINT8(0, acmpThresholdToDacCode(1000, 0)); // zero vin guard
}

void test_dac_code_is_nearest() {
  // Round-trip tests are blind to a systematic bias (a ceil-based mapping
  // would also round-trip); pin the NEAREST-code behavior at step boundaries.
  // threshold(code) = (code+1)*3300/64: code0 = 51.56mV, code1 = 103.1mV,
  // so the 0/1 boundary sits at 77.3mV and the 1/2 boundary at 128.9mV.
  TEST_ASSERT_EQUAL_UINT8(0, acmpThresholdToDacCode(77));
  TEST_ASSERT_EQUAL_UINT8(1, acmpThresholdToDacCode(78));
  TEST_ASSERT_EQUAL_UINT8(1, acmpThresholdToDacCode(128));
  TEST_ASSERT_EQUAL_UINT8(2, acmpThresholdToDacCode(129));
  // 2500mV requested: code 47 = 2475 is 25 away, code 48 = 2526.6 is 26.6
  // away -> nearest is 47
  TEST_ASSERT_EQUAL_UINT8(47, acmpThresholdToDacCode(2500));
  // The programmed threshold is never more than half a step (25.8mV) off
  for (uint16_t mv = 100; mv <= 3300; mv += 7) {
    const int32_t actual = acmpDacCodeToMv(acmpThresholdToDacCode(mv));
    int32_t err = actual - mv;
    if (err < 0) err = -err;
    // Below code 0's 51.6mV floor the error can exceed half a step; the
    // config validator floors thresholds at 100mV so start there
    TEST_ASSERT_TRUE(err <= 26);
  }
}

void test_pin_routing_table() {
  // Spot-check every entry against RM rev4 Table 10-1
  struct { uint8_t pin, cmp, psel; } expect[] = {
    {18, 1, 0}, {17, 1, 1}, {25, 1, 2}, {14, 1, 3}, {16, 1, 5}, {21, 1, 6},
    {15, 2, 3}, {38, 2, 6},
    {40, 3, 3}, {1, 3, 4}, {39, 3, 6},
    {19, 4, 2}, {41, 4, 3}, {0, 4, 4}, {20, 4, 5}, {26, 4, 6},
  };
  for (unsigned i = 0; i < sizeof(expect) / sizeof(expect[0]); i++) {
    const AcmpRoute r = acmpRouteForPin(expect[i].pin);
    TEST_ASSERT_EQUAL_UINT8(expect[i].cmp, r.cmp);
    TEST_ASSERT_EQUAL_UINT8(expect[i].psel, r.psel);
  }
}

void test_non_acmp_pins_rejected() {
  // 22/23 are comparator-capable silicon but are this firmware's Sm40/Sm41
  // PWM outputs - claiming one would re-mux the pad away from FlexPWM4
  const uint8_t bad[] = {2, 13, 22, 23, 24, 27, 28, 32, 42, 255};
  for (unsigned i = 0; i < sizeof(bad); i++) {
    TEST_ASSERT_EQUAL_UINT8(0, acmpRouteForPin(bad[i]).cmp);
  }
}

void test_xbar_input_indices() {
  // Mirrors XBARA1_IN_ACMP1_OUT..ACMP4_OUT = 26..29
  TEST_ASSERT_EQUAL_UINT8(26, acmpXbarInputForCmp(1));
  TEST_ASSERT_EQUAL_UINT8(29, acmpXbarInputForCmp(4));
}

void test_filter_glitch_time() {
  // 4 samples x 15 clocks at 150MHz = 400ns recognition window
  TEST_ASSERT_EQUAL_UINT32(400, acmpFilterGlitchNanos(4, 15, 150000000));
  // Bypass semantics: either parameter zero = no filter
  TEST_ASSERT_EQUAL_UINT32(0, acmpFilterGlitchNanos(0, 15, 150000000));
  TEST_ASSERT_EQUAL_UINT32(0, acmpFilterGlitchNanos(4, 0, 150000000));
  TEST_ASSERT_EQUAL_UINT32(0, acmpFilterGlitchNanos(4, 15, 0));
  // Max settings stay sane: 7 x 255 at 150MHz = 11.9us
  TEST_ASSERT_EQUAL_UINT32(11900, acmpFilterGlitchNanos(7, 255, 150000000));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_dac_code_round_trip);
  RUN_TEST(test_dac_code_monotonic);
  RUN_TEST(test_dac_code_edges);
  RUN_TEST(test_dac_code_is_nearest);
  RUN_TEST(test_pin_routing_table);
  RUN_TEST(test_non_acmp_pins_rejected);
  RUN_TEST(test_xbar_input_indices);
  RUN_TEST(test_filter_glitch_time);
  return UNITY_END();
}
