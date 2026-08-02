#ifndef MODULATION_H
#define MODULATION_H

// Inverter modulation schemes: no hardware dependencies, unit-tested natively.
//
// Every scheme decomposes into orthogonal pieces:
//  1. Unit reference waveform - what goes in the LUT (sine, or sine + 1/6
//     third harmonic for THIPWM), stored SIGNED at unit amplitude so that
//     amplitude can be scaled at runtime (closed-loop control, soft-start)
//     without rebuilding the table.
//  2. Runtime scaling - a Q15 modulation index applied per carrier cycle,
//     plus optional dead-time compensation (a polarity-signed duty offset).
//  3. Per-cell duty mapping - full reference (SPWM / phase-shifted / SVPWM
//     zero-sequence injection), or a band slice of it (level-shifted stacked
//     carriers reduce to clamp(N*ref - k) per cell).
//  4. Per-cell carrier geometry - in phase, or 180deg inverted. On FlexPWM a
//     180deg shift of a centre-aligned carrier is exactly "invert the output
//     polarity and complement the duty".
//
// Hardware note: arbitrary carrier shifts (e.g. 90deg for 4-cell PS-PWM) need
// the PHASEDLY register, which exists on the i.MX RT1170 but not the RT1062,
// so phase-shifted mode alternates carriers by 180deg (exact for 2 cells).
// See docs/RT1170_PSPWM.md.

#include <stdint.h>
#include <math.h>
#include "spwm_math.h"

enum : uint8_t {
  ModSchemeFixed = 0,        // no reference modulation: plain fixed-duty PWM
  ModSchemeSpwmUnipolar = 1, // 2 legs, complementary references (3-level output)
  ModSchemeSpwmBipolar = 2,  // 2 legs, one reference, leg 2 in exact opposition (2-level output)
  ModSchemeThipwm = 3,       // unipolar + 1/6 third harmonic (index up to 1.155)
  ModSchemeLevelShifted = 4, // N stacked carrier bands (NPC/diode-clamped)
  ModSchemePhaseShifted = 5, // N cells, full reference each, alternating 180deg carriers
  ModSchemeSvpwm = 6,        // 3-phase space vector (min-max zero-sequence injection), 3 cells
  ModSchemeDpwm = 7,         // 3-phase discontinuous PWM (rail clamping), 3 cells
  ModSchemeSvpwm3D = 8,      // 3-phase four-leg (3D-SVM): legs carry v+zss, leg 4 carries zss,
                             // so phase-to-neutral is the pure reference and zero-sequence
                             // load current returns through the fourth leg. 4 cells.
};

// Reference waveform stored in the LUT (THIPWM always uses sine + 1/6 third)
enum : uint8_t {
  RefWaveSine = 0,
  RefWaveTrapezoid = 1, // 60deg linear ramps, flat top: classic trapezoidal drive
  RefWaveSquare = 2,    // six-step / square-wave operation
  RefWaveCustom = 3,    // uploaded teg-wave reference (see waveform_parse.h)
  RefWaveSequence = 4,  // uploaded teg-wave level/duration segment player
};

// Discontinuous PWM variants (ModSchemeDpwm)
enum : uint8_t {
  DpwmMin = 0,         // clamp lowest phase to the negative rail (120deg clamping)
  DpwmMax = 1,         // clamp highest phase to the positive rail (120deg clamping)
  DpwmGeneralised = 2, // GDPWM: clamp largest-|ref| phase, selection advanced by a
                       // clamp angle; 0deg = DPWM1, -30deg = DPWM0, +30deg = DPWM2
  Dpwm3 = 3,           // clamp the intermediate-magnitude phase (30deg segments)
};

// Carrier disposition, used by ModSchemeLevelShifted only
enum : uint8_t {
  CarrierPd = 0,   // Phase Disposition: all carriers in phase
  CarrierPod = 1,  // Phase Opposition Disposition: carriers below zero inverted
  CarrierApod = 2, // Alternate Phase Opposition Disposition: alternate carriers inverted
};

