#ifndef PLL_MATH_H
#define PLL_MATH_H

// Grid/reference PLL math: SOGI-QSG quadrature generator feeding a
// park-transform SRF-PLL. No hardware dependencies, unit-tested natively.
//
// Topology (single loop - the DDS phase accumulator is the PLL's VCO):
// the capture ISR samples the reference once per carrier cycle; every sample
// is processed at the exact fixed Ts = 1/carrier. The SOGI produces the
// in-phase (va) and quadrature (vb, lagging 90deg) components of the
// reference fundamental; the park transform by the DDS phase mirror gives a
// normalized error vq ~ sin(theta_ref - theta_dds), and a PI drives the DDS
// frequency until both frequency AND phase converge - no accumulator write
// is ever needed, so the output waveform never has a discontinuity.
//
// SOGI discretization: forward Euler on the damping integrator, backward
// Euler on the quadrature integrator (Ciobotaru two-integrator structure;
// this pairing is amplitude-accurate where forward/forward biases +1.1% at
// 20kHz and +28% at a 1kHz carrier). Discrete stability requires
// 4 - 2k*g - g^2 > 0 (g < 1.035 at k = sqrt(2)), and the resonator quality
// degrades well before that - pllParamsInit clamps g to GMax = 0.35 (the
// tested envelope) and validateConfig keeps carrier >= 18x nominal so the
// clamp never silently detunes the center frequency in practice.
//
// Sign convention (pinned by test_reference_lead_raises_frequency): the LUT
// outputs sin(theta_dds); with reference u = A*sin(theta_ref) the SOGI gives
// va ~ A*sin(theta_ref), vb ~ -A*cos(theta_ref), so
//   vq = (va*cos(theta_hat) + vb*sin(theta_hat)) / A ~ sin(theta_ref - theta_hat)
// which is positive when the reference leads - frequency must rise.

#include <stdint.h>
#include <math.h>

constexpr float PllTwoPi = 6.28318530717958648f;
constexpr float PllKSogi = 1.4142135f; // zeta = k/2 = 0.707
constexpr float PllGMax = 0.35f;       // largest tested SOGI step (50Hz @ 1kHz)
constexpr uint32_t PllMaxDrainSamples = 4096;

inline float pllUnitClamp(float value) {
  if (!(value > 0.0f)) return 0.0f;
  return value < 1.0f ? value : 1.0f;
}

// A sample older than this cannot be paired honestly with the increment that
// generated it: the firmware keeps only the current DDS increment, not an
// increment history. It is safer to drop the stale block and reacquire than to
// feed phase-fiction through the PI loop. The absolute cap also guarantees that
// one loop() pass can drain every accepted sample.
inline uint32_t pllBacklogLimit(uint32_t carrierHz) {
  uint32_t quarterSecond = carrierHz / 4U;
  if (quarterSecond == 0) quarterSecond = 1;
  return quarterSecond < PllMaxDrainSamples ? quarterSecond : PllMaxDrainSamples;
}

struct PllParams {
  float g = 0.0f;         // w0 * Ts (SOGI center at nominal frequency)
  float lambdaDc = 0.0f;  // Ts / 1.0s   - residual DC tracker
  float lambdaA = 0.0f;   // Ts / 20ms   - amplitude estimate LPF
  float lambdaM = 0.0f;   // Ts / 100ms  - lock metric + freq stability LPFs
  float kpHz = 0.0f;      // proportional path, Hz per rad of phase error
  float kiHzTs = 0.0f;    // integral path, Hz per rad per sample
  float minHz = 45.0f;    // steering clamp = anti-windup rail
  float maxHz = 55.0f;
  float nominalHz = 50.0f;
  float zeroCounts = 2048.0f;
  float puScale = 1.0f / 2047.0f; // counts -> fraction of half scale
  float aFloor = 0.06f;           // per-unit amplitude floor (signal present)
  uint32_t lockDwell = 2000;      // samples: 100ms of continuous lock quality
  uint32_t unlockDwell = 1000;    // samples: 50ms of continuous bad quality
  uint32_t sigLossDwell = 400;    // samples: 20ms below the amplitude floor
  float mLock = 0.05f;            // lock metric thresholds; scaled with g at
  float mUnlock = 0.15f;          // low carriers where discretization ripple
                                  // grows without the phase lock being worse
  float fStabHz = 0.2f;           // frequency-stability gate, same scaling
};

