#ifndef ACMP_MATH_H
#define ACMP_MATH_H

// Pure math and lookup tables for the on-chip analog comparator (ACMP)
// overcurrent path: no hardware dependencies, unit-tested natively.
//
// The i.MX RT1062 has four comparators (CMP1-4), each with an 8-channel
// input mux (channels 0-6 = fixed external pads, channel 7 = the internal
// 6-bit DAC). Which Teensy 4.1 pins reach which comparator is fixed silicon
// routing (RM rev4 Table 10-1); the table below holds one deterministic
// route per reachable pin.

#include <stdint.h>

struct AcmpRoute {
  uint8_t cmp;  // comparator unit 1-4; 0 = pin not comparator-capable
  uint8_t psel; // MUXCR plus-input channel for that unit
};

// One route per ACMP-reachable Teensy 4.1 pin (RM rev4 Table 10-1 pp.290-291
// cross-checked against core_pins.h pad assignments). Pins 17/18 are
// reachable by all four units; the table pins them to CMP1 so the other
// units stay free for pins only they can reach. Pins 22/23 are comparator-
// capable in silicon but EXCLUDED here: this firmware drives them as
// FlexPWM4 outputs (Sm40/Sm41), and claiming one for the comparator would
// re-mux the pad away from the PWM.
inline AcmpRoute acmpRouteForPin(uint8_t pin) {
  switch (pin) {
    case 18: return {1, 0}; // AD_B1_01
    case 17: return {1, 1}; // AD_B1_06
    case 25: return {1, 2}; // AD_B0_13
    case 14: return {1, 3}; // AD_B1_02
    case 16: return {1, 5}; // AD_B1_07
    case 21: return {1, 6}; // AD_B1_11
    case 15: return {2, 3}; // AD_B1_03
    case 38: return {2, 6}; // AD_B1_12 (A14)
    case 40: return {3, 3}; // AD_B1_04 (A16, the Meter current-pin default)
    case 1:  return {3, 4}; // AD_B0_02
    case 23: return {0, 0}; // AD_B1_09: reachable (CMP3 ch5) but is the Sm41 PWM pin
    case 22: return {0, 0}; // AD_B1_08: reachable (CMP2 ch5) but is the Sm40 PWM pin
    case 39: return {3, 6}; // AD_B1_13 (A15)
    case 19: return {4, 2}; // AD_B1_00
    case 41: return {4, 3}; // AD_B1_05 (A17)
    case 0:  return {4, 4}; // AD_B0_03
    case 20: return {4, 5}; // AD_B1_10
    case 26: return {4, 6}; // AD_B1_14 (A12)
    default: return {0, 0};
  }
}

// XBARA1 input index carrying CMPn's output (mirrors XBARA1_IN_ACMP1_OUT..4
// = 26..29 in imxrt.h, kept as arithmetic so this header stays native-clean)
constexpr uint8_t acmpXbarInputForCmp(uint8_t cmp) {
  return static_cast<uint8_t>(25 + cmp);
}

// 6-bit DAC transfer function: DACO = (Vin/64) * (VOSEL+1), so the reachable
// thresholds are Vin/64 .. Vin in Vin/64 (~51.6mV) steps. Vin is VDDA = the
// 3.3V rail on the RT1062 (both DAC reference inputs are tied to it).
constexpr uint16_t AcmpDacVinMv = 3300;

inline uint8_t acmpThresholdToDacCode(uint16_t thresholdMv, uint16_t vinMv = AcmpDacVinMv) {
  if (vinMv == 0) {
    return 0;
  }
  const int32_t code = (static_cast<int32_t>(thresholdMv) * 64 + vinMv / 2) / vinMv - 1;
  if (code < 0) {
    return 0;
  }
  return code > 63 ? 63 : static_cast<uint8_t>(code);
}

inline uint16_t acmpDacCodeToMv(uint8_t code, uint16_t vinMv = AcmpDacVinMv) {
  if (code > 63) {
    code = 63;
  }
  return static_cast<uint16_t>((static_cast<uint32_t>(code) + 1) * vinMv / 64);
}

// CMP sampled-filter glitch rejection: pulses shorter than roughly
// (count-1) * period bus clocks are rejected; recognition latency is about
// count * period clocks. Either parameter at 0 = hardware filter bypass
// (continuous mode - the RM's recommendation when driving PWM fault inputs).
inline uint32_t acmpFilterGlitchNanos(uint8_t filterCount, uint8_t filterPeriod, uint32_t busHz) {
  if (filterCount == 0 || filterPeriod == 0 || busHz == 0) {
    return 0;
  }
  const uint64_t clocks = static_cast<uint64_t>(filterCount) * filterPeriod;
  return static_cast<uint32_t>(clocks * 1000000000ULL / busHz);
}

#endif