constexpr uint8_t MaxModulationCells = 4;
constexpr uint16_t MaxModulationIndexMilli = 1155; // 2/sqrt(3), reachable with THIPWM/SVPWM
constexpr uint32_t PhaseShift120Deg = 1431655765U; // 2^32 / 3, for the SVPWM phase legs

// How one cell (submodule) realises its carrier geometry
struct CellPlan {
  bool polarityInverted; // programmed into OCTRL once at configure time
  bool dutyComplement;   // applied per carrier cycle in the ISR
};

constexpr uint32_t indexMilliToQ15(uint16_t indexMilli) {
  return (static_cast<uint32_t>(indexMilli) << 15) / 1000U;
}

// One cycle of the reference waveform at unit amplitude, signed. The second
// half-cycle is mirrored from the first so the wave is antisymmetric - zero
// DC bias - by construction. THIPWM values peak at ~0.866 (28377): the third
// harmonic flattens the crest, which is what allows indices up to 1.155.
inline void buildUnitReferenceLut(int16_t *lut, uint32_t size, uint8_t wave, bool thirdHarmonic) {
  const float step = 6.28318530717958648f / static_cast<float>(size);
  for (uint32_t i = 0; i < size / 2; i++) {
    float ref;
    if (thirdHarmonic) {
      ref = sinf(step * static_cast<float>(i)) + sinf(3.0f * step * static_cast<float>(i)) / 6.0f;
    } else if (wave == RefWaveSquare) {
      ref = 1.0f;
    } else if (wave == RefWaveTrapezoid) {
      // 60deg ramp up, 60deg flat, 60deg ramp down per half cycle
      const float deg = 360.0f * static_cast<float>(i) / static_cast<float>(size);
      if (deg < 60.0f) {
        ref = deg / 60.0f;
      } else if (deg <= 120.0f) {
        ref = 1.0f;
      } else {
        ref = (180.0f - deg) / 60.0f;
      }
    } else {
      ref = sinf(step * static_cast<float>(i));
    }
    const int16_t v = static_cast<int16_t>(roundf(32767.0f * ref));
    lut[i] = v;
    lut[i + size / 2] = static_cast<int16_t>(-v);
  }
}

// Interpolated signed reference for a DDS phase (same top-bits/frac scheme as
// spwmDutyFromPhase; integer-only for the ISR)
inline int32_t refFromPhase(const int16_t *lut, uint32_t phase) {
  const uint32_t idx = phase >> (32 - SpwmLutBits);
  const int32_t frac = (phase >> (32 - SpwmLutBits - 8)) & 0xFF;
  const int32_t a = lut[idx];
  const int32_t b = lut[(idx + 1) & (SpwmLutSize - 1)];
  return a + (((b - a) * frac) >> 8);
}

// Duty-domain dead-time compensation magnitude: each switching period loses
// (or gains) roughly 2*td of effective pulse width depending on current
// polarity, i.e. a duty fraction of 2*td*fsw.
constexpr int32_t deadtimeCompQ15(uint16_t deadtimeNs, uint32_t carrierHz) {
  return static_cast<int32_t>((2ULL * deadtimeNs * carrierHz << 15) / 1000000000ULL);
}

// Scale a signed unit reference by the Q15 modulation index, apply polarity-
// signed dead-time compensation, saturate, and shift into the duty domain.
// Overmodulation (index > 1) clamps to the rails here.
inline uint16_t refToDuty(int32_t unitRef, uint32_t indexQ15, int32_t dtCompQ15) {
  int32_t v = (unitRef * static_cast<int32_t>(indexQ15)) >> 15;
  if (dtCompQ15 != 0) {
    if (unitRef > 0) {
      v += dtCompQ15;
    } else if (unitRef < 0) {
      v -= dtCompQ15;
    }
  }
  if (v > 32767) v = 32767;
  if (v < -32767) v = -32767;
  return static_cast<uint16_t>(32768 + v);
}

