// Tests for the SOGI-QSG + SRF-PLL math (pll_math.h): closed-loop simulation
// with the DDS phase accumulator as the VCO, exactly as the firmware task
// drives it (sample -> pllStep -> increment write -> accumulator advance).

#include <unity.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <pll_math.h>
#include <spwm_math.h>
#include <modulation.h> // buildUnitReferenceLut/refFromPhase for the sign test

void setUp() {}
void tearDown() {}

namespace {

constexpr float TwoPi = 6.28318530717958648f;

struct Sim {
  PllParams p;
  PllState s;
  uint32_t ddsPhase = 0;
  uint32_t inc = 0;
  double refPhase = 0.0; // radians, double to avoid drift over long runs
  double refHz = 50.0;
  float ampCounts = 1200.0f;
  float biasCounts = 2048.0f;
  uint32_t carrierHz = 20000;
  float offsetRad = 0.0f;

  void init(uint32_t carrier, float nominal, float minHz, float maxHz) {
    carrierHz = carrier;
    pllParamsInit(p, static_cast<float>(carrier), nominal, 20.0f, minHz, maxHz, 1650, 100);
    pllReset(s, nominal);
    inc = pllIncrementFromMilliHz(static_cast<uint64_t>(nominal * 1000.0), carrier);
    ddsPhase = 0;
    refPhase = 0.0;
  }

  // One carrier cycle; extraCounts adds distortion/noise to the sample
  void tick(float extraCounts = 0.0f) {
    float u = biasCounts + ampCounts * sinf(static_cast<float>(refPhase)) + extraCounts;
    if (u < 0.0f) u = 0.0f;
    if (u > 4095.0f) u = 4095.0f;
    const float theta = static_cast<float>(ddsPhase) * (TwoPi / 4294967296.0f) - offsetRad;
    const float fCmd = pllStep(s, p, static_cast<uint16_t>(u + 0.5f), theta);
    inc = pllIncrementFromMilliHz(static_cast<uint64_t>(llroundf(fCmd * 1000.0f)), carrierHz);
    ddsPhase += inc;
    refPhase += TwoPi * refHz / carrierHz;
    if (refPhase > TwoPi) refPhase -= TwoPi;
  }

  void run(uint32_t n, float extraCounts = 0.0f) {
    for (uint32_t i = 0; i < n; i++) {
      tick(extraCounts);
    }
  }

  // Mean frequency estimate over n samples: fHat carries a deterministic 2w
  // ripple (fixed-center SOGI ellipticity), so equilibrium assertions must
  // average over whole ripple cycles rather than sample an instant
  float meanFHat(uint32_t n) {
    double sum = 0.0;
    for (uint32_t i = 0; i < n; i++) {
      tick();
      sum += s.fHat;
    }
    return static_cast<float>(sum / n);
  }

  // Signed (ddsPhase - refPhase) in degrees, wrapped to +/-180
  float ddsLeadDeg() const {
    const float dds = static_cast<float>(ddsPhase) * (360.0f / 4294967296.0f);
    const float ref = static_cast<float>(refPhase) * (360.0f / TwoPi);
    float d = dds - ref;
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
  }
};

} // namespace

void test_sogi_quadrature() {
  // Open-ish loop check of the SOGI itself: once the amplitude LPF (tau =
  // 20ms) has fully settled, the in-phase output correlates with sin(ref),
  // the quadrature with -cos(ref), and the amplitude estimate is within 2%
  Sim sim;
  sim.init(20000, 50.0f, 45.0f, 55.0f);
  sim.run(2000); // 100ms = 5 amplitude-LPF time constants
  double dotA = 0, dotB = 0, ee = 0;
  for (int i = 0; i < 400; i++) { // one full cycle
    const float sr = sinf(static_cast<float>(sim.refPhase));
    const float cr = cosf(static_cast<float>(sim.refPhase));
    sim.tick();
    dotA += sim.s.va * sr;
    dotB += sim.s.vb * -cr;
    ee += static_cast<double>(sim.s.va) * sim.s.va + static_cast<double>(sim.s.vb) * sim.s.vb;
  }
  const float ampPu = 1200.0f / 2047.0f;
  // Correlations: each dot ~ amp/2 * 400 samples when aligned
  TEST_ASSERT_GREATER_THAN_FLOAT(0.95f * ampPu * 200.0f, static_cast<float>(dotA));
  TEST_ASSERT_GREATER_THAN_FLOAT(0.95f * ampPu * 200.0f, static_cast<float>(dotB));
  TEST_ASSERT_FLOAT_WITHIN(0.02f * ampPu, ampPu, sim.s.aLpf);
}

