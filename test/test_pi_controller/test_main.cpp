// Tests for the closed-loop PI controller (pi_controller.h)

#include <unity.h>
#include <pi_controller.h>

void setUp() {}
void tearDown() {}

void test_proportional_path() {
  PiController c;
  c.kp = 0.5f;
  c.ki = 0.0f;
  c.outMax = 10.0f;
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, piUpdate(c, 2.0f, 0.001f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, piUpdate(c, -2.0f, 0.001f)); // clamped at outMin
}

void test_integral_accumulates() {
  PiController c;
  c.ki = 1.0f;
  c.outMax = 10.0f;
  float out = 0.0f;
  for (int i = 0; i < 1000; i++) {
    out = piUpdate(c, 1.0f, 0.001f); // 1V error for 1 second total
  }
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, out);
}

void test_anti_windup_clamps_integrator() {
  PiController c;
  c.ki = 100.0f;
  c.outMax = 1.155f;
  for (int i = 0; i < 10000; i++) {
    piUpdate(c, 5.0f, 0.001f); // huge sustained error
  }
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.155f, c.integrator); // clamped, not wound up
  // Recovery is immediate once the error reverses, not delayed by unwinding
  const float out = piUpdate(c, -1.0f, 0.001f);
  TEST_ASSERT_LESS_THAN_FLOAT(1.155f, out);
}

void test_closed_loop_converges_on_first_order_plant() {
  // plant: y += (out - y) * 0.1 per tick; setpoint 0.8
  PiController c;
  c.kp = 0.5f;
  c.ki = 20.0f;
  c.outMax = 1.155f;
  float y = 0.0f;
  for (int i = 0; i < 5000; i++) {
    const float out = piUpdate(c, 0.8f - y, 0.001f);
    y += (out - y) * 0.1f;
  }
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.8f, y);
}

void test_reset() {
  PiController c;
  c.ki = 1.0f;
  piUpdate(c, 5.0f, 1.0f);
  piReset(c);
  TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, c.integrator);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_proportional_path);
  RUN_TEST(test_integral_accumulates);
  RUN_TEST(test_anti_windup_clamps_integrator);
  RUN_TEST(test_closed_loop_converges_on_first_order_plant);
  RUN_TEST(test_reset);
  return UNITY_END();
}