constexpr uint32_t degreesToPhase(int32_t degrees) {
  return static_cast<uint32_t>((static_cast<int64_t>(degrees) * 4294967296LL) / 360LL);
}

// Three-phase references, index-scaled (Q15 domain, may exceed +/-32767 in
// overmodulation - the zero-sequence and saturation stages handle that)
inline void threePhaseScaledRefs(const int16_t *lut, uint32_t phase, uint32_t indexQ15, int32_t v[3]) {
  v[0] = (refFromPhase(lut, phase) * static_cast<int32_t>(indexQ15)) >> 15;
  v[1] = (refFromPhase(lut, phase - PhaseShift120Deg) * static_cast<int32_t>(indexQ15)) >> 15;
  v[2] = (refFromPhase(lut, phase - 2U * PhaseShift120Deg) * static_cast<int32_t>(indexQ15)) >> 15;
}

// SVPWM: min-max zero-sequence injection centres the envelope. Line-to-line
// voltages are unaffected by the common mode; the crest flattening extends
// the linear range to index 1.1547.
inline int32_t continuousSvmZss(const int32_t v[3]) {
  int32_t mx = v[0], mn = v[0];
  for (int k = 1; k < 3; k++) {
    if (v[k] > mx) mx = v[k];
    if (v[k] < mn) mn = v[k];
  }
  return -(mx + mn) / 2;
}

// Discontinuous PWM: choose the zero sequence that pins one phase to a rail,
// so that phase stops switching (~33% switching-loss reduction). Which phase,
// and when, is the variant. Rail clamping is affine (the rail is a constant),
// so unlike SVPWM this must operate on index-SCALED references.
inline int32_t dpwmZss(const int16_t *lut, uint32_t phase, uint32_t clampAnglePhase,
                       uint8_t variant, const int32_t v[3]) {
  int sel = 0;
  switch (variant) {
    case DpwmMin:
      for (int k = 1; k < 3; k++) {
        if (v[k] < v[sel]) sel = k;
      }
      return -32767 - v[sel];
    case DpwmMax:
      for (int k = 1; k < 3; k++) {
        if (v[k] > v[sel]) sel = k;
      }
      return 32767 - v[sel];
    case Dpwm3: {
      // Intermediate |v|: not the largest, not the smallest
      int lo = 0, hi = 0;
      int32_t mags[3];
      for (int k = 0; k < 3; k++) {
        mags[k] = v[k] >= 0 ? v[k] : -v[k];
      }
      for (int k = 1; k < 3; k++) {
        if (mags[k] > mags[hi]) hi = k;
        if (mags[k] < mags[lo]) lo = k;
      }
      // sel = 3 - hi - lo picks the remaining index, but only when hi and lo are
      // actually different. When every magnitude is equal - which is exactly what
      // happens at modulation index 0, so during soft-start and whenever the output is
      // ramped to zero - neither comparison above ever fires, both stay 0, and the
      // expression yields 3: a read of v[3], one past the end.
      //
      // The old guard tested `sel == hi || sel == lo`, which 3 satisfies neither of, so
      // it let the out-of-range value straight through. Test the range instead.
      sel = (hi == lo) ? hi : 3 - hi - lo;
      break;
    }
    default: { // DpwmGeneralised: largest |ref| after the clamp-angle advance
      int32_t best = -1;
      for (int k = 0; k < 3; k++) {
        const int32_t a = refFromPhase(lut, phase + clampAnglePhase - static_cast<uint32_t>(k) * PhaseShift120Deg);
        const int32_t mag = a >= 0 ? a : -a;
        if (mag > best) {
          best = mag;
          sel = k;
        }
      }
      break;
    }
  }
  return v[sel] >= 0 ? 32767 - v[sel] : -32767 - v[sel];
}