void test_reference_lead_raises_frequency() {
  // Sign-convention canary: a reference that LEADS the DDS must produce a
  // positive q error and a frequency command above nominal. A park-transform
  // sign error turns the loop into positive feedback - this test catches it.
  Sim sim;
  sim.init(20000, 50.0f, 45.0f, 55.0f);
  sim.run(20000); // settle into alignment
  sim.refPhase += 10.0 * TwoPi / 360.0; // reference jumps 10deg ahead
  float vqSum = 0.0f;
  for (int i = 0; i < 100; i++) {
    sim.tick();
    vqSum += sim.s.vqn;
  }
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, vqSum);
  TEST_ASSERT_GREATER_THAN_FLOAT(50.0f, sim.s.fCmd);
}

void test_acquisition_from_2hz_error() {
  Sim sim;
  sim.init(20000, 50.0f, 45.0f, 55.0f);
  sim.refHz = 52.0;
  sim.run(1600); // 80ms: most of the way there (linear settle ~52ms + SOGI lag)
  TEST_ASSERT_FLOAT_WITHIN(0.15f, 52.0f, static_cast<float>(sim.s.fHat));
  sim.run(8400); // to 500ms total
  TEST_ASSERT_TRUE(sim.s.locked);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 52.0f, sim.meanFHat(2000));
  // The fixed-center SOGI has a known static detune error (~1.4deg/Hz after
  // the one-sample park compensation) - a calibration quantity, not a fault
  TEST_ASSERT_FLOAT_WITHIN(5.0f, 0.0f, sim.ddsLeadDeg());
}

void test_phase_offset() {
  // PhaseOffset = +90deg: converged DDS fundamental leads the reference.
  // Residual bias after the one-sample park compensation: the DC tracker's
  // 0.18deg lead plus 2w ripple - bench calibration absorbs what's left.
  Sim sim;
  sim.init(20000, 50.0f, 45.0f, 55.0f);
  sim.offsetRad = TwoPi / 4.0f;
  sim.run(20000); // 1s
  TEST_ASSERT_TRUE(sim.s.locked);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 90.0f, sim.ddsLeadDeg());
}

void test_distortion_immunity() {
  // 4% 3rd + 3% 5th harmonic + a 100-count zero-calibration error, present
  // from the start (the tau=1s DC tracker needs ~3 time constants to settle
  // before ripple measurement is meaningful)
  Sim sim;
  sim.init(20000, 50.0f, 45.0f, 55.0f);
  float fMin = 100.0f, fMax = 0.0f, errMax = 0.0f;
  for (int i = 0; i < 70000; i++) { // 3.5s
    const float th = static_cast<float>(sim.refPhase);
    const float extra = 48.0f * sinf(3.0f * th) + 36.0f * sinf(5.0f * th) + 100.0f;
    sim.tick(extra);
    if (i > 60000) { // measure over the last 0.5s
      const float f = static_cast<float>(sim.s.fHat);
      if (f < fMin) fMin = f;
      if (f > fMax) fMax = f;
      const float e = fabsf(sim.ddsLeadDeg());
      if (e > errMax) errMax = e;
    }
  }
  TEST_ASSERT_TRUE(sim.s.locked);
  TEST_ASSERT_LESS_THAN_FLOAT(0.35f, fMax - fMin);
  TEST_ASSERT_LESS_THAN_FLOAT(2.0f, errMax);
}

