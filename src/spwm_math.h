#ifndef SPWM_MATH_H
#define SPWM_MATH_H

// Pure math for the SPWM DDS and PWM configuration: no Arduino/hardware
// dependencies so it can be unit-tested on the host (pio test -e native).

#include <stdint.h>
#include <math.h>

constexpr uint32_t SpwmLutBits = 11;
constexpr uint32_t SpwmLutSize = 1U << SpwmLutBits; // 2048 entries, 4 KB
constexpr uint16_t SpwmMidDuty = 32768U;

// DDS tuning word: the modulation phase advances by modulation/carrier of a
// full cycle (2^32) per carrier period, so the output frequency is exact to
// carrier/2^32 Hz for ANY carrier/modulation ratio.
constexpr uint32_t spwmPhaseIncrement(uint32_t carrierHz, uint32_t modulationHz) {
  return carrierHz > 0
           ? static_cast<uint32_t>((static_cast<uint64_t>(modulationHz) << 32) / carrierHz)
           : 0;
}

// The modulation frequency actually produced by a tuning word, in millihertz
constexpr uint64_t spwmActualMilliHz(uint32_t phaseIncrement, uint32_t carrierHz) {
  return (static_cast<uint64_t>(phaseIncrement) * carrierHz * 1000ULL) >> 32;
}

// One full sine cycle of channel-A duty values in [1, 65535], centred on
// SpwmMidDuty. Channel B duty is derived as 65536 - dutyA.
// The second half-cycle is mirrored from the first (sinf(x + pi) is not exactly
// -sinf(x) in float), making the wave antisymmetric — zero DC bias — by
// construction.
inline void fillSpwmLut(uint16_t *lut, uint32_t size) {
  const float phaseStep = 6.28318530717958648f / static_cast<float>(size);
  for (uint32_t i = 0; i < size / 2; i++) {
    const float s = roundf((SpwmMidDuty - 1) * sinf(phaseStep * static_cast<float>(i)));
    lut[i] = static_cast<uint16_t>(SpwmMidDuty + s);
    lut[i + size / 2] = static_cast<uint16_t>(65536U - lut[i]);
  }
}

// Top bits of the phase index the table, the next 8 bits interpolate linearly
// between adjacent entries (max slope is ~100 counts/entry, so the maths stays
// small). Integer-only on purpose: called from the SPWM ISR, where an FPU
// instruction would trigger the M7's lazy FPU context stacking.
inline uint16_t spwmDutyFromPhase(const uint16_t *lut, uint32_t phase) {
  const uint32_t idx = phase >> (32 - SpwmLutBits);
  const int32_t frac = (phase >> (32 - SpwmLutBits - 8)) & 0xFF;
  const int32_t a = lut[idx];
  const int32_t b = lut[(idx + 1) & (SpwmLutSize - 1)];
  return static_cast<uint16_t>(a + (((b - a) * frac) >> 8));
}

// Smallest prescaler (index into 1,2,4,...,128) that fits one PWM period of
// pwmFrequencyHz into a 16-bit counter running at busClockHz
constexpr uint8_t bestPrescalerIndex(uint32_t busClockHz, uint32_t pwmFrequencyHz,
                                     uint32_t maxCounterValue) {
  for (uint8_t i = 0; i < 8; i++) {
    const uint32_t effectiveClock = busClockHz / (1U << i);
    if (pwmFrequencyHz > 0 && (effectiveClock / pwmFrequencyHz) <= maxCounterValue) {
      return i;
    }
  }
  return 7;
}

#endif
