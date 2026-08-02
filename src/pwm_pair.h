#pragma once

// Per-submodule PWM pair semantics.
//
// A FlexPWM submodule's A and B outputs do not mean the same thing on every board,
// and that is the whole reason this header exists. Three distinct things get wired
// to an A/B pair in this project:
//
//   - a genuine half-bridge leg, where B must be A's complement and hardware dead
//     time is a safety requirement;
//   - a differential command into a single logical channel (the companion MOSFET
//     driver board takes PWM_IN and PWM_IN_INVERTED on J1/J2 and hard-parallels
//     both of its gate drivers onto that one pair), where a dead-time gap is not a
//     safety margin at all - it is a window where neither state is commanded, into
//     a common-source AC switch that has no freewheel path;
//   - two genuinely unrelated outputs (Tm4's asymmetric induction mode drives A and
//     B from independent VAL2..VAL5 start/stop values).
//
// Treating all three as one global mode is what made an earlier attempt unsafe.
//
// This header is pure: no Arduino, no registers, no side effects. It exists so the
// decisions can be unit-tested on the host before anything writes to hardware,
// because the native suite cannot see registers and every defect in the previous
// attempt was register-init or lifecycle. Nothing here touches the chip; a later
// change wires these decisions to FlexPWM.

#include <stdint.h>

// --- pair modes -------------------------------------------------------------

enum : uint8_t {
  // SMCTRL2[INDEP] = 1. A from VAL2/VAL3, B from VAL4/VAL5, unrelated. The
  // dead-time registers are ignored by the hardware in this mode. This is what
  // the firmware has always actually done, and it stays the default: a firmware
  // update must not silently change what the pins do on a board already wired.
  PairIndependent = 0,

  // SMCTRL2[INDEP] = 0, DTCNT0 = DTCNT1 = dead time. B is the hardware complement
  // of A with a gap on both edges where both pins sit at their INACTIVE level.
  PairHalfBridge = 1,

  // SMCTRL2[INDEP] = 0, DTCNT0 = DTCNT1 = 0. B is the hardware complement of A
  // with no gap, i.e. a true differential command. Dead time is deliberately not
  // floored here - see pairDeadTimeNs().
  PairDifferential = 2,

  PairModeCount = 3,
};

// --- hardware limits --------------------------------------------------------

// DTCNT0/DTCNT1 are 11-bit fields (RM 55.8.24/55.8.25). A larger value does not
// saturate - it wraps, so a bigger number silently programs a SMALLER dead time.
// Everything below exists to make that unreachable.
constexpr uint16_t MaxDeadTimeCycles = 2047;

// Floor for a half-bridge. A gap of zero on a real leg is a shoot-through on every
// carrier edge, and this is the last line of defence: the settings file can be
// missing or truncated, in which case validation never runs at all.
//
// It is a floor, not a recommendation. A real SiC leg wants considerably more -
// worst-case turn-off at temperature, minus fastest turn-on, plus the gate
// driver's propagation-delay mismatch, which lands past 130ns for common 1200V
// parts before any margin.
constexpr uint16_t MinHalfBridgeDeadTimeNs = 150;

// The IPG bus clock the FlexPWM counters run from on this part.
constexpr uint32_t PwmBusClockHz = 150000000u;

// Convert nanoseconds to DTCNT cycles, saturating rather than wrapping.
constexpr uint16_t pairDeadTimeCycles(uint32_t ns, uint32_t busHz = PwmBusClockHz) {
  return (static_cast<uint64_t>(ns) * busHz / 1000000000ull) > MaxDeadTimeCycles
             ? MaxDeadTimeCycles
             : static_cast<uint16_t>(static_cast<uint64_t>(ns) * busHz / 1000000000ull);
}

// The largest dead time the hardware can actually represent, in nanoseconds.
// Anything above this is not merely clamped by us - it is unrepresentable, and
// would wrap if handed to the register directly.
constexpr uint32_t pairMaxDeadTimeNs(uint32_t busHz = PwmBusClockHz) {
  return static_cast<uint32_t>(static_cast<uint64_t>(MaxDeadTimeCycles) * 1000000000ull / busHz);
}

// --- mode predicates --------------------------------------------------------

