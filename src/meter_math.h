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

inline MeterReadings computeMeterReadings(int64_t sumP, uint64_t sumVsq, uint64_t sumIsq,
                                          uint32_t n, float mvPerCountV, float maPerCountI) {
  MeterReadings r;
  if (n == 0) {
    return r;
  }
  // count-domain means (double: sums can reach 2^54 over a 1s window)
  const float meanP = static_cast<float>(static_cast<double>(sumP) / n);
  const float vrmsCounts = sqrtf(static_cast<float>(static_cast<double>(sumVsq) / n));
  const float irmsCounts = sqrtf(static_cast<float>(static_cast<double>(sumIsq) / n));

  // (mV) * (mA) / 1000 = mW
  const float powerMw = meanP * mvPerCountV * maPerCountI / 1000.0f;
  const float vrmsMv = vrmsCounts * mvPerCountV;
  const float irmsMa = irmsCounts * maPerCountI;

  r.powerMw = static_cast<int32_t>(roundf(powerMw));
  r.vrmsMv = static_cast<uint32_t>(roundf(vrmsMv));
  r.irmsMa = static_cast<uint32_t>(roundf(irmsMa));

  const float apparentMw = (vrmsMv / 1000.0f) * irmsMa;
  if (apparentMw > 0.5f) {
    float pf = 1000.0f * powerMw / apparentMw;
    if (pf > 1000.0f) pf = 1000.0f;
    if (pf < -1000.0f) pf = -1000.0f;
    r.pfMilli = static_cast<int32_t>(roundf(pf));
  }
  r.valid = true;
  return r;
}

// Energy integration step: mWh added by holding powerMw for elapsedMs
inline double energyStepMwh(int32_t powerMw, uint32_t elapsedMs) {
  return static_cast<double>(powerMw) * elapsedMs / 3600000.0;
}

#endif
