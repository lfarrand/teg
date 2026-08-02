#ifndef PWM_TIMING_H
#define PWM_TIMING_H

// Asymmetric Induction edge-timing math: no hardware dependencies, unit-tested
// natively. Mirrors the float arithmetic historically used in configureModule4.

#include <stdint.h>
#include <math.h>
#include "spwm_math.h"

struct AsymmetricTimings {
  bool valid;
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

  const uint64_t roundedPeriod =
      (static_cast<uint64_t>(effectiveClockHz) + pwmFrequencyHz / 2U) / pwmFrequencyHz;
  if (roundedPeriod < 2U || roundedPeriod > static_cast<uint64_t>(maxCounterValue) + 1U) {
    return t;
  }
  const int64_t periodTicks = static_cast<int64_t>(roundedPeriod);
  const int64_t pulseTicksA =
      (periodTicks * static_cast<uint64_t>(dutyCycleA16) + 32768U) / 65536U;
  const auto nanosToTicks = [effectiveClockHz](int32_t ns) -> int64_t {
    const int64_t product = static_cast<int64_t>(ns) * effectiveClockHz;
    return product >= 0 ? (product + 500000000LL) / 1000000000LL
                        : (product - 500000000LL) / 1000000000LL;
  };
  const int64_t preShiftTicks = nanosToTicks(preShiftNanos);
  const int64_t postShiftTicks = nanosToTicks(postShiftNanos);

  const int64_t periodEnd = periodTicks - 1;
  const int64_t stopA = pulseTicksA;
  const int64_t startB = stopA - preShiftTicks;
  const int64_t stopB = periodEnd - postShiftTicks;
  // Every compare must be reachable between INIT and VAL1, in order. A value
  // outside this interval is never matched and can leave an output asserted.
  if (stopA <= 0 || stopA > periodEnd || startB < 0 || startB >= stopB ||
      stopB > periodEnd) {
    return t;
  }

  t.periodStart = 0;
  t.periodEnd = static_cast<int16_t>(periodEnd);
  t.startChanA = t.periodStart;
  t.stopChanA = static_cast<int16_t>(stopA);
  t.startChanB = static_cast<int16_t>(startB);
  t.stopChanB = static_cast<int16_t>(stopB);
  t.valid = true;
  return t;
}

#endif
