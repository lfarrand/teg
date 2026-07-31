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

// All tick values are in prescaler-divided clock ticks — the rate the counter
// actually runs at — so low frequencies that need a prescaler > /1 still fit
// the 16-bit VAL registers.
inline AsymmetricTimings computeAsymmetricTimings(uint32_t busClockHz, uint32_t pwmFrequencyHz,
                                                  uint16_t dutyCycleA16, int32_t preShiftNanos,
                                                  int32_t postShiftNanos, uint32_t maxCounterValue) {
  AsymmetricTimings t{};

  // A zero frequency is reachable: SubmoduleConfig::PwmFrequency used to be
  // zero-initialised, so a board booting with no settings file arrived here with 0 and
  // divided by it below - producing inf, an undefined narrowing cast, garbage VALx
  // values, and then enabled outputs. The struct default is fixed, but this is the
  // function that actually divides, so it refuses here too rather than trusting every
  // caller for ever. Returning a zeroed result leaves the outputs off.
  if (pwmFrequencyHz == 0 || busClockHz == 0) {
    return t;
  }

  t.prescalerIndex = bestPrescalerIndex(busClockHz, pwmFrequencyHz, maxCounterValue);
  const uint32_t effectiveClockHz = busClockHz / (1U << t.prescalerIndex);

  const float dutyCycleA = static_cast<float>(dutyCycleA16) / 65536.0f;
  const float clockTicksPerNanosecond = static_cast<float>(effectiveClockHz) / 1000000000.0f;
  const uint32_t periodTicks =
    static_cast<uint32_t>(roundf(static_cast<float>(effectiveClockHz) / static_cast<float>(pwmFrequencyHz)));
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