void test_phase_jump_rides_through() {
  // +30deg reference step: the P path rails the command at the clamp, the
  // phase slews across in tens of ms, and the lock flag never drops
  Sim sim;
  sim.init(20000, 50.0f, 45.0f, 55.0f);
  sim.run(20000);
  TEST_ASSERT_TRUE(sim.s.locked);
  sim.refPhase += 30.0 * TwoPi / 360.0;
  bool stayedLocked = true;
  float fPeak = 0.0f;
  for (int i = 0; i < 2000; i++) { // 100ms
    sim.tick();
    stayedLocked = stayedLocked && sim.s.locked;
    if (sim.s.fCmd > fPeak) fPeak = sim.s.fCmd;
  }
  TEST_ASSERT_TRUE(stayedLocked);
  TEST_ASSERT_LESS_OR_EQUAL_FLOAT(55.0f, fPeak); // clamp respected
  TEST_ASSERT_GREATER_THAN_FLOAT(53.0f, fPeak);  // and actually used to slew
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, sim.ddsLeadDeg());
  // 150ms more: integrator unwound, no residual frequency or phase offset
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 50.0f, sim.meanFHat(3000));
  TEST_ASSERT_FLOAT_WITHIN(0.6f, 0.0f, sim.ddsLeadDeg());
}

void test_out_of_range_reference_clamps() {
  // 70Hz reference with 45-55 clamps: the error beats (the loop can drift to
  // either rail - which one is model-fragile, and both are correct behavior),
  // but it must never lock and the clamps must hold. When the reference
  // returns in-range, the clamped integrator re-acquires with no unwind lag.
  Sim sim;
  sim.init(20000, 50.0f, 45.0f, 55.0f);
  sim.refHz = 70.0;
  bool everLocked = false;
  for (int i = 0; i < 20000; i++) { // 1s
    sim.tick();
    everLocked = everLocked || sim.s.locked;
    TEST_ASSERT_TRUE(sim.s.fCmd >= 45.0f && sim.s.fCmd <= 55.0f);
  }
  TEST_ASSERT_FALSE(everLocked);
  sim.refHz = 50.5;
  sim.run(14000); // 700ms (may start from the far rail)
  TEST_ASSERT_TRUE(sim.s.locked);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 50.5f, sim.meanFHat(2000));
}

void test_no_signal_coasts_at_nominal() {
  // Flat mid-rail + small noise: below the amplitude floor the integrator
  // freezes at nominal, the command stays there, lock never asserts
  Sim sim;
  sim.init(20000, 50.0f, 45.0f, 55.0f);
  sim.ampCounts = 0.0f;
  srand(42);
  for (int i = 0; i < 20000; i++) {
    sim.tick(static_cast<float>(rand() % 41 - 20));
  }
  TEST_ASSERT_FALSE(sim.s.locked);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, sim.s.fCmd);
}

void test_white_noise_no_false_lock() {
  Sim sim;
  sim.init(20000, 50.0f, 45.0f, 55.0f);
  sim.ampCounts = 0.0f;
  srand(1234);
  bool everLocked = false;
  for (int i = 0; i < 40000; i++) { // 2s
    sim.tick(static_cast<float>(rand() % 601 - 300)); // sigma ~ 300 counts
    everLocked = everLocked || sim.s.locked;
    TEST_ASSERT_TRUE(sim.s.fCmd >= 45.0f && sim.s.fCmd <= 55.0f);
  }
  TEST_ASSERT_FALSE(everLocked);
}

void test_increment_conversion() {
  // Round-trip against the firmware's own increment-to-milliHz reporter
  TEST_ASSERT_EQUAL_UINT32(10737418, pllIncrementFromMilliHz(50000, 20000));
  const uint32_t carriers[3] = {1000, 20000, 200000};
  const uint64_t freqs[4] = {40000, 50000, 60000, 400000};
  for (int c = 0; c < 3; c++) {
    for (int f = 0; f < 4; f++) {
      const uint32_t inc = pllIncrementFromMilliHz(freqs[f], carriers[c]);
      const uint64_t back = spwmActualMilliHz(inc, carriers[c]);
      TEST_ASSERT_UINT64_WITHIN(1, freqs[f], back);
    }
  }
  TEST_ASSERT_EQUAL_UINT32(0, pllIncrementFromMilliHz(50000, 0)); // guard
}

