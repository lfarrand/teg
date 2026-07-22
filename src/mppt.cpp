// MPPT (item: MPPT). Pure P&O math in mppt_math.h; this file owns cadence,
// gating and the actuator write.
//
// Actuator: setModulationIndexTargetQ15 - the same knob the closed-loop
// feedback uses, which is why validateConfig makes the two mutually
// exclusive. The thermal derate cap is applied INSIDE the setter; the task
// also clamps its own state to the derated ceiling so P&O never pushes
// against an invisible wall (it would misread the flat response as "no
// improvement" and wander).
//
// Timing: one evaluation per IntervalMs (validated >= 2100ms, and >= the
// soft-start ramp + 2100). The meter reading available at an evaluation can
// be up to one drain period (~1s) old and covers the second before THAT, so
// only an interval above two meter periods guarantees the reading reflects
// a full window under the perturbed index.

#include "mppt.h"
#include "mppt_math.h"
#include "meter.h"
#include "thermal.h"
#include "pwm_utils.h"
#include "modulation.h"
#include "config_json.h"
#include "utils.h"
#include <Arduino.h>

extern MainConfig config;

static MpptParams params;
static MpptState st;
static bool stepping = false;
static int32_t lastDeltaMw = 0;
static elapsedMillis sinceStep;

void mpptConfigure() {
  const bool wasStepping = stepping;
  stepping = false;
  if (!config.Mppt.Enabled) {
    if (wasStepping) {
      // Hand the actuator back: restore the configured index instead of
      // leaving the output parked at the tracker's last position
      uint16_t indexMilli = config.Pwm.Tm2.ModulationIndexMilli;
      if (indexMilli > MaxModulationIndexMilli) {
        indexMilli = MaxModulationIndexMilli;
      }
      setModulationIndexTargetQ15(indexMilliToQ15(indexMilli));
    }
    return;
  }
  params.minIndexMilli = config.Mppt.MinIndexMilli;
  params.maxIndexMilli = config.Mppt.MaxIndexMilli;
  params.stepMaxMilli = config.Mppt.StepMilli;
  params.stepMinMilli = config.Mppt.MinStepMilli;
  params.deadbandMw = config.Mppt.DeadbandMw;
  params.restartDeltaMw = config.Mppt.RestartDeltaMw;
  // Seed from wherever the index currently is: the climb starts from the
  // operating point, not from a config constant
  mpptInit(st, static_cast<uint16_t>(modulationIndexTargetMilli()), params);
  sinceStep = 0;
}

void mpptTask() {
  if (!config.Mppt.Enabled || !spwmActive()) {
    stepping = false;
    st.primed = false; // re-baseline on resume: a stale delta means nothing
    sinceStep = 0;
    return;
  }
  const MeterReadings m = meterReadings();
  if (!m.valid) {
    stepping = false; // metering off/not ready: hold, resume when it returns
    st.primed = false;
    sinceStep = 0; // and wait a full interval so the first window is settled
    return;
  }
  if (sinceStep < config.Mppt.IntervalMs) {
    return;
  }
  sinceStep = 0;

  const bool wasPrimed = st.primed;
  const int32_t before = st.prevPowerMw;
  uint16_t next = mpptStep(st, params, m.powerMw);
  lastDeltaMw = wasPrimed ? m.powerMw - before : 0;

  // Clamp to the thermally derated ceiling and keep the P&O state consistent
  // with what is actually applied
  const uint16_t derateCap = static_cast<uint16_t>(
    (static_cast<uint32_t>(MaxModulationIndexMilli) * thermalDerateMilliNow()) / 1000U);
  if (next > derateCap) {
    next = derateCap;
    st.indexMilli = derateCap;
    st.dir = -1;
  }
  setModulationIndexTargetQ15(indexMilliToQ15(next));
  stepping = true;
}

bool mpptActive() {
  return stepping;
}

uint16_t mpptIndexMilli() {
  return st.indexMilli;
}

uint16_t mpptStepMilli() {
  return st.stepMilli;
}

int8_t mpptDirection() {
  return st.dir;
}

int32_t mpptLastDeltaMw() {
  return lastDeltaMw;
}
