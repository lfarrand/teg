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
inline void buildUnitReferenceLut(int16_t *lut, uint32_t size, bool thirdHarmonic) {
  const float step = 6.28318530717958648f / static_cast<float>(size);
  for (uint32_t i = 0; i < size / 2; i++) {
    float ref = sinf(step * static_cast<float>(i));
    if (thirdHarmonic) {
      ref += sinf(3.0f * step * static_cast<float>(i)) / 6.0f;
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

// SVPWM: three unit references 120deg apart with min-max zero-sequence
// injection. Line-to-line voltages are unaffected by the common-mode term;
// the crest flattening extends the linear range to index 1.1547, same as
// THIPWM but exact for three-phase.
inline void svmUnitRefs(const int16_t *lut, uint32_t phase, int32_t out[3]) {
  out[0] = refFromPhase(lut, phase);
  out[1] = refFromPhase(lut, phase - PhaseShift120Deg);
  out[2] = refFromPhase(lut, phase - 2U * PhaseShift120Deg);

  int32_t mx = out[0], mn = out[0];
  for (int k = 1; k < 3; k++) {
    if (out[k] > mx) mx = out[k];
    if (out[k] < mn) mn = out[k];
  }
  const int32_t zss = -(mx + mn) / 2;
  out[0] += zss;
  out[1] += zss;
  out[2] += zss;
}

// Soft-start: per-carrier-cycle Q15 step so the index reaches targetQ15 in
// roughly rampMs. rampMs == 0 disables ramping (instant, step spans the range).
constexpr uint32_t softStartStepQ15(uint32_t targetQ15, uint16_t rampMs, uint32_t carrierHz) {
  const uint64_t steps = (static_cast<uint64_t>(rampMs) * carrierHz) / 1000ULL;
  if (steps == 0) {
    return 1UL << 30;
  }
  const uint32_t step = static_cast<uint32_t>(targetQ15 / steps);
  return step != 0 ? step : 1;
}

// Advance the current index one carrier cycle toward the target, slew-limited
inline uint32_t rampIndexQ15(uint32_t current, uint32_t target, uint32_t step) {
  if (current < target) {
    const uint32_t next = current + step;
    return next > target ? target : next;
  }
  if (current > target) {
    return (current - target) > step ? current - step : target;
  }
  return current;
}

// Duty for one cell of a level-shifted (stacked-carrier) modulator: the
// reference-duty domain [0,65535] is split into numCells equal bands; a cell
// is fully off below its band, fully on above it, and linear inside it.
inline uint16_t lsCellDuty(uint16_t ref, uint8_t cell, uint8_t numCells) {
  const uint32_t scaled = static_cast<uint32_t>(ref) * numCells;
  const uint32_t bandStart = static_cast<uint32_t>(cell) << 16;
  if (scaled <= bandStart) {
    return 0;
  }
  const uint32_t d = scaled - bandStart;
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

// The duty a cell's comparator produces, before carrier geometry
inline uint16_t modulationCellDuty(uint8_t scheme, uint16_t ref, uint8_t cell, uint8_t numCells) {
  return scheme == ModSchemeLevelShifted ? lsCellDuty(ref, cell, numCells) : ref;
}

// Final duty written to the submodule, including the ISR half of the carrier
// geometry. 65535 - d (not 65536 - d) so that duty 0 maps to "always off"
// after the polarity inversion, at the cost of one LSB.
inline uint16_t modulationFinalDuty(uint16_t cellDuty, const CellPlan &plan) {
  return plan.dutyComplement ? static_cast<uint16_t>(65535U - cellDuty) : cellDuty;
}

#endif
