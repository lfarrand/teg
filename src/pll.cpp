// Grid/reference PLL (item: PLL sync). The pure math lives in pll_math.h;
// this file owns the sample drain, the DDS steering write, and lifecycle.
//
// Architecture: the capture ISR samples the reference pin once per carrier
// cycle into the PSRAM ring; pllTask() drains every new sample each loop()
// pass (lossless, uniformly spaced at exactly Ts = 1/carrier regardless of
// loop jitter) and runs the SOGI/SRF-PLL per sample. The park angle comes
// from a software mirror of the DDS phase accumulator, re-anchored every
// pass from a coherent (sampleCount, vPhase) snapshot - so the DDS itself is
// the PLL's VCO and lock means the OUTPUT waveform is phase-aligned to the
// reference, with no accumulator write (no waveform discontinuity) ever.
//
// The only value crossing to the ISR is the 32-bit increment via an atomic
// aligned store. All float math stays in loop() context (ISR is integer-only).
//
// Timing model: the drained backlog was generated under the increment that
// was live when those samples were recorded, so the mirror advances with the
// SNAPSHOT increment for the whole batch, while updated commands are written
// per chunk (they only affect future samples). A backlog larger than one task
// pass (or 1/4s at a low carrier) resyncs instead: without an increment history,
// a multi-pass backlog cannot be paired with truthful historical phase.

#include "pll.h"
#include "pll_math.h"
#include "capture.h"
#include "pwm_utils.h"
#include "spwm_math.h"
#include "config_json.h"
#include "utils.h"
#include <Arduino.h>

extern MainConfig config;

static PllParams params;
static PllState st;
static uint32_t cursor = 0;         // captureSampleCount() drain watermark
static uint32_t lastWrittenInc = 0;
static bool steering = false;
static uint32_t lastSampleMs = 0;
static uint32_t resyncs = 0;
static uint8_t stateCode = 0; // indexes stateNames
static float offsetRad = 0.0f;

// Snapshot of everything the PLL derives its coefficients from, so an
// unrelated settings apply (Influx host, thermal limits...) does not drop
// lock or reset the SOGI
struct PllRelevant {
  PllConfig pll;
  uint32_t carrier;
  uint32_t nominal;
};
static PllRelevant lastRelevant;

static const char *stateNames[] = {"off", "noCapture", "excluded", "acquiring",
                                   "locked", "coasting", "noRef"};

static void dropLock() {
  st.locked = false;
  st.lockCnt = 0;
}

void pllConfigure() {
  const bool wasSteering = steering;
  const double heldHz = st.fHat;
  const uint32_t carrier = config.Pwm.Tm2.SpwmCarrierFrequency;
  const uint32_t nominal = config.Pwm.Tm2.SpwmModulationFrequency;

  if (!config.Pll.Enabled) {
    if (wasSteering) {
      // Nothing else restores the free-run frequency when only the PLL
      // section changed (buildSpwmLut runs on Tm2 changes only)
      modulationSetPhaseIncrement(spwmPhaseIncrement(carrier, nominal));
    }
    steering = false;
    dropLock();
    stateCode = 0;
    lastRelevant.pll = config.Pll;
    lastRelevant.carrier = carrier;
    lastRelevant.nominal = nominal;
    return;
  }

  const bool relevantChanged =
    memcmp(&lastRelevant.pll, &config.Pll, sizeof(PllConfig)) != 0 ||
    lastRelevant.carrier != carrier || lastRelevant.nominal != nominal;
  lastRelevant.pll = config.Pll;
  const bool carrierChanged = lastRelevant.carrier != carrier;
  lastRelevant.carrier = carrier;
  lastRelevant.nominal = nominal;

  if (!relevantChanged && wasSteering) {
    // Unrelated apply: keep the SOGI state and the lock flag; just re-anchor
    // the drain (captureConfigure reset the ring) and re-assert our command
    // over the increment buildSpwmLut may have rewritten
    cursor = captureSampleCount();
    modulationSetPhaseIncrement(lastWrittenInc);
    return;
  }

  steering = false;
  pllParamsInit(params, static_cast<float>(carrier), static_cast<float>(nominal),
                config.Pll.BandwidthDeciHz / 10.0f,
                static_cast<float>(config.Pll.MinHz), static_cast<float>(config.Pll.MaxHz),
                config.Pll.ZeroMillivolts, config.Pll.MinLevelMillivolts);
  pllReset(st, static_cast<float>(nominal));
  offsetRad = static_cast<float>(config.Pll.PhaseOffsetCentiDeg) * (PllTwoPi / 36000.0f);
  cursor = captureSampleCount();

  // Bumpless re-entry after a PLL-relevant change: seed the integrator from
  // the held estimate (clamped to the new window) and re-steer immediately
  // so the output frequency never steps. A carrier change invalidates Ts -
  // clean re-acquire from nominal instead.
  if (wasSteering && !carrierChanged) {
    double f = heldHz;
    if (f < static_cast<double>(params.minHz)) f = params.minHz;
    if (f > static_cast<double>(params.maxHz)) f = params.maxHz;
    st.fHat = f;
    st.fCmd = static_cast<float>(f);
    st.fBar = static_cast<float>(f);
    const uint32_t inc =
      pllIncrementFromMilliHz(static_cast<uint64_t>(f * 1000.0 + 0.5), carrier);
    modulationSetPhaseIncrement(inc);
    lastWrittenInc = inc;
    steering = true;
  }
  stateCode = 3;
}

