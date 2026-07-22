// Tests for the power/energy metering math (meter_math.h)

#include <unity.h>
#include <math.h>
#include <meter_math.h>

void setUp() {}
void tearDown() {}

void test_scale_factors() {
  // 1:1 ratio: one count = 3300/4095 mV
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.80586f, meterMvPerCount(1000));
  // 100:1 divider (output 100V per pin volt)
  TEST_ASSERT_FLOAT_WITHIN(1e-2f, 80.586f, meterMvPerCount(100000));
  // ACS712-20A: 100mV/A -> 10000 mA/V
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 8.0586f, meterMaPerCount(10000));
  // Mid-rail zero
  TEST_ASSERT_EQUAL_INT32(2048, meterZeroCounts(1650));
  TEST_ASSERT_EQUAL_INT32(0, meterZeroCounts(0));
}

// Build accumulator sums from a synthetic sampled waveform, then check the
// computed readings against the analytic values
static void accumulateSine(float vAmpCounts, float iAmpCounts, float phaseShift, uint32_t n,
                           int64_t &sumP, uint64_t &sumVsq, uint64_t &sumIsq) {
  sumP = 0;
  sumVsq = 0;
  sumIsq = 0;
  for (uint32_t k = 0; k < n; k++) {
    const float th = 6.28318530717958648f * 50.0f * k / n; // 50 cycles over the window
    const int32_t v = static_cast<int32_t>(roundf(vAmpCounts * sinf(th)));
    const int32_t i = static_cast<int32_t>(roundf(iAmpCounts * sinf(th - phaseShift)));
    sumP += static_cast<int64_t>(v) * i;
    sumVsq += static_cast<uint64_t>(static_cast<int64_t>(v) * v);
    sumIsq += static_cast<uint64_t>(static_cast<int64_t>(i) * i);
  }
}

void test_resistive_load_unity_pf() {
  int64_t sumP;
  uint64_t sumVsq, sumIsq;
  accumulateSine(1000.0f, 500.0f, 0.0f, 20000, sumP, sumVsq, sumIsq);
  // 1:1 scales for easy analytics: Vrms = 707.1 counts * 0.80586 mV
  const MeterReadings r = computeMeterReadings(sumP, sumVsq, sumIsq, 20000,
                                               meterMvPerCount(1000), meterMaPerCount(1000));
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_UINT32_WITHIN(3, 570, r.vrmsMv);  // 707.1 * 0.80586
  TEST_ASSERT_UINT32_WITHIN(2, 285, r.irmsMa);  // 353.6 * 0.80586
  // P = Vrms*Irms for unity PF: 0.570V * 285mA = 162.4 mW
  TEST_ASSERT_INT32_WITHIN(2, 162, r.powerMw);
  TEST_ASSERT_INT32_WITHIN(5, 1000, r.pfMilli);
}

void test_reactive_load_power_factor() {
  int64_t sumP;
  uint64_t sumVsq, sumIsq;
  accumulateSine(1000.0f, 500.0f, 1.0471975512f, 20000, sumP, sumVsq, sumIsq); // 60 degrees
  const MeterReadings r = computeMeterReadings(sumP, sumVsq, sumIsq, 20000,
                                               meterMvPerCount(1000), meterMaPerCount(1000));
  TEST_ASSERT_INT32_WITHIN(10, 500, r.pfMilli); // cos(60) = 0.5
  // Real power halves relative to the resistive case
  TEST_ASSERT_INT32_WITHIN(3, 81, r.powerMw);
}

void test_reverse_power_flow_is_signed() {
  int64_t sumP;
  uint64_t sumVsq, sumIsq;
  accumulateSine(1000.0f, 500.0f, 3.14159265f, 20000, sumP, sumVsq, sumIsq); // anti-phase
  const MeterReadings r = computeMeterReadings(sumP, sumVsq, sumIsq, 20000,
                                               meterMvPerCount(1000), meterMaPerCount(1000));
  TEST_ASSERT_LESS_THAN_INT32(0, r.powerMw);
  TEST_ASSERT_INT32_WITHIN(10, -1000, r.pfMilli);
}

void test_empty_window_invalid() {
  const MeterReadings r = computeMeterReadings(0, 0, 0, 0, 1.0f, 1.0f);
  TEST_ASSERT_FALSE(r.valid);
  TEST_ASSERT_EQUAL_INT32(0, r.powerMw);
}

void test_energy_integration() {
  // 1W for one hour = 1000 mWh
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1000.0f, static_cast<float>(energyStepMwh(1000, 3600000)));
  // 10W for 1 second
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 10000.0f / 3600.0f, static_cast<float>(energyStepMwh(10000, 1000)));
  // Negative power subtracts
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, -1000.0f, static_cast<float>(energyStepMwh(-1000, 3600000)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_scale_factors);
  RUN_TEST(test_resistive_load_unity_pf);
  RUN_TEST(test_reactive_load_power_factor);
  RUN_TEST(test_reverse_power_flow_is_signed);
  RUN_TEST(test_empty_window_invalid);
  RUN_TEST(test_energy_integration);
  return UNITY_END();
}
