// Tests for the aux power monitor scaling (power_monitor_math.h): INA226
// register math and the TPS25983 IMON conversion, checked against the
// datasheet worked examples and the driver board's actual component values
// (R24 = 10 mOhm shunt, R_IMON = 4.53 kOhm, GIMON = 243 uA/A).

#include <unity.h>
#include <power_monitor_math.h>

void setUp() {}
void tearDown() {}

// --------------------------------------------------------------------------
// Calibration register: CAL = 0.00512 / (Current_LSB * Rshunt)
// --------------------------------------------------------------------------

void test_cal_register_driver_board_values() {
  // 10 mOhm, 50 uA/bit -> 0.00512 / (50e-6 * 0.01) = 10240 exactly
  TEST_ASSERT_EQUAL_UINT32(10240, ina226CalRegister(10000, 50));
  TEST_ASSERT_TRUE(ina226CalValid(10000, 50));
}

void test_cal_register_other_known_points() {
  // Datasheet section 8.2.2 example: 2 mOhm, 1 mA/bit -> 2560
  TEST_ASSERT_EQUAL_UINT32(2560, ina226CalRegister(2000, 1000));
  // 100 mOhm, 100 uA/bit -> 512
  TEST_ASSERT_EQUAL_UINT32(512, ina226CalRegister(100000, 100));
}

void test_cal_register_invalid_inputs() {
  TEST_ASSERT_EQUAL_UINT32(0, ina226CalRegister(0, 50));
  TEST_ASSERT_EQUAL_UINT32(0, ina226CalRegister(10000, 0));
  TEST_ASSERT_FALSE(ina226CalValid(0, 50));
  TEST_ASSERT_FALSE(ina226CalValid(10000, 0));
  // 1 uOhm at 1 uA/bit -> CAL far above 16 bits: must be rejected, not
  // silently truncated into a wrong scale
  TEST_ASSERT_FALSE(ina226CalValid(1, 1));
  // Extremely large product -> CAL divides to 0: also invalid
  TEST_ASSERT_FALSE(ina226CalValid(4000000000u, 65535));
}

// --------------------------------------------------------------------------
// Register -> engineering units
// --------------------------------------------------------------------------

void test_bus_voltage_scaling() {
  TEST_ASSERT_EQUAL_UINT32(0, ina226BusMv(0));
  // 1.25 mV/count: 4 counts = 5 mV exactly
  TEST_ASSERT_EQUAL_UINT32(5, ina226BusMv(4));
  // 24 V bus -> 19200 counts
  TEST_ASSERT_EQUAL_UINT32(24000, ina226BusMv(19200));
  // Full scale (0x7FFF positive range per datasheet): 32767 * 1.25 = 40958.75
  TEST_ASSERT_EQUAL_UINT32(40959, ina226BusMv(32767));
}

void test_current_scaling_signed() {
  // 50 uA/bit: 20000 counts = 1.000 A
  TEST_ASSERT_EQUAL_INT32(1000, ina226CurrentMa(20000, 50));
  TEST_ASSERT_EQUAL_INT32(-1000, ina226CurrentMa(-20000, 50));
  TEST_ASSERT_EQUAL_INT32(0, ina226CurrentMa(0, 50));
  // Rounding: 9 counts = 450 uA -> 0 mA; 11 counts = 550 uA -> 1 mA
  TEST_ASSERT_EQUAL_INT32(0, ina226CurrentMa(9, 50));
  TEST_ASSERT_EQUAL_INT32(1, ina226CurrentMa(11, 50));
  TEST_ASSERT_EQUAL_INT32(-1, ina226CurrentMa(-11, 50));
}

void test_power_scaling() {
  // Power LSB = 25 * 50 uA = 1.25 mW/count
  TEST_ASSERT_EQUAL_UINT32(0, ina226PowerMw(0, 50));
  TEST_ASSERT_EQUAL_UINT32(5000, ina226PowerMw(4000, 50));
  // 7.6 W (worst-case aux draw at the Murata's power ceiling) -> 6080 counts
  TEST_ASSERT_EQUAL_UINT32(7600, ina226PowerMw(6080, 50));
  // Consistency: P from the power register equals V*I at a matched operating
  // point. 24 V, 0.3 A -> 7.2 W = 5760 power counts at 1.25 mW/bit
  const uint32_t busMv = ina226BusMv(19200);
  const int32_t currentMa = ina226CurrentMa(6000, 50);
  TEST_ASSERT_EQUAL_UINT32(24000, busMv);
  TEST_ASSERT_EQUAL_INT32(300, currentMa);
  TEST_ASSERT_EQUAL_UINT32(7200, ina226PowerMw(5760, 50));
}