void pllTask() {
  if (!config.Pll.Enabled) {
    stateCode = 0;
    return;
  }
  if (!captureActive()) {
    stateCode = 1;
    dropLock();
    return;
  }
  if (!spwmActive() || !modulationDdsDriven()) {
    stateCode = 2; // stepped playback or modulation halted: steering meaningless
    dropLock();
    return;
  }
  if (captureIsFrozen()) {
    // Scope trigger or fault froze the ring while the DDS kept running: the
    // frozen backlog can no longer be paired with a phase, so discard it and
    // coast on the held frequency until samples resume
    cursor = captureSampleCount();
    stateCode = 5;
    dropLock();
    return;
  }

  const uint32_t carrier = config.Pwm.Tm2.SpwmCarrierFrequency;

  // Self-heal: something else rewrote the increment (live waveform-upload
  // LUT rebuild, a path that forgot pllConfigure). Reassert our command.
  if (steering && modulationPhaseIncrement() != lastWrittenInc) {
    modulationSetPhaseIncrement(lastWrittenInc);
  }

  // Coherent (sampleCount, ddsPhase) snapshot: the ISR advances both; retry
  // until the count reads stably around the phase read. Residual hazard: a
  // snapshot landing inside the ISR between its phase write and captureTick
  // pairs the batch one carrier tick ahead - an intermittent sub-degree
  // angle error the loop filter absorbs.
  uint32_t t, ph;
  do {
    t = captureSampleCount();
    ph = modulationPhaseNow();
  } while (captureSampleCount() != t);

  const uint32_t lag = t - cursor; // modular: survives counter wrap/reset
  if (lag == 0) {
    if (steering && millis() - lastSampleMs > 250) {
      stateCode = 5; // capture paused: coast on the held frequency
      dropLock();
    }
    return;
  }
  if (lag > pllBacklogLimit(carrier)) {
    // Long stall/counter reset: every accepted backlog is drained in this pass.
    // Anything larger would span multiple increment writes and make the next
    // pass reconstruct old samples using a new increment.
    cursor = t;
    resyncs++;
    dropLock();
    return;
  }
  lastSampleMs = millis();

  // Park-angle mirror: the drained samples were generated under the
  // snapshot-time increment, so the mirror advances with THAT increment for
  // the whole backlog even though newer commands are written per chunk below
  const uint32_t inc = modulationPhaseIncrement();
  uint32_t mirror = ph - lag * inc;

  static uint16_t buf[512];
  uint32_t processed = 0;
  uint32_t n;
  while (processed < PllMaxDrainSamples && (n = captureReadSince(&cursor, buf, 512)) > 0) {
    for (uint32_t i = 0; i < n; i++) {
      const float theta = static_cast<float>(mirror) * (PllTwoPi / 4294967296.0f) - offsetRad;
      pllStep(st, params, buf[i], theta);
      mirror += inc;
    }
    processed += n;
    // Steer per chunk: bounds actuation staleness during catch-up bursts
    // (writes only affect samples generated after they land)
    const uint32_t newInc = pllIncrementFromMilliHz(
      static_cast<uint64_t>(llroundf(st.fCmd * 1000.0f)), carrier);
    modulationSetPhaseIncrement(newInc);
    lastWrittenInc = newInc;
    steering = true;
  }

  stateCode = st.locked ? 4 : (st.aLpf < params.aFloor ? 6 : 3);
}

const char *pllStateStr() {
  return stateNames[stateCode];
}

bool pllLocked() {
  return config.Pll.Enabled && st.locked;
}

uint64_t pllRefMilliHz() {
  return static_cast<uint64_t>(st.fHat * 1000.0 + 0.5);
}

int32_t pllPhaseErrCentiDeg() {
  return ::pllPhaseErrCentiDeg(st);
}

uint32_t pllRefMillivolts() {
  // aLpf is per-unit of 2047 counts; back to pin millivolts
  return static_cast<uint32_t>(st.aLpf * 2047.0f * 3300.0f / 4095.0f);
}

uint32_t pllResyncCount() {
  return resyncs;
}