void test_carrier_200k_smoke() {
  Sim sim;
  sim.init(200000, 50.0f, 45.0f, 55.0f);
  sim.refHz = 52.0;
  sim.run(100000); // 500ms
  TEST_ASSERT_TRUE(sim.s.locked);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 52.0f, sim.meanFHat(20000));
}

void test_carrier_1k_capped_bandwidth() {
  // At the 1kHz carrier floor the natural frequency caps at 15.9Hz (forward
  // Euler validity) and g = 0.314 - the largest discretization step we allow
  Sim sim;
  sim.init(1000, 50.0f, 45.0f, 55.0f);
  sim.refHz = 51.0;
  sim.run(2000); // 2s
  TEST_ASSERT_TRUE(sim.s.locked);
  // Mean only: at g = 0.314 the deterministic ripple is large and the static
  // phase offset (~4deg) is a bench-calibration quantity
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 51.0f, sim.meanFHat(1000));
}

void test_low_amplitude_acquisition() {
  // Amplitude-normalization pin: at 350 counts (0.17 pu) the un-normalized
  // loop gain would be ~3.5x low and the 80ms settle assert below would fail
  // - this is the test that breaks if "/ denom" is ever dropped from pllStep
  Sim sim;
  sim.init(20000, 50.0f, 45.0f, 55.0f);
  sim.ampCounts = 350.0f;
  sim.refHz = 52.0;
  sim.run(1600); // 80ms
  TEST_ASSERT_FLOAT_WITHIN(0.15f, 52.0f, static_cast<float>(sim.s.fHat));
  sim.run(8400);
  TEST_ASSERT_TRUE(sim.s.locked);
  TEST_ASSERT_FLOAT_WITHIN(0.03f, 52.0f, sim.meanFHat(2000));
}

void test_stall_catchup() {
  // Firmware model: during a loop() stall the DDS free-runs on the last
  // written increment; the backlog is then drained with the mirror stepped
  // by that SAME increment (correct pairing) and commands re-engage after.
  // A 150ms backlog (3000 samples, under the 250ms resync threshold) must
  // produce only a bounded transient and re-converge quickly.
  Sim sim;
  sim.init(20000, 50.0f, 45.0f, 55.0f);
  sim.refHz = 50.3;
  sim.run(20000);
  TEST_ASSERT_TRUE(sim.s.locked);
  // Stall: samples keep arriving and being processed against the true DDS
  // phase (which free-runs on the frozen increment), but no new increment
  // is applied - exactly the firmware's drain-then-write-per-chunk pairing
  const uint32_t frozenInc = sim.inc;
  for (int i = 0; i < 3000; i++) {
    float u = sim.biasCounts + sim.ampCounts * sinf(static_cast<float>(sim.refPhase));
    const float theta = static_cast<float>(sim.ddsPhase) * (TwoPi / 4294967296.0f);
    pllStep(sim.s, sim.p, static_cast<uint16_t>(u + 0.5f), theta);
    sim.ddsPhase += frozenInc; // actuation frozen during the backlog
    sim.refPhase += TwoPi * sim.refHz / sim.carrierHz;
    if (sim.refPhase > TwoPi) sim.refPhase -= TwoPi;
  }
  sim.inc = pllIncrementFromMilliHz(
    static_cast<uint64_t>(llroundf(sim.s.fCmd * 1000.0f)), sim.carrierHz);
  // Re-converge: bounded excursion, back in lock quickly
  float fMin = 100.0f, fMax = 0.0f;
  for (int i = 0; i < 8000; i++) { // 400ms
    sim.tick();
    const float f = static_cast<float>(sim.s.fHat);
    if (f < fMin) fMin = f;
    if (f > fMax) fMax = f;
  }
  TEST_ASSERT_TRUE(fMin >= 45.0f && fMax <= 55.0f);
  TEST_ASSERT_TRUE(sim.s.locked);
  TEST_ASSERT_FLOAT_WITHIN(0.03f, 50.3f, sim.meanFHat(2000));
}