// Natural frequency capped so forward Euler stays valid: wn*Ts <= 0.1
inline void pllParamsInit(PllParams &p, float carrierHz, float nominalHz, float bandwidthHz,
                          float minHz, float maxHz, uint16_t zeroMv, uint16_t minLevelMv) {
  if (!(carrierHz >= 1.0f) || !(nominalHz > 0.0f) || !(bandwidthHz > 0.0f)) {
    p = PllParams{};
    p.nominalHz = nominalHz > 0.0f ? nominalHz : 50.0f;
    p.minHz = minHz;
    p.maxHz = maxHz;
    return;
  }
  const float ts = 1.0f / carrierHz;
  float fn = bandwidthHz;
  const float fnMax = 0.1f * carrierHz / PllTwoPi;
  if (fn > fnMax) {
    fn = fnMax;
  }
  const float wn = PllTwoPi * fn;
  p.g = PllTwoPi * nominalHz * ts;
  if (p.g > PllGMax) {
    // Defensive only: config validation rejects carrier < 18x nominal. A
    // clamped g means the SOGI center sits below the configured nominal -
    // stable but detuned, never divergent.
    p.g = PllGMax;
  }
  // Direct callers and corrupted configs must not turn the one-pole filters
  // into unstable extrapolators at a very low sampling rate.
  p.lambdaDc = pllUnitClamp(ts / 1.0f);
  p.lambdaA = pllUnitClamp(ts / 0.02f);
  p.lambdaM = pllUnitClamp(ts / 0.1f);
  p.kpHz = 2.0f * 0.7071f * wn / PllTwoPi;    // 2*zeta*wn, output in Hz
  p.kiHzTs = (wn * wn / PllTwoPi) * ts;       // wn^2, per-sample, output in Hz
  p.minHz = minHz;
  p.maxHz = maxHz;
  p.nominalHz = nominalHz;
  p.zeroCounts = static_cast<float>(zeroMv) * 4095.0f / 3300.0f;
  p.puScale = 1.0f / 2047.0f;
  p.aFloor = (static_cast<float>(minLevelMv) * 4095.0f / 3300.0f) * p.puScale;
  if (p.aFloor < 1.0e-4f) p.aFloor = 1.0e-4f;
  p.lockDwell = static_cast<uint32_t>(0.1f * carrierHz);
  p.unlockDwell = static_cast<uint32_t>(0.05f * carrierHz);
  p.sigLossDwell = static_cast<uint32_t>(0.02f * carrierHz);
  if (p.lockDwell == 0) p.lockDwell = 1;
  if (p.unlockDwell == 0) p.unlockDwell = 1;
  if (p.sigLossDwell == 0) p.sigLossDwell = 1;
  p.mLock = 0.05f * (1.0f + 2.0f * p.g);
  p.mUnlock = 0.15f * (1.0f + 2.0f * p.g);
  p.fStabHz = 0.2f * (1.0f + 2.0f * p.g);
}

struct PllState {
  float va = 0.0f;
  float vb = 0.0f;
  float dc = 0.0f;
  float aLpf = 0.0f;
  double fHat = 50.0;   // PI integrator = frequency estimate, Hz. Double on
                        // purpose: near lock the per-sample increment shrinks
                        // to ~2 float ulps of the value and float32 stalls
                        // the final convergence (measured at 200kHz carrier);
                        // the M7 FPU does double in hardware.
  float fCmd = 50.0f;   // last commanded (clamped) frequency, Hz
  float m = 0.15f;      // filtered |vq| lock metric, starts at unlock level
  float fBar = 50.0f;   // slow frequency average for the stability gate
  float vqn = 0.0f;     // last normalized q error (phase error ~ rad)
  uint32_t lockCnt = 0;
  uint32_t badCnt = 0;
  uint32_t sigLossCnt = 0;
  bool locked = false;
};

inline void pllReset(PllState &s, float nominalHz) {
  s = PllState{};
  s.fHat = nominalHz;
  s.fCmd = nominalHz;
  s.fBar = nominalHz;
}

