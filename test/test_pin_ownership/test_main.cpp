#include <unity.h>
#include <pin_ownership.h>

void setUp() {}
void tearDown() {}

void test_safe_defaults_and_intentional_current_sensor_share() {
  MainConfig cfg;
  TEST_ASSERT_TRUE(validatePinOwnership(cfg));
  cfg.Capture.Enabled = true;
  cfg.Meter.Enabled = true;
  cfg.CurrentLimit.Enabled = true;
  TEST_ASSERT_TRUE(validatePinOwnership(cfg)); // pin 40 ADC + ACMP is intentional
}

void test_rejects_remuxing_pwm_and_fixed_bus_pins() {
  MainConfig cfg;
  cfg.PowerMon.Enabled = true;
  cfg.PowerMon.PgEfusePin = 4;
  PinValidationResult r;
  TEST_ASSERT_FALSE(validatePinOwnership(cfg, &r));
  TEST_ASSERT_EQUAL_UINT8(4, r.pin);
  TEST_ASSERT_EQUAL_UINT16(PinRolePwm, r.existing);

  cfg = MainConfig{};
  cfg.FaultProtection.Enabled = true;
  cfg.FaultProtection.Pin = 18;
  TEST_ASSERT_FALSE(validatePinOwnership(cfg, &r));
  TEST_ASSERT_EQUAL_UINT16(PinRoleWire, r.existing);
}

void test_rejects_non_analog_and_pin_42() {
  MainConfig cfg;
  cfg.Capture.Enabled = true;
  cfg.Feedback.AnalogPin = 32;
  TEST_ASSERT_FALSE(validatePinOwnership(cfg));
  cfg.Feedback.AnalogPin = 42;
  TEST_ASSERT_FALSE(validatePinOwnership(cfg));

  cfg = MainConfig{};
  cfg.PowerMon.Enabled = true;
  cfg.PowerMon.ImonPin = 42;
  TEST_ASSERT_FALSE(validatePinOwnership(cfg));
}

void test_rejects_conflicts_between_configurable_features() {
  MainConfig cfg;
  cfg.FaultProtection.Enabled = true;
  cfg.FaultProtection.Pin = 21;
  cfg.Thermal.Enabled = true;
  cfg.Thermal.OneWirePin = 21;
  TEST_ASSERT_FALSE(validatePinOwnership(cfg));

  cfg = MainConfig{};
  cfg.Capture.Enabled = true;
  cfg.Feedback.AnalogPin = 24;
  cfg.PowerMon.Enabled = true;
  TEST_ASSERT_FALSE(validatePinOwnership(cfg));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_safe_defaults_and_intentional_current_sensor_share);
  RUN_TEST(test_rejects_remuxing_pwm_and_fixed_bus_pins);
  RUN_TEST(test_rejects_non_analog_and_pin_42);
  RUN_TEST(test_rejects_conflicts_between_configurable_features);
  return UNITY_END();
}
