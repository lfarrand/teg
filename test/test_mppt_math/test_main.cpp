// Tests for the adaptive perturb & observe MPPT (mppt_math.h): a simulated
// TEG source (EMF behind internal resistance) gives a concave P(index)
// curve; the tracker must climb it, limit-cycle tightly at the peak, adapt
// when the peak moves, and respect bounds and noise deadbands.

#include <unity.h>
#include <stdlib.h>
#include <mppt_math.h>

void setUp() {}
void tearDown() {}

namespace {

// Concave power curve: peak powerPeakMw at index mppMilli, parabolic falloff
// (a good local model of the TEG matched-load curve)
int32_t tegPowerMw(uint16_t indexMilli, uint16_t mppMilli, int32_t peakMw) {
  const int32_t d = static_cast<int32_t>(indexMilli) - mppMilli;
  return peakMw - (d * d) / 20; // 20: ~2W down at +/-200 milli from the peak
}

MpptParams defaults() {
  MpptParams p;
  p.minIndexMilli = 50;
  p.maxIndexMilli = 1000;
  p.stepMaxMilli = 20;
  p.stepMinMilli = 5;
  p.deadbandMw = 10;
  p.restartDeltaMw = 1000;
  return p;
}

// Run n evaluations against the curve; returns the final index
uint16_t runTracker(MpptState &s, const MpptParams &p, uint16_t mppMilli, int32_t peakMw,
                    int n, int32_t noiseMw = 0, unsigned seed = 1) {
  srand(seed);
  uint16_t idx = s.indexMilli;
  for (int i = 0; i < n; i++) {
    int32_t pw = tegPowerMw(idx, mppMilli, peakMw);
    if (noiseMw > 0) {
      pw += rand() % (2 * noiseMw + 1) - noiseMw;
    }
    idx = mpptStep(s, p, pw);
  }
  return idx;
}

} // namespace

void test_converges_to_peak_from_below() {
  MpptParams p = defaults();
  MpptState s;
  mpptInit(s, 300, p); // peak at 700: long climb
  runTracker(s, p, 700, 5000, 60);
  // Settled: within max-step of the peak, limit-cycling at the minimum step
  TEST_ASSERT_UINT16_WITHIN(25, 700, s.indexMilli);
  TEST_ASSERT_EQUAL_UINT16(p.stepMinMilli, s.stepMilli);
}

void test_converges_from_above() {
  MpptParams p = defaults();
  MpptState s;
  mpptInit(s, 950, p);
  runTracker(s, p, 600, 5000, 60);
  TEST_ASSERT_UINT16_WITHIN(25, 600, s.indexMilli);
}

void test_step_adaptation_accelerates_climb() {
  // With adaptation the 400-milli climb takes far fewer evaluations than
  // distance/stepMax would suggest is comfortable; check the tracker is
  // near the peak in 35 steps (pure fixed-min-step P&O would need 80+)
  MpptParams p = defaults();
  MpptState s;
  mpptInit(s, 300, p);
  runTracker(s, p, 700, 5000, 35);
  TEST_ASSERT_UINT16_WITHIN(40, 700, s.indexMilli);
}

void test_tracks_moving_peak() {
  MpptParams p = defaults();
  MpptState s;
  mpptInit(s, 300, p);
  runTracker(s, p, 600, 5000, 50);
  TEST_ASSERT_UINT16_WITHIN(25, 600, s.indexMilli);
  // Thermal shift moves the peak; tracker follows without a reset
  runTracker(s, p, 750, 5000, 50);
  TEST_ASSERT_UINT16_WITHIN(25, 750, s.indexMilli);
  runTracker(s, p, 500, 5000, 60);
  TEST_ASSERT_UINT16_WITHIN(25, 500, s.indexMilli);
}

void test_restart_on_power_jump() {
  // A step change in source power (restartDelta exceeded) resets the step
  // to maximum for coarse re-tracking
  MpptParams p = defaults();
  MpptState s;
  mpptInit(s, 300, p);
  runTracker(s, p, 600, 5000, 50);
  TEST_ASSERT_EQUAL_UINT16(p.stepMinMilli, s.stepMilli); // refined at peak
  mpptStep(s, p, tegPowerMw(s.indexMilli, 600, 5000) + 3000); // 3W jump
  TEST_ASSERT_EQUAL_UINT16(p.stepMaxMilli, s.stepMilli);      // coarse again
}

