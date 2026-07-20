#ifndef MODULATION_H
#define MODULATION_H

// Inverter modulation schemes: no hardware dependencies, unit-tested natively.
//
// Every scheme decomposes into three orthogonal pieces:
//  1. Reference waveform - what goes in the LUT (sine, or sine + 1/6 third
//     harmonic for THIPWM), scaled by a modulation index.
//  2. Per-cell duty mapping - full reference (SPWM / phase-shifted), or a
//     band slice of it (level-shifted stacked carriers reduce to
//     clamp(N*ref - k) per cell).
//  3. Per-cell carrier geometry - in phase, or 180deg inverted. On FlexPWM a
//     180deg shift of a centre-aligned carrier is exactly "invert the output
//     polarity and complement the duty": the centred pulse becomes a centred
//     notch, i.e. the same pulse moved half a carrier period.
//
// Hardware note: arbitrary carrier shifts (e.g. 90deg for 4-cell PS-PWM) need
// the PHASEDLY register, which exists on the i.MX RT1170 but not the RT1062,
// so phase-shifted mode alternates carriers by 180deg (exact for 2 cells).

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
};

// Carrier disposition, used by ModSchemeLevelShifted only
enum : uint8_t {
  CarrierPd = 0,   // Phase Disposition: all carriers in phase
  CarrierPod = 1,  // Phase Opposition Disposition: carriers below zero inverted
  CarrierApod = 2, // Alternate Phase Opposition Disposition: alternate carriers inverted
};

constexpr uint8_t MaxModulationCells = 4;
constexpr uint16_t MaxModulationIndexMilli = 1155; // 2/sqrt(3), reachable with THIPWM

// How one cell (submodule) realises its carrier geometry
struct CellPlan {
  bool polarityInverted; // programmed into OCTRL once at configure time
  bool dutyComplement;   // applied per carrier cycle in the ISR
};

// One cycle of the reference waveform as duty values. indexMilli is the
// modulation index in thousandths (1000 = 1.0); references beyond +/-1
// (overmodulation) clamp to the rails. The second half-cycle is mirrored from
// the first so the wave is antisymmetric - zero DC bias - by construction.
inline void buildReferenceLut(uint16_t *lut, uint32_t size, bool thirdHarmonic, uint16_t indexMilli) {
  const float m = static_cast<float>(indexMilli) / 1000.0f;
  const float step = 6.28318530717958648f / static_cast<float>(size);
  for (uint32_t i = 0; i < size / 2; i++) {
    float ref = sinf(step * static_cast<float>(i));
    if (thirdHarmonic) {
      ref += sinf(3.0f * step * static_cast<float>(i)) / 6.0f;
    }
    ref *= m;
    if (ref > 1.0f) {
      ref = 1.0f;
    }
    const uint16_t duty = static_cast<uint16_t>(32768.0f + roundf(32767.0f * ref));
    lut[i] = duty;
    lut[i + size / 2] = static_cast<uint16_t>(65536U - duty);
  }
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
    default:
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