// Duty from an index-scaled (Q15) reference: polarity-signed dead-time
// compensation, saturation, shift into the duty domain. polaritySource
// carries the pre-injection reference so the clamped phase's compensation
// direction stays tied to (proxy) current polarity.
inline uint16_t dutyFromScaled(int32_t v, int32_t dtCompQ15, int32_t polaritySource) {
  if (dtCompQ15 != 0) {
    if (polaritySource > 0) {
      v += dtCompQ15;
    } else if (polaritySource < 0) {
      v -= dtCompQ15;
    }
  }
  if (v > 32767) v = 32767;
  if (v < -32767) v = -32767;
  return static_cast<uint16_t>(32768 + v);
}

// ---------------------------------------------------------------------------
// Spread-spectrum / random carrier (RPWM). The ISR rewrites each cell's
// period registers every cycle from a small table; each period entry has a
// matched DDS increment so the fundamental frequency stays exact regardless
// of the dither sequence.
// ---------------------------------------------------------------------------

enum : uint8_t {
  DitherOff = 0,
  DitherRandom = 1, // LFSR-selected entry per cycle (spreads the spectrum)
  DitherSweep = 2,  // triangular sweep through the table (deterministic FM)
};

constexpr uint32_t DitherTableSize = 16;
constexpr uint8_t MaxDitherPercent = 30;

inline void buildDitherTables(uint16_t *periods, uint32_t *increments, uint32_t effectiveClockHz,
                              uint32_t carrierHz, uint32_t modulationHz, uint8_t ditherPercent) {
  if (ditherPercent > MaxDitherPercent) ditherPercent = MaxDitherPercent;
  const uint32_t nominal = carrierHz > 0 ? effectiveClockHz / carrierHz : 2;
  const uint32_t span = (nominal * ditherPercent) / 100U;
  for (uint32_t i = 0; i < DitherTableSize; i++) {
    // Evenly spaced periods across [nominal - span, nominal + span]
    const uint32_t period = nominal - span + (2U * span * i) / (DitherTableSize - 1);
    periods[i] = static_cast<uint16_t>(period);
    // Phase advance for one carrier cycle of this length: mod * T * 2^32
    increments[i] = static_cast<uint32_t>(
      (static_cast<uint64_t>(modulationHz) * period << 32) / effectiveClockHz);
  }
}

// 16-bit Galois LFSR (maximal length, never returns 0 for a non-zero seed)
inline uint16_t nextLfsr16(uint16_t s) {
  return static_cast<uint16_t>((s >> 1) ^ (-(static_cast<int32_t>(s & 1U)) & 0xB400U));
}

// 0,1,...,n-1,n-2,...,1,0,1,... triangular sweep
inline uint32_t triangleIndex(uint32_t counter, uint32_t n) {
  const uint32_t m = counter % (2U * (n - 1));
  return m < n ? m : 2U * (n - 1) - m;
}

// Soft-start retains 24 fractional bits. An integer-Q15 step bottoms out at one,
// limiting any ramp to 32,768 cycles (1.64 s at 20 kHz) regardless of the requested
// duration. The fractional accumulator preserves minute-long ramps without division
// in the ISR.
constexpr uint8_t SoftStartFractionBits = 24;
constexpr uint64_t SoftStartInstant = UINT64_MAX;

constexpr uint64_t softStartStepQ24(uint32_t targetQ15, uint16_t rampMs,
                                    uint32_t carrierHz) {
  const uint64_t steps = (static_cast<uint64_t>(rampMs) * carrierHz) / 1000ULL;
  if (steps == 0) {
    return SoftStartInstant;
  }
  const uint64_t distance = static_cast<uint64_t>(targetQ15) << SoftStartFractionBits;
  const uint64_t step = (distance + steps - 1U) / steps;
  return step != 0 ? step : 1;
}

