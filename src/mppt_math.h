#ifndef MPPT_MATH_H
#define MPPT_MATH_H

// Maximum power point tracking: adaptive-step perturb & observe over the
// modulation index, fed by the meter's real-power measurement. No hardware
// dependencies, unit-tested natively.
//
// A TEG behaves as an EMF behind a series internal resistance, so drawn
// power is concave in the load current (and hence in the modulation index):
// P&O climbs that curve. The step adapts - two consecutive improvements
// double it (fast tracking after thermal shifts), a reversal halves it
// (small limit-cycle at the peak). A power jump far beyond the deadband
// resets the step to maximum: the operating environment changed and the
// old fine step would crawl.
//
// Timing contract (owner: the task, not this header): one mpptStep per
// evaluation interval, where the interval covers the soft-start index ramp
// PLUS a full meter accumulation window, so powerMw genuinely reflects the
// previous perturbation before the next decision is made.

#include <stdint.h>

struct MpptParams {
  uint16_t minIndexMilli = 50;
  uint16_t maxIndexMilli = 1000;
  uint16_t stepMaxMilli = 20;
  uint16_t stepMinMilli = 5;
  uint16_t deadbandMw = 10;      // |dP| below this = noise: hold direction
  uint32_t restartDeltaMw = 1000; // |dP| above this = environment changed
};

struct MpptState {
  int32_t prevPowerMw = 0;
  int8_t dir = 1;
  uint16_t stepMilli = 20;
  uint16_t indexMilli = 500;
  uint8_t growCnt = 0;
  uint8_t flatCnt = 0; // consecutive in-deadband evaluations
  bool primed = false; // first sample only records a baseline
};

// After this many consecutive in-deadband steps the tracker turns around
// instead of continuing to walk: on a power plateau (e.g. output saturated
// by cycle-by-cycle current limiting) an unbounded "keep exploring" policy
// would sweep the index rail to rail. The cap bounds the wander to a few
// steps either side of where the gradient disappeared. Note the inherent
// limit: within the deadband-blind radius the gradient is invisible by
// definition, so the parking point there is where the tracker entered, not
// necessarily the true peak - tune DeadbandMw to the meter's real noise.
constexpr uint8_t MpptFlatCap = 4;

inline void mpptInit(MpptState &s, uint16_t startIndexMilli, const MpptParams &p) {
  s = MpptState{};
  s.stepMilli = p.stepMaxMilli;
  s.indexMilli = startIndexMilli;
  if (s.indexMilli < p.minIndexMilli) s.indexMilli = p.minIndexMilli;
  if (s.indexMilli > p.maxIndexMilli) s.indexMilli = p.maxIndexMilli;
}

// One P&O evaluation: powerMw is the settled measurement under the CURRENT
// indexMilli. Returns the next index target.
inline uint16_t mpptStep(MpptState &s, const MpptParams &p, int32_t powerMw) {
  if (!s.primed) {
    s.prevPowerMw = powerMw;
    s.primed = true;
    // Take the first perturbation immediately so the climb starts
  } else {
    const int32_t delta = powerMw - s.prevPowerMw;
    const int32_t mag = delta >= 0 ? delta : -delta;
    s.prevPowerMw = powerMw;
    if (mag > static_cast<int32_t>(p.restartDeltaMw)) {
      // Source shifted (thermal transient, load change): re-track coarsely
      s.stepMilli = p.stepMaxMilli;
      s.growCnt = 0;
      s.flatCnt = 0;
      if (delta < 0) {
        s.dir = static_cast<int8_t>(-s.dir);
      }
    } else if (mag <= static_cast<int32_t>(p.deadbandMw)) {
      // Within noise: keep exploring, but bounded - turn around after
      // MpptFlatCap consecutive flat results so a plateau cannot pull the
      // index rail to rail
      s.growCnt = 0;
      if (++s.flatCnt >= MpptFlatCap) {
        s.flatCnt = 0;
        s.dir = static_cast<int8_t>(-s.dir);
      }
    } else if (delta > 0) {
      // Improved: keep direction, accelerate after two wins in a row
      s.flatCnt = 0;
      if (++s.growCnt >= 2) {
        const uint32_t doubled = 2U * s.stepMilli;
        s.stepMilli = doubled > p.stepMaxMilli ? p.stepMaxMilli : static_cast<uint16_t>(doubled);
        s.growCnt = 0;
      }
    } else {
      // Got worse: we stepped past the peak - reverse and refine
      s.dir = static_cast<int8_t>(-s.dir);
      const uint16_t halved = s.stepMilli / 2;
      s.stepMilli = halved < p.stepMinMilli ? p.stepMinMilli : halved;
      s.growCnt = 0;
      s.flatCnt = 0;
    }
  }

  int32_t next = static_cast<int32_t>(s.indexMilli) + s.dir * s.stepMilli;
  if (next <= p.minIndexMilli) {
    next = p.minIndexMilli;
    s.dir = 1; // walked into the floor: only way is up
  }
  if (next >= p.maxIndexMilli) {
    next = p.maxIndexMilli;
    s.dir = -1;
  }
  s.indexMilli = static_cast<uint16_t>(next);
  return s.indexMilli;
}

#endif