void test_deadband_rejects_noise_at_peak() {
  // With +/-8mW noise inside the 10mW deadband the tracker must not wander:
  // it stays within a tight band around the peak over a long run
  MpptParams p = defaults();
  MpptState s;
  mpptInit(s, 300, p);
  runTracker(s, p, 700, 5000, 60);
  uint16_t lo = 65535, hi = 0;
  for (int i = 0; i < 200; i++) {
    const uint16_t idx = runTracker(s, p, 700, 5000, 1, 8, 42 + i);
    if (idx < lo) lo = idx;
    if (idx > hi) hi = idx;
  }
  TEST_ASSERT_UINT16_WITHIN(40, 700, lo);
  TEST_ASSERT_UINT16_WITHIN(40, 700, hi);
}

void test_bounds_clamp_and_turn() {
  // Peak beyond the ceiling: tracker parks at the bound and turns inward
  // instead of pushing past it
  MpptParams p = defaults();
  MpptState s;
  mpptInit(s, 900, p);
  runTracker(s, p, 1200, 5000, 30); // "peak" outside [50, 1000]
  TEST_ASSERT_UINT16_WITHIN(25, 1000, s.indexMilli);
  // And the floor
  MpptState s2;
  mpptInit(s2, 200, p);
  runTracker(s2, p, 0, 5000, 30);
  TEST_ASSERT_UINT16_WITHIN(25, 50, s2.indexMilli);
}

void test_flat_plateau_stays_bounded() {
  // Output power saturated (e.g. cycle-by-cycle current limiting): every
  // delta is inside the deadband. The flat-cap must bound the wander to a
  // small sawtooth instead of sweeping the index rail to rail.
  MpptParams p = defaults();
  MpptState s;
  mpptInit(s, 500, p);
  uint16_t lo = 65535, hi = 0;
  for (int i = 0; i < 200; i++) {
    const uint16_t idx = mpptStep(s, p, 3000); // dead-flat power
    if (idx < lo) lo = idx;
    if (idx > hi) hi = idx;
  }
  TEST_ASSERT_GREATER_OR_EQUAL_UINT16(380, lo);
  TEST_ASSERT_LESS_OR_EQUAL_UINT16(620, hi);
}

void test_shallow_curve_never_reaches_rails() {
  // A locally-flat peak (K=200: ~10x shallower than the other tests) hides
  // the gradient inside the deadband over a wide radius. The tracker parks
  // in that blind region - off-peak is information-theoretically unavoidable
  // - but must never sweep out to the index rails.
  MpptParams p = defaults();
  MpptState s;
  mpptInit(s, 300, p);
  uint16_t lo = 65535, hi = 0;
  for (int i = 0; i < 300; i++) {
    const int32_t d = static_cast<int32_t>(s.indexMilli) - 700;
    const uint16_t idx = mpptStep(s, p, 5000 - (d * d) / 200);
    if (i > 50) {
      if (idx < lo) lo = idx;
      if (idx > hi) hi = idx;
    }
  }
  TEST_ASSERT_GREATER_THAN_UINT16(300, lo);  // never back to the start
  TEST_ASSERT_LESS_THAN_UINT16(990, hi);     // never at the ceiling
  TEST_ASSERT_UINT16_WITHIN(280, 700, lo);   // parked inside the blind radius
  TEST_ASSERT_UINT16_WITHIN(280, 700, hi);
}

void test_init_clamps_seed() {
  MpptParams p = defaults();
  MpptState s;
  mpptInit(s, 5, p);
  TEST_ASSERT_EQUAL_UINT16(50, s.indexMilli);
  mpptInit(s, 1100, p);
  TEST_ASSERT_EQUAL_UINT16(1000, s.indexMilli);
}

void test_first_step_perturbs_immediately() {
  MpptParams p = defaults();
  MpptState s;
  mpptInit(s, 500, p);
  const uint16_t next = mpptStep(s, p, 4000);
  TEST_ASSERT_EQUAL_UINT16(500 + p.stepMaxMilli, next); // baseline + first move
  TEST_ASSERT_TRUE(s.primed);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_converges_to_peak_from_below);
  RUN_TEST(test_converges_from_above);
  RUN_TEST(test_step_adaptation_accelerates_climb);
  RUN_TEST(test_tracks_moving_peak);
  RUN_TEST(test_restart_on_power_jump);
  RUN_TEST(test_deadband_rejects_noise_at_peak);
  RUN_TEST(test_bounds_clamp_and_turn);
  RUN_TEST(test_flat_plateau_stays_bounded);
  RUN_TEST(test_shallow_curve_never_reaches_rails);
  RUN_TEST(test_init_clamps_seed);
  RUN_TEST(test_first_step_perturbs_immediately);
  return UNITY_END();
}