void test_lut_sign_convention() {
  // Cross-header pin: generate the reference FROM the firmware's own LUT
  // (buildUnitReferenceLut + refFromPhase) instead of sinf, so a change of
  // the LUT convention (cos, inverted) breaks this test even though the
  // PLL-internal math stays self-consistent
  static int16_t lut[SpwmLutSize];
  buildUnitReferenceLut(lut, SpwmLutSize, RefWaveSine, false);
  Sim sim;
  sim.init(20000, 50.0f, 45.0f, 55.0f);
  uint32_t refPhaseU = 0;
  const uint32_t refInc = pllIncrementFromMilliHz(50000, 20000);
  for (int i = 0; i < 20000; i++) {
    const float u = sim.biasCounts +
                    1200.0f * static_cast<float>(refFromPhase(lut, refPhaseU)) / 32767.0f;
    const float theta = static_cast<float>(sim.ddsPhase) * (TwoPi / 4294967296.0f);
    const float f = pllStep(sim.s, sim.p, static_cast<uint16_t>(u + 0.5f), theta);
    sim.inc = pllIncrementFromMilliHz(
      static_cast<uint64_t>(llroundf(f * 1000.0f)), sim.carrierHz);
    sim.ddsPhase += sim.inc;
    refPhaseU += refInc;
  }
  TEST_ASSERT_TRUE(sim.s.locked);
  // Locked with the DDS accumulator aligned to the LUT-generated reference
  int32_t diff = static_cast<int32_t>(sim.ddsPhase - refPhaseU);
  const float leadDeg = static_cast<float>(diff) * (360.0f / 4294967296.0f);
  TEST_ASSERT_FLOAT_WITHIN(1.5f, 0.0f, leadDeg);
}

void test_g_guard_survives_insane_config() {
  // 400Hz nominal on a 1kHz carrier gives g = 2.51 - divergent without the
  // clamp. The clamped SOGI is detuned (can't lock) but must stay finite
  // and inside the steering clamps; validateConfig rejects this combination
  // at the config layer anyway.
  Sim sim;
  sim.init(1000, 400.0f, 390.0f, 400.0f);
  sim.refHz = 400.0;
  for (int i = 0; i < 3000; i++) {
    sim.tick();
    TEST_ASSERT_TRUE(sim.s.fCmd >= 390.0f && sim.s.fCmd <= 400.0f);
    TEST_ASSERT_TRUE(sim.s.va == sim.s.va); // never NaN
  }
}

void test_nan_injection_recovers() {
  // A non-finite SOGI state must reset and re-acquire, not poison the PLL
  Sim sim;
  sim.init(20000, 50.0f, 45.0f, 55.0f);
  sim.run(20000);
  TEST_ASSERT_TRUE(sim.s.locked);
  sim.s.va = 1.0f / 0.0f; // injected corruption (stand-in for any overflow)
  sim.run(20000); // 1s of clean reference
  TEST_ASSERT_TRUE(sim.s.va == sim.s.va);   // finite again
  TEST_ASSERT_TRUE(sim.s.locked);           // re-acquired
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 50.0f, sim.meanFHat(2000));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_sogi_quadrature);
  RUN_TEST(test_reference_lead_raises_frequency);
  RUN_TEST(test_acquisition_from_2hz_error);
  RUN_TEST(test_phase_offset);
  RUN_TEST(test_distortion_immunity);
  RUN_TEST(test_phase_jump_rides_through);
  RUN_TEST(test_out_of_range_reference_clamps);
  RUN_TEST(test_no_signal_coasts_at_nominal);
  RUN_TEST(test_white_noise_no_false_lock);
  RUN_TEST(test_increment_conversion);
  RUN_TEST(test_carrier_200k_smoke);
  RUN_TEST(test_carrier_1k_capped_bandwidth);
  RUN_TEST(test_low_amplitude_acquisition);
  RUN_TEST(test_stall_catchup);
  RUN_TEST(test_lut_sign_convention);
  RUN_TEST(test_g_guard_survives_insane_config);
  RUN_TEST(test_nan_injection_recovers);
  return UNITY_END();
}
