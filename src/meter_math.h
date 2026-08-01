#ifndef METER_MATH_H
#define METER_MATH_H

// Power/energy metering math over the ISR's raw accumulators: no hardware
// dependencies, unit-tested natively.
//
// The capture ISR accumulates, per sample pair, with sensor zero offsets
// already subtracted (signed counts):
//   sumP   += v * i        (instantaneous power, count^2)
//   sumVsq += v * v
//   sumIsq += i * i
// This header converts a drained accumulator bank into engineering units
// using per-count scale factors derived from the sensor calibration.

#include <stdint.h>
#include <limits.h>
#include <math.h>

struct MeterReadings {
  int32_t powerMw = 0;  // real (average) power; signed - export vs import
  uint32_t vrmsMv = 0;
  uint32_t irmsMa = 0;
  int32_t pfMilli = 0;  // power factor x1000, signed with power direction
  bool valid = false;
};

// 12-bit ADC, 3.3V reference: pin millivolts per count, times the external
// divider ratio (output volts per pin volt, in thousandths)
inline float meterMvPerCount(uint32_t voltageRatioMilli) {
  return (3300.0f / 4095.0f) * (static_cast<float>(voltageRatioMilli) / 1000.0f);
}

// Current sensor: pin millivolts per count, times sensor gain (mA per volt)
inline float meterMaPerCount(uint32_t currentMilliampPerVolt) {
  return (3300.0f / 4095.0f) * (static_cast<float>(currentMilliampPerVolt) / 1000.0f);
}

// Sensor zero (bias) point in raw ADC counts
inline int32_t meterZeroCounts(uint16_t zeroMillivolts) {
  return static_cast<int32_t>(roundf(static_cast<float>(zeroMillivolts) * 4095.0f / 3300.0f));
}

// Reject calibrations whose worst possible 12-bit sample pair cannot be
// represented by the public engineering-unit types. This is deliberately
// conservative (full-scale signed deltas on both channels).
inline bool meterCalibrationValid(uint16_t voltageZeroMillivolts,
                                  uint16_t currentZeroMillivolts,
                                  uint32_t currentMilliampPerVolt,
                                  uint32_t voltageRatioMilli) {
  if (voltageZeroMillivolts > 3300 || currentZeroMillivolts > 3300 ||
      currentMilliampPerVolt == 0 || voltageRatioMilli == 0) {
    return false;
  }
  const double mvPerCount = (3300.0 / 4095.0) * voltageRatioMilli / 1000.0;
  const double maPerCount = (3300.0 / 4095.0) * currentMilliampPerVolt / 1000.0;
  const double maxCounts = 4095.0;
  const double maxPowerMw = maxCounts * maxCounts * mvPerCount * maPerCount / 1000.0;
  return isfinite(maxPowerMw) && maxPowerMw <= static_cast<double>(INT32_MAX) &&
         maxCounts * mvPerCount <= static_cast<double>(UINT32_MAX) &&
         maxCounts * maPerCount <= static_cast<double>(UINT32_MAX);
}

inline int32_t meterSaturatingRoundI32(double value) {
  if (!isfinite(value)) return 0;
  if (value >= static_cast<double>(INT32_MAX)) return INT32_MAX;
  if (value <= static_cast<double>(INT32_MIN)) return INT32_MIN;
  return static_cast<int32_t>(llround(value));
}

inline uint32_t meterSaturatingRoundU32(double value) {
  if (!isfinite(value) || value <= 0.0) return 0;
  if (value >= static_cast<double>(UINT32_MAX)) return UINT32_MAX;
  return static_cast<uint32_t>(llround(value));
}

inline MeterReadings computeMeterReadings(int64_t sumP, uint64_t sumVsq, uint64_t sumIsq,
                                          uint32_t n, float mvPerCountV, float maPerCountI) {
  MeterReadings r;
  if (n == 0 || !isfinite(mvPerCountV) || !isfinite(maPerCountI) ||
      mvPerCountV <= 0.0f || maPerCountI <= 0.0f) {
    return r;
  }
  // Keep the full accumulator precision through the range checks. Sums can
  // reach 2^54 over a one-second window and accepted calibration values used
  // to make float-to-int overflow undefined at this boundary.
  const double meanP = static_cast<double>(sumP) / n;
  const double vrmsCounts = sqrt(static_cast<double>(sumVsq) / n);
  const double irmsCounts = sqrt(static_cast<double>(sumIsq) / n);
  const double mvScale = static_cast<double>(mvPerCountV);
  const double maScale = static_cast<double>(maPerCountI);

  // (mV) * (mA) / 1000 = mW
  const double powerMw = meanP * mvScale * maScale / 1000.0;
  const double vrmsMv = vrmsCounts * mvScale;
  const double irmsMa = irmsCounts * maScale;
  if (!isfinite(powerMw) || !isfinite(vrmsMv) || !isfinite(irmsMa)) {
    return r;
  }

  r.powerMw = meterSaturatingRoundI32(powerMw);
  r.vrmsMv = meterSaturatingRoundU32(vrmsMv);
  r.irmsMa = meterSaturatingRoundU32(irmsMa);

  const double apparentMw = (vrmsMv / 1000.0) * irmsMa;
  if (apparentMw > 0.5) {
    double pf = 1000.0 * powerMw / apparentMw;
    if (pf > 1000.0) pf = 1000.0;
    if (pf < -1000.0) pf = -1000.0;
    r.pfMilli = meterSaturatingRoundI32(pf);
  }
  r.valid = true;
  return r;
}

// Energy integration step: mWh added by holding powerMw for elapsedMs
inline double energyStepMwh(int32_t powerMw, uint32_t elapsedMs) {
  return static_cast<double>(powerMw) * elapsedMs / 3600000.0;
}

inline uint64_t energyMwhToCounter(double energyMwh) {
  if (!isfinite(energyMwh) || energyMwh <= 0.0) return 0;
  if (energyMwh >= static_cast<double>(UINT64_MAX)) return UINT64_MAX;
  return static_cast<uint64_t>(energyMwh);
}

#endif
