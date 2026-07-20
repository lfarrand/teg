#ifndef PWM_TIMING_H
#define PWM_TIMING_H

// Asymmetric Induction edge-timing math: no hardware dependencies, unit-tested
// natively. Mirrors the float arithmetic historically used in configureModule4.

#include <stdint.h>
#include <math.h>
#include "spwm_math.h"

struct AsymmetricTimings {
  uint8_t prescalerIndex;
  int16_t periodStart;
  int16_t periodEnd;
  int16_t startChanA;
  int16_t stopChanA;
  int16_t startChanB;
  int16_t stopChanB;
};

// NOTE: periodTicks is computed from the undivided bus clock, matching the
// original code: frequencies low enough to need a prescaler > /1 overflow the
// 16-bit VAL registers. Asymmetric mode is intended for frequencies that fit
// at /1 (>= ~2289 Hz at 150 MHz).
inline AsymmetricTimings computeAsymmetricTimings(uint32_t busClockHz, uint32_t pwmFrequencyHz,
                                                  uint16_t dutyCycleA16, int32_t preShiftNanos,
                                                  int32_t postShiftNanos, uint32_t maxCounterValue) {
  AsymmetricTimings t{};

  t.prescalerIndex = bestPrescalerIndex(busClockHz, pwmFrequencyHz, maxCounterValue);

  const float dutyCycleA = static_cast<float>(dutyCycleA16) / 65536.0f;
  const float clockTicksPerNanosecond = static_cast<float>(busClockHz) / 1000000000.0f;
  const uint32_t periodTicks =
    static_cast<uint32_t>(roundf(static_cast<float>(busClockHz) / static_cast<float>(pwmFrequencyHz)));
  const int16_t pulseTicksA = static_cast<int16_t>(roundf(static_cast<float>(periodTicks) * dutyCycleA));
  const int16_t preShiftTicks =
    static_cast<int16_t>(roundf(clockTicksPerNanosecond * static_cast<float>(preShiftNanos)));
  const int16_t postShiftTicks =
    static_cast<int16_t>(roundf(clockTicksPerNanosecond * static_cast<float>(postShiftNanos)));

  t.periodStart = 0;
  t.periodEnd = static_cast<int16_t>(periodTicks - 1);
  t.startChanA = t.periodStart;
  t.stopChanA = static_cast<int16_t>(t.startChanA + pulseTicksA);
  t.startChanB = static_cast<int16_t>(t.stopChanA - preShiftTicks);
  t.stopChanB = static_cast<int16_t>(t.periodEnd - postShiftTicks);
  return t;
}

#endif
