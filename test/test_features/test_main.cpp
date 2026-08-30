// Defaults for compile-time feature flags (teg_features.h)

#include <unity.h>
#include <teg_features.h>

void setUp() {}
void tearDown() {}

void test_teg_with_oled_defaults_on() {
  TEST_ASSERT_EQUAL(1, TEG_WITH_OLED);
}

void test_teg_with_thermal_defaults_on() {
  TEST_ASSERT_EQUAL(1, TEG_WITH_THERMAL);
}

void test_teg_with_mqtt_defaults_on() {
  TEST_ASSERT_EQUAL(1, TEG_WITH_MQTT);
}

void test_teg_with_influx_defaults_on() {
  TEST_ASSERT_EQUAL(1, TEG_WITH_INFLUX);
}

void test_teg_with_spectrum_defaults_on() {
  TEST_ASSERT_EQUAL(1, TEG_WITH_SPECTRUM);
}

void test_teg_with_powermon_defaults_on() {
  TEST_ASSERT_EQUAL(1, TEG_WITH_POWERMON);
}

void test_teg_with_mtp_service_defaults_on() {
  TEST_ASSERT_EQUAL(1, TEG_WITH_MTP_SERVICE);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_teg_with_oled_defaults_on);
  RUN_TEST(test_teg_with_thermal_defaults_on);
  RUN_TEST(test_teg_with_mqtt_defaults_on);
  RUN_TEST(test_teg_with_influx_defaults_on);
  RUN_TEST(test_teg_with_spectrum_defaults_on);
  RUN_TEST(test_teg_with_powermon_defaults_on);
  RUN_TEST(test_teg_with_mtp_service_defaults_on);
  return UNITY_END();
}