// --------------------------------------------------------------------------
// Alert limit
// --------------------------------------------------------------------------

void test_alert_limit_counts() {
  // 1.5 A on 10 mOhm = 15 mV shunt = 6000 counts of 2.5 uV
  TEST_ASSERT_EQUAL_UINT16(6000, ina226AlertLimitCounts(1500, 10000));
  // 0 mA -> 0 counts (caller treats 0 mA as "alert disabled" before here)
  TEST_ASSERT_EQUAL_UINT16(0, ina226AlertLimitCounts(0, 10000));
  // Clamped rather than wrapped when the request exceeds the register
  TEST_ASSERT_EQUAL_UINT16(0xFFFF, ina226AlertLimitCounts(2000000, 100000));
}

// --------------------------------------------------------------------------
// TPS25983 IMON channel
// --------------------------------------------------------------------------

void test_imon_scaling_driver_board_values() {
  // R_IMON = 4.53k, G = 243 uA/A -> 1.1008 V/A; a 12-bit 3.3 V ADC reads
  // ~2998 mA at full scale (the design's 3 A headroom above the ~2.1 A ILIM)
  TEST_ASSERT_EQUAL_INT32(0, imonMilliAmp(0, 4095, 3300, 4530, 243));
  TEST_ASSERT_EQUAL_INT32(2998, imonMilliAmp(4095, 4095, 3300, 4530, 243));
  // Mid-scale: 2048 counts -> ~1.4996 A
  const int32_t mid = imonMilliAmp(2048, 4095, 3300, 4530, 243);
  TEST_ASSERT_INT32_WITHIN(2, 1500, mid);
  // One count is ~0.73 mA: monotonic, no dead zone
  TEST_ASSERT_EQUAL_INT32(1, imonMilliAmp(1, 4095, 3300, 4530, 243));
}

void test_imon_invalid_divisor() {
  TEST_ASSERT_EQUAL_INT32(0, imonMilliAmp(2048, 4095, 3300, 0, 243));
  TEST_ASSERT_EQUAL_INT32(0, imonMilliAmp(2048, 4095, 3300, 4530, 0));
  TEST_ASSERT_EQUAL_INT32(0, imonMilliAmp(2048, 0, 3300, 4530, 243));
}

// --------------------------------------------------------------------------
// Fixed register constants
// --------------------------------------------------------------------------

void test_config_register_fields() {
  // 0x4927 = AVG 16 (100b), VBUSCT 1.1ms (100b), VSHCT 1.1ms (100b),
  // mode continuous shunt+bus (111b) - decode the fields rather than
  // trusting the assembled literal
  TEST_ASSERT_EQUAL_UINT16(0x4, (Ina226ConfigValue >> 9) & 0x7);  // AVG
  TEST_ASSERT_EQUAL_UINT16(0x4, (Ina226ConfigValue >> 6) & 0x7);  // VBUSCT
  TEST_ASSERT_EQUAL_UINT16(0x4, (Ina226ConfigValue >> 3) & 0x7);  // VSHCT
  TEST_ASSERT_EQUAL_UINT16(0x7, Ina226ConfigValue & 0x7);         // MODE
  TEST_ASSERT_EQUAL_UINT16(0x1, (Ina226ConfigValue >> 14) & 0x3); // fixed 01
  // SOL (bit 15) + LEN (bit 0)
  TEST_ASSERT_EQUAL_UINT16(0x8001, Ina226MaskSolLatched);
  TEST_ASSERT_EQUAL_UINT16(1u << 4, Ina226MaskAffBit);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_cal_register_driver_board_values);
  RUN_TEST(test_cal_register_other_known_points);
  RUN_TEST(test_cal_register_invalid_inputs);
  RUN_TEST(test_bus_voltage_scaling);
  RUN_TEST(test_current_scaling_signed);
  RUN_TEST(test_power_scaling);
  RUN_TEST(test_alert_limit_counts);
  RUN_TEST(test_imon_scaling_driver_board_values);
  RUN_TEST(test_imon_invalid_divisor);
  RUN_TEST(test_config_register_fields);
  return UNITY_END();
}