inline uint64_t rampIndexQ24(uint64_t currentQ24, uint32_t targetQ15,
                             uint64_t stepQ24) {
  const uint64_t targetQ24 = static_cast<uint64_t>(targetQ15) << SoftStartFractionBits;
  if (stepQ24 == SoftStartInstant) {
    return targetQ24;
  }
  if (currentQ24 < targetQ24) {
    const uint64_t remaining = targetQ24 - currentQ24;
    return remaining > stepQ24 ? currentQ24 + stepQ24 : targetQ24;
  }
  if (currentQ24 > targetQ24) {
    const uint64_t remaining = currentQ24 - targetQ24;
    return remaining > stepQ24 ? currentQ24 - stepQ24 : targetQ24;
  }
  return currentQ24;
}

// Duty for one cell of a level-shifted (stacked-carrier) modulator: the
// reference-duty domain [0,65535] is split into numCells equal bands; a cell
// is fully off below its band, fully on above it, and linear inside it.
// With nearestLevel set, the in-band cell snaps to whichever rail is closer
// (Nearest Level Modulation: staircase output, fundamental-frequency
// switching only - coarse at <=5 levels but essentially zero switching loss).
inline uint16_t lsCellDuty(uint16_t ref, uint8_t cell, uint8_t numCells, bool nearestLevel = false) {
  const uint32_t scaled = static_cast<uint32_t>(ref) * numCells;
  const uint32_t bandStart = static_cast<uint32_t>(cell) << 16;
  if (scaled <= bandStart) {
    return 0;
  }
  const uint32_t d = scaled - bandStart;
  if (nearestLevel) {
    return d >= 32768U ? 65535U : 0U;
  }
  return d >= 65535U ? 65535U : static_cast<uint16_t>(d);
}

inline CellPlan modulationCellPlan(uint8_t scheme, uint8_t disposition, uint8_t cell, uint8_t numCells) {
  CellPlan p{false, false};
  switch (scheme) {
    case ModSchemeSpwmUnipolar:
    case ModSchemeThipwm:
      p.dutyComplement = (cell & 1) != 0; // odd legs get the mirrored reference
      break;
    case ModSchemeSpwmBipolar:
      p.polarityInverted = (cell & 1) != 0; // odd legs switch in exact opposition
      break;
    case ModSchemePhaseShifted:
      p.polarityInverted = (cell & 1) != 0; // alternating 180deg carriers
      p.dutyComplement = (cell & 1) != 0;
      break;
    case ModSchemeLevelShifted:
      switch (disposition) {
        case CarrierPod: // bands below zero in antiphase
          p.polarityInverted = cell < numCells / 2;
          p.dutyComplement = cell < numCells / 2;
          break;
        case CarrierApod:
          p.polarityInverted = (cell & 1) != 0;
          p.dutyComplement = (cell & 1) != 0;
          break;
        default: // PD: all in phase
          break;
      }
      break;
    default: // Fixed, SVPWM: no carrier games
      break;
  }
  return p;
}

// Whether any active cell would need its output polarity inverted.
//
// A complementary pair cannot do that. Inverting POLA/POLB turns every dead-time
// gap into an overlap - both switches conducting - and MASK and the fault state
// force the pins to logic 0 BEFORE polarity is applied (RM 55.8.45.4, 55.8.18.3),
// so an inverted pair would drive both switches HIGH on fault, during OTA and at
// boot-mask.
//
// Derived from the plans rather than from an allow-list of scheme numbers: that
// correctly admits level-shifted PD, which never inverts, refuses POD/APOD, and
// cannot drift out of sync when a scheme is added or a plan changes.
//
// The hardware CAN do this, just not through polarity - scheme 2 needs an inverted
// output (DTSRCSEL + one FORCE_OUT) and schemes 5 and 4-POD/APOD need a displaced
// carrier (VAL2/VAL3 edge bias). Both are documented in docs/BENCH_CHECKS.md and
// deliberately deferred; until then a complementary pair refuses these schemes.
inline bool schemeRequiresPolarityInversion(uint8_t scheme, uint8_t disposition, uint8_t cells) {
  for (uint8_t cell = 0; cell < cells; cell++) {
    if (modulationCellPlan(scheme, disposition, cell, cells).polarityInverted) {
      return true;
    }
  }
  return false;
}