// Both complementary modes clear INDEP; they differ only in dead time.
constexpr bool pairIsComplementary(uint8_t mode) {
  return mode == PairHalfBridge || mode == PairDifferential;
}

// Only a half-bridge wants a gap. A differential pair wants none, and an
// independent pair has its dead-time registers ignored by the hardware anyway.
constexpr bool pairUsesDeadTime(uint8_t mode) {
  return mode == PairHalfBridge;
}

// The dead time actually programmed, in nanoseconds, given what the operator
// configured. Differential returns 0 deliberately: flooring it would insert the
// very gap that mode exists to avoid. Independent returns 0 because the hardware
// ignores the value and programming a nonzero one only invites confusion when
// reading back registers on a bench.
constexpr uint32_t pairDeadTimeNs(uint8_t mode, uint32_t configuredNs) {
  return !pairUsesDeadTime(mode)              ? 0u
         : configuredNs < MinHalfBridgeDeadTimeNs ? MinHalfBridgeDeadTimeNs
         : configuredNs > pairMaxDeadTimeNs()     ? pairMaxDeadTimeNs()
                                                  : configuredNs;
}

// --- hardware facts, not configuration --------------------------------------

// Whether a submodule has a channel-B pin brought out at all. This is a property
// of the Teensy 4.1 board, not a choice: the core's pwm_pin_info[] exposes
// FlexPWM2_1 as pin 5 only, and Tm4's Sm40/Sm41 were likewise constructed with a
// channel A alone. Only three of Tm2's four submodules can ever be a leg.
//
// It matters beyond "B goes nowhere". The Teensy core's flexpwm_init() writes
// DTCNT0 = 0 and never writes DTCNT1, which therefore sits at its reset value
// 0x07FF - 2047 cycles, about 13.6us. Putting a B-less submodule into a
// complementary mode would run that garbage as the falling-edge dead time.
enum : uint8_t { PairSm13 = 0, PairSm20, PairSm21, PairSm22, PairSm23, PairSm31, PairSm40, PairSm41, PairSm42, PairSubmoduleCount };

constexpr bool pairHasChannelB(uint8_t submodule) {
  return !(submodule == PairSm21 || submodule == PairSm40 || submodule == PairSm41);
}

// Asymmetric induction mode drives Sm42's A and B from independent start/stop
// values, so complementary generation would overwrite the relationship the mode
// exists to produce. Kept separate from pairHasChannelB() because the reason is
// different: Sm42 *has* a B pin, it just must not be paired with A.
constexpr bool pairAllowsComplementary(uint8_t submodule) {
  return pairHasChannelB(submodule) && submodule != PairSm42;
}

// --- validation -------------------------------------------------------------

// Reduce a requested mode to one the submodule can actually run. Returns
// PairIndependent for anything out of range or physically impossible, so a
// corrupt or hand-edited settings file degrades to the safe, always-valid mode
// rather than to a plausible-looking wrong one.
constexpr uint8_t pairModeSanitised(uint8_t requested, uint8_t submodule) {
  return requested >= PairModeCount ? static_cast<uint8_t>(PairIndependent)
         : (pairIsComplementary(requested) && !pairAllowsComplementary(submodule))
             ? static_cast<uint8_t>(PairIndependent)
             : requested;
}

// True when the requested mode survives sanitising unchanged. Callers use this to
// report a correction to the operator rather than silently applying a different
// mode from the one they asked for.
constexpr bool pairModeValid(uint8_t requested, uint8_t submodule) {
  return pairModeSanitised(requested, submodule) == requested;
}

// As above, but also refuses a complementary mode when the active modulation would
// need a cell's output polarity inverted - which a complementary pair cannot do
// safely. The caller supplies that as a plain bool (see
// schemeRequiresPolarityInversion() in modulation.h) so this header stays free of
// modulation concerns and both halves stay independently testable.
constexpr uint8_t pairModeSanitisedForScheme(uint8_t requested, uint8_t submodule,
                                             bool schemeNeedsInversion) {
  return (schemeNeedsInversion && pairIsComplementary(pairModeSanitised(requested, submodule)))
             ? static_cast<uint8_t>(PairIndependent)
             : pairModeSanitised(requested, submodule);
}