// One sample: raw 12-bit capture count and the park angle theta (radians:
// DDS phase mirror MINUS the configured phase offset). Returns the commanded
// frequency in Hz, clamped to [minHz, maxHz].
inline float pllStep(PllState &s, const PllParams &p, uint16_t raw, float theta) {
  // SOGI-QSG with slow residual-DC tracking (the quadrature path passes DC
  // with gain k, so the config zero alone is not enough)
  const float u0 = (static_cast<float>(raw) - p.zeroCounts) * p.puScale;
  s.dc += p.lambdaDc * (u0 - s.dc);
  const float u = u0 - s.dc;
  const float e = PllKSogi * (u - s.va) - s.vb;
  s.va += p.g * e;
  s.vb += p.g * s.va; // uses the freshly updated va (backward Euler)
  const float a = sqrtf(s.va * s.va + s.vb * s.vb);
  s.aLpf += p.lambdaA * (a - s.aLpf);

  // Non-finite SOGI state (overflow, injected corruption): a NaN here sticks
  // forever - and it never reaches the fCmd guard below, because NaN aLpf
  // makes sig false, which freezes the integrator at a finite value. Reset
  // and free-run; the loop re-acquires from nominal.
  if (!(s.va == s.va) || !(s.vb == s.vb) || !(s.aLpf == s.aLpf)) {
    pllReset(s, p.nominalHz);
    return p.nominalHz;
  }

  const bool sig = s.aLpf >= p.aFloor;

  // Park transform, amplitude-normalized so the loop gain is signal-level
  // independent (floored to bound noise blowup at small amplitude). The
  // discrete SOGI's va leads the input by exactly one sample (0.9deg at
  // 20kHz, 18deg at a 1kHz carrier); advancing the park angle by g = w0*Ts
  // cancels that bias to first order so the lock point stays at theta_ref
  // across the whole carrier range.
  const float thetaC = theta + p.g;
  const float denom = s.aLpf > p.aFloor ? s.aLpf : p.aFloor;
  const float vqn = (s.va * cosf(thetaC) + s.vb * sinf(thetaC)) / denom;
  s.vqn = vqn;

  // PI with clamped-integrator anti-windup; freeze while no signal
  if (sig) {
    s.fHat += static_cast<double>(p.kiHzTs * vqn);
    if (s.fHat < static_cast<double>(p.minHz)) s.fHat = p.minHz;
    if (s.fHat > static_cast<double>(p.maxHz)) s.fHat = p.maxHz;
  }
  float fCmd = static_cast<float>(s.fHat) + (sig ? p.kpHz * vqn : 0.0f);
  if (fCmd < p.minHz) fCmd = p.minHz;
  if (fCmd > p.maxHz) fCmd = p.maxHz;
  if (!(fCmd == fCmd)) {
    // NaN via vqn with finite SOGI state: reset and return IMMEDIATELY -
    // falling through would poison the freshly reset lock metric with the
    // local NaN vqn and lock could never re-assert
    pllReset(s, p.nominalHz);
    return p.nominalHz;
  }
  s.fCmd = fCmd;

  // Lock detection: filtered |phase error| small AND signal present AND the
  // estimate stable AND strictly inside the clamp window, all sustained
  s.m += p.lambdaM * (fabsf(vqn) - s.m);
  const float fHatF = static_cast<float>(s.fHat);
  s.fBar += p.lambdaM * (fHatF - s.fBar);
  const bool insideClamps = fHatF > p.minHz + 0.05f && fHatF < p.maxHz - 0.05f;
  const float fDev = fHatF - s.fBar;
  const bool stable = fDev < p.fStabHz && fDev > -p.fStabHz;
  const bool good = sig && s.m < p.mLock && insideClamps && stable;
  const bool bad = s.m > p.mUnlock || !insideClamps;

  s.lockCnt = good ? s.lockCnt + 1 : 0;
  s.badCnt = bad ? s.badCnt + 1 : 0;
  s.sigLossCnt = sig ? 0 : s.sigLossCnt + 1;

  if (!s.locked && s.lockCnt >= p.lockDwell) {
    s.locked = true;
  }
  if (s.locked && (s.badCnt >= p.unlockDwell || s.sigLossCnt >= p.sigLossDwell)) {
    s.locked = false;
  }
  return fCmd;
}

// Frequency (milli-Hz) to 32-bit DDS increment, round-to-nearest: 1 LSB is
// carrier/2^32 Hz (~4.7uHz at 20kHz) - far below any lock threshold
inline uint32_t pllIncrementFromMilliHz(uint64_t milliHz, uint32_t carrierHz) {
  if (carrierHz == 0) {
    return 0;
  }
  const uint64_t num = (milliHz << 32) + (500ULL * carrierHz);
  return static_cast<uint32_t>(num / (1000ULL * carrierHz));
}

// Phase error estimate for telemetry, centi-degrees (vqn ~ sin(err))
inline int32_t pllPhaseErrCentiDeg(const PllState &s) {
  float v = s.vqn;
  if (v > 1.0f) v = 1.0f;
  if (v < -1.0f) v = -1.0f;
  return static_cast<int32_t>(asinf(v) * (36000.0f / PllTwoPi));
}

#endif