// The duty a cell's comparator produces, before carrier geometry
inline uint16_t modulationCellDuty(uint8_t scheme, uint16_t ref, uint8_t cell, uint8_t numCells,
                                   bool nearestLevel = false) {
  return scheme == ModSchemeLevelShifted ? lsCellDuty(ref, cell, numCells, nearestLevel) : ref;
}

// Final duty written to the submodule, including the ISR half of the carrier
// geometry. 65535 - d (not 65536 - d) so that duty 0 maps to "always off"
// after the polarity inversion, at the cost of one LSB.
inline uint16_t modulationFinalDuty(uint16_t cellDuty, const CellPlan &plan) {
  return plan.dutyComplement ? static_cast<uint16_t>(65535U - cellDuty) : cellDuty;
}

// Everything the per-cycle pipeline needs beyond phase and index
struct ModCycleConfig {
  uint8_t scheme = ModSchemeSpwmUnipolar;
  uint8_t cells = 2;
  uint8_t dpwmVariant = DpwmGeneralised;
  uint32_t dpwmClampPhase = 0;
  bool nearestLevel = false;
  int32_t dtCompQ15 = 0;
};

// How many duty slots modulationCycleDuties fills for a scheme
constexpr uint8_t modulationCellCount(uint8_t scheme, uint8_t cells) {
  return scheme == ModSchemeSvpwm || scheme == ModSchemeDpwm ? 3
       : scheme == ModSchemeSvpwm3D                          ? 4
                                                             : cells;
}

// One carrier cycle of the LUT-driven modulation pipeline: reference lookup,
// index scaling, zero-sequence injection, band slicing, carrier geometry -
// exactly the duties the ISR writes to the comparators. Shared between the
// ISR and the host-side spectral tests so the tests measure the real thing.
inline void modulationCycleDuties(const int16_t *lut, uint32_t phase, uint32_t indexQ15,
                                  const ModCycleConfig &c, const CellPlan *plans,
                                  uint16_t *duties) {
  if (c.scheme == ModSchemeSvpwm || c.scheme == ModSchemeDpwm || c.scheme == ModSchemeSvpwm3D) {
    int32_t v[3];
    threePhaseScaledRefs(lut, phase, indexQ15, v);
    const int32_t zss = c.scheme == ModSchemeDpwm
                          ? dpwmZss(lut, phase, c.dpwmClampPhase, c.dpwmVariant, v)
                          : continuousSvmZss(v);
    for (uint8_t k = 0; k < 3; k++) {
      duties[k] = dutyFromScaled(v[k] + zss, c.dtCompQ15, v[k]);
    }
    if (c.scheme == ModSchemeSvpwm3D) {
      // Neutral leg carries the zero sequence, so phase-to-neutral = pure
      // reference. No dead-time comp: its current is the (unknown) sum of
      // the phase currents.
      duties[3] = dutyFromScaled(zss, 0, 0);
    }
    return;
  }
  const int32_t s = refFromPhase(lut, phase);
  // Dead-time compensation applies to full-reference legs; the band-sliced
  // level-shifted cells switch at most one pair per instant, handled per cell
  const uint16_t ref = refToDuty(s, indexQ15, c.scheme == ModSchemeLevelShifted ? 0 : c.dtCompQ15);
  for (uint8_t k = 0; k < c.cells; k++) {
    duties[k] = modulationFinalDuty(modulationCellDuty(c.scheme, ref, k, c.cells, c.nearestLevel),
                                    plans[k]);
  }
}

#endif
