// Tests for the thermal derating curve (thermal_math.h)

#include <unity.h>
#include <thermal_math.h>

void setUp() {}
void tearDown() {}

void test_no_derating_at_or_below_start() {
  TEST_ASSERT_EQUAL_UINT16(1000, thermalDerateMilli(25.0f, 70.0f, 90.0f));
  TEST_ASSERT_EQUAL_UINT16(1000, thermalDerateMilli(70.0f, 70.0f, 90.0f));
  TEST_ASSERT_EQUAL_UINT16(1000, thermalDerateMilli(-10.0f, 70.0f, 90.0f));
}

void test_full_cutoff_at_or_above_end() {
  TEST_ASSERT_EQUAL_UINT16(0, thermalDerateMilli(90.0f, 70.0f, 90.0f));
  TEST_ASSERT_EQUAL_UINT16(0, thermalDerateMilli(150.0f, 70.0f, 90.0f));
}

void test_linear_between() {
  TEST_ASSERT_EQUAL_UINT16(500, thermalDerateMilli(80.0f, 70.0f, 90.0f)); // midpoint
  TEST_ASSERT_EQUAL_UINT16(750, thermalDerateMilli(75.0f, 70.0f, 90.0f));
  TEST_ASSERT_EQUAL_UINT16(250, thermalDerateMilli(85.0f, 70.0f, 90.0f));
  // Monotonically non-increasing across the window
  uint16_t prev = 1000;
  for (float tc = 69.0f; tc <= 91.0f; tc += 0.5f) {
    const uint16_t d = thermalDerateMilli(tc, 70.0f, 90.0f);
    TEST_ASSERT_LESS_OR_EQUAL_UINT16(prev, d);
    prev = d;
  }
}

void test_degenerate_window_is_hard_cutoff() {
  TEST_ASSERT_EQUAL_UINT16(1000, thermalDerateMilli(69.9f, 70.0f, 70.0f));
  TEST_ASSERT_EQUAL_UINT16(0, thermalDerateMilli(70.1f, 70.0f, 70.0f));
  TEST_ASSERT_EQUAL_UINT16(0, thermalDerateMilli(75.0f, 70.0f, 60.0f)); // end < start
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_no_derating_at_or_below_start);
  RUN_TEST(test_full_cutoff_at_or_above_end);
  RUN_TEST(test_linear_between);
  RUN_TEST(test_degenerate_window_is_hard_cutoff);
  return UNITY_END();
}
