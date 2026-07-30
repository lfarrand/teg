// Tests for the inverter modulation engine (modulation.h)

#include <unity.h>
#include <modulation.h>

static int16_t lut[SpwmLutSize];

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Unit reference LUT: sine, THIPWM, trapezoid, square
// ---------------------------------------------------------------------------

void test_sine_unit_lut_key_points_and_antisymmetry() {
  buildUnitReferenceLut(lut, SpwmLutSize, RefWaveSine, false);
  TEST_ASSERT_EQUAL_INT16(0, lut[0]);
  TEST_ASSERT_EQUAL_INT16(32767, lut[SpwmLutSize / 4]);
  TEST_ASSERT_EQUAL_INT16(0, lut[SpwmLutSize / 2]);
  TEST_ASSERT_EQUAL_INT16(-32767, lut[3 * SpwmLutSize / 4]);
  for (uint32_t i = 0; i < SpwmLutSize / 2; i++) {
    TEST_ASSERT_EQUAL_INT16(-lut[i], lut[i + SpwmLutSize / 2]);
  }
}

void test_thipwm_unit_lut_flattened_crest() {
  buildUnitReferenceLut(lut, SpwmLutSize, RefWaveSine, true);
  TEST_ASSERT_INT16_WITHIN(3, 28377, lut[SpwmLutSize / 6]); // 0.866 * 32767 at 60deg
  TEST_ASSERT_INT16_WITHIN(3, 27306, lut[SpwmLutSize / 4]); // (5/6) * 32767 at 90deg
}

void test_square_unit_lut() {
  buildUnitReferenceLut(lut, SpwmLutSize, RefWaveSquare, false);
  for (uint32_t i = 0; i < SpwmLutSize / 2; i++) {
    TEST_ASSERT_EQUAL_INT16(32767, lut[i]);
    TEST_ASSERT_EQUAL_INT16(-32767, lut[i + SpwmLutSize / 2]);
  }
}

void test_trapezoid_unit_lut() {
  buildUnitReferenceLut(lut, SpwmLutSize, RefWaveTrapezoid, false);
  TEST_ASSERT_EQUAL_INT16(0, lut[0]);
  TEST_ASSERT_INT16_WITHIN(100, 16384, lut[SpwmLutSize / 12]); // ~30deg: halfway up the ramp
  TEST_ASSERT_EQUAL_INT16(32767, lut[SpwmLutSize / 4]);        // 90deg: flat top
  TEST_ASSERT_INT16_WITHIN(40, 32767, lut[SpwmLutSize / 6]);   // ~60deg: top of the ramp
  TEST_ASSERT_INT16_WITHIN(100, -16384, lut[SpwmLutSize / 2 + SpwmLutSize / 12]);
}

void test_full_index_duty_matches_legacy_uint16_table() {
  buildUnitReferenceLut(lut, SpwmLutSize, RefWaveSine, false);
  static uint16_t legacy[SpwmLutSize];
  fillSpwmLut(legacy, SpwmLutSize);
  TEST_ASSERT_EQUAL_UINT32(32768, indexMilliToQ15(1000));
  for (uint32_t i = 0; i < SpwmLutSize; i++) {
    const uint32_t phase = i << (32 - SpwmLutBits);
    TEST_ASSERT_EQUAL_UINT16(legacy[i], refToDuty(refFromPhase(lut, phase), 32768, 0));
  }
}

// ---------------------------------------------------------------------------
// Runtime index scaling, overmodulation, dead-time comp, soft-start
// ---------------------------------------------------------------------------

void test_index_scaling_and_overmodulation_clamp() {
  buildUnitReferenceLut(lut, SpwmLutSize, RefWaveSine, false);
  const int32_t peak = refFromPhase(lut, 1UL << 30);
  TEST_ASSERT_EQUAL_UINT16(32768 + 16383, refToDuty(peak, indexMilliToQ15(500), 0)); // Q15 floor
  TEST_ASSERT_EQUAL_UINT16(32768, refToDuty(peak, 0, 0));
  TEST_ASSERT_EQUAL_UINT16(65535, refToDuty(peak, indexMilliToQ15(1155), 0));
  TEST_ASSERT_EQUAL_UINT16(1, refToDuty(-peak, indexMilliToQ15(1155), 0));
}

void test_deadtime_comp_magnitude_and_sign() {
  TEST_ASSERT_EQUAL_INT32(65, deadtimeCompQ15(50, 20000));
  TEST_ASSERT_EQUAL_UINT16(32768 + 1000 + 65, refToDuty(1000, 32768, 65));
  TEST_ASSERT_EQUAL_UINT16(32768 - 1000 - 65, refToDuty(-1000, 32768, 65));
  TEST_ASSERT_EQUAL_UINT16(32768, refToDuty(0, 32768, 65));
}

void test_soft_start_ramp() {
  TEST_ASSERT_EQUAL_UINT32(3, softStartStepQ15(32768, 500, 20000));
  TEST_ASSERT_EQUAL_UINT32(1UL << 30, softStartStepQ15(32768, 0, 20000));
  uint32_t idx = 0;
  for (int i = 0; i < 12000 && idx != 32768; i++) {
    idx = rampIndexQ15(idx, 32768, 3);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(32768, idx);
  }
  TEST_ASSERT_EQUAL_UINT32(32768, idx);
  TEST_ASSERT_EQUAL_UINT32(100, rampIndexQ15(102, 100, 3)); // no undershoot
}

// ---------------------------------------------------------------------------
// Three-phase: SVPWM and the DPWM family
// ---------------------------------------------------------------------------

void test_degrees_to_phase() {
  TEST_ASSERT_UINT32_WITHIN(2, PhaseShift120Deg, degreesToPhase(120));
  TEST_ASSERT_EQUAL_UINT32(0, degreesToPhase(0));
  TEST_ASSERT_UINT32_WITHIN(2, 0U - PhaseShift120Deg, degreesToPhase(-120));
}

void test_svm_zss_centres_envelope_and_preserves_line_to_line() {
  buildUnitReferenceLut(lut, SpwmLutSize, RefWaveSine, false);
  const uint32_t q = indexMilliToQ15(1000);
  for (uint32_t phase = 0; phase < 0xF0000000UL; phase += 0x08000000UL) {
    int32_t v[3];
    threePhaseScaledRefs(lut, phase, q, v);
    const int32_t zss = continuousSvmZss(v);
    int32_t mx = v[0] + zss, mn = v[0] + zss;
    for (int k = 1; k < 3; k++) {
      const int32_t w = v[k] + zss;
      if (w > mx) mx = w;
      if (w < mn) mn = w;
    }
    TEST_ASSERT_INT32_WITHIN(1, 0, mx + mn);                 // envelope centred
    TEST_ASSERT_EQUAL_INT32(v[0] - v[1], (v[0] + zss) - (v[1] + zss)); // line-to-line untouched
  }
}

void test_svm_linear_at_max_index() {
  buildUnitReferenceLut(lut, SpwmLutSize, RefWaveSine, false);
  const uint32_t q = indexMilliToQ15(1155);
  const uint32_t inc = spwmPhaseIncrement(20000, 50);
  uint32_t phase = 0;
  for (int i = 0; i < 400; i++) {
    int32_t v[3];
    threePhaseScaledRefs(lut, phase, q, v);
    const int32_t zss = continuousSvmZss(v);
    for (int k = 0; k < 3; k++) {
      TEST_ASSERT_GREATER_OR_EQUAL_UINT16(1, dutyFromScaled(v[k] + zss, 0, v[k]));
    }
    phase += inc;
  }
}

// One phase must sit exactly on a rail every cycle, line-to-line must be
// unaffected, and over a fundamental each phase should be clamped about 1/3
// of the time (120deg for MIN/MAX, 2x60deg for GDPWM at 0deg)
static void checkDpwmVariant(uint8_t variant, uint32_t clampAnglePhase) {
  const uint32_t q = indexMilliToQ15(1000);
  const uint32_t inc = spwmPhaseIncrement(20000, 50);
  int clamped[3] = {0, 0, 0};
  uint32_t phase = 0;
  for (int i = 0; i < 400; i++) {
    int32_t v[3];
    threePhaseScaledRefs(lut, phase, q, v);
    const int32_t zss = dpwmZss(lut, phase, clampAnglePhase, variant, v);
    bool railHit = false;
    for (int k = 0; k < 3; k++) {
      const int32_t w = v[k] + zss;
      TEST_ASSERT_LESS_OR_EQUAL_INT32(32767, w);
      TEST_ASSERT_GREATER_OR_EQUAL_INT32(-32767, w);
      if (w == 32767 || w == -32767) {
        clamped[k]++;
        railHit = true;
      }
    }
    TEST_ASSERT_TRUE(railHit); // some phase is always pinned to a rail
    TEST_ASSERT_EQUAL_INT32(v[1] - v[2], (v[1] + zss) - (v[2] + zss));
    phase += inc;
  }
  for (int k = 0; k < 3; k++) {
    TEST_ASSERT_INT_WITHIN(30, 133, clamped[k]); // ~1/3 of 400 samples each
  }
}

void test_dpwm_min_max_gdpwm_variants() {
  buildUnitReferenceLut(lut, SpwmLutSize, RefWaveSine, false);
  checkDpwmVariant(DpwmMin, 0);
  checkDpwmVariant(DpwmMax, 0);
  checkDpwmVariant(DpwmGeneralised, 0);                   // DPWM1
  checkDpwmVariant(DpwmGeneralised, degreesToPhase(30));  // DPWM2
  checkDpwmVariant(DpwmGeneralised, degreesToPhase(-30)); // DPWM0
}

void test_dpwm_min_clamps_low_max_clamps_high() {
  buildUnitReferenceLut(lut, SpwmLutSize, RefWaveSine, false);
  const uint32_t q = indexMilliToQ15(1000);
  int32_t v[3];
  threePhaseScaledRefs(lut, 1UL << 28, q, v); // arbitrary phase
  const int32_t zMin = dpwmZss(lut, 1UL << 28, 0, DpwmMin, v);
  const int32_t zMax = dpwmZss(lut, 1UL << 28, 0, DpwmMax, v);
  int32_t mn = v[0], mx = v[0];
  for (int k = 1; k < 3; k++) {
    if (v[k] < mn) mn = v[k];
    if (v[k] > mx) mx = v[k];
  }
  TEST_ASSERT_EQUAL_INT32(-32767, mn + zMin); // lowest phase on the negative rail
  TEST_ASSERT_EQUAL_INT32(32767, mx + zMax);  // highest phase on the positive rail
}

void test_dpwm3_clamps_intermediate_phase() {
  buildUnitReferenceLut(lut, SpwmLutSize, RefWaveSine, false);
  const uint32_t q = indexMilliToQ15(1000);
  const uint32_t phase = degreesToPhase(20); // magnitudes distinct: |a|,|b|,|c|
  int32_t v[3];
  threePhaseScaledRefs(lut, phase, q, v);
  const int32_t zss = dpwmZss(lut, phase, 0, Dpwm3, v);
  // Identify the intermediate-magnitude phase and confirm it landed on a rail
  int lo = 0, hi = 0;
  int32_t mags[3];
  for (int k = 0; k < 3; k++) mags[k] = v[k] >= 0 ? v[k] : -v[k];
  for (int k = 1; k < 3; k++) {
    if (mags[k] > mags[hi]) hi = k;
    if (mags[k] < mags[lo]) lo = k;
  }
  const int mid = 3 - hi - lo;
  const int32_t w = v[mid] + zss;
  TEST_ASSERT_TRUE(w == 32767 || w == -32767);
}

// Four-leg 3D-SVPWM: phase legs carry v+zss, the neutral leg carries zss, so
// the switched phase-to-neutral voltage equals the pure reference exactly
void test_fourleg_phase_to_neutral_is_pure_reference() {
  buildUnitReferenceLut(lut, SpwmLutSize, RefWaveSine, false);
  const uint32_t q = indexMilliToQ15(1000);
  const uint32_t inc = spwmPhaseIncrement(20000, 50);
  uint32_t phase = 0;
  for (int i = 0; i < 400; i++) {
    int32_t v[3];
    threePhaseScaledRefs(lut, phase, q, v);
    const int32_t zss = continuousSvmZss(v);
    const int32_t neutralDuty = dutyFromScaled(zss, 0, 0);
    for (int k = 0; k < 3; k++) {
      const int32_t legDuty = dutyFromScaled(v[k] + zss, 0, v[k]);
      // At index 1.0 nothing saturates, so the duty difference IS the reference
      TEST_ASSERT_EQUAL_INT32(v[k], legDuty - neutralDuty);
    }
    phase += inc;
  }
}

// ---------------------------------------------------------------------------
// Nearest Level Modulation (level-shifted staircase)
// ---------------------------------------------------------------------------

void test_nlm_snaps_cells_to_rails() {
  // Every cell duty is exactly 0 or 65535; the number of fully-on cells is
  // the reference rounded to the nearest level
  for (uint32_t ref = 0; ref <= 65535; ref += 331) {
    uint32_t on = 0;
    for (uint8_t cell = 0; cell < 4; cell++) {
      const uint16_t d = lsCellDuty((uint16_t)ref, cell, 4, true);
      TEST_ASSERT_TRUE(d == 0 || d == 65535);
      if (d == 65535) on++;
    }
    const uint32_t expectedOn = (ref * 4U + 32768U) >> 16; // round(ref * 4 / 65536)
    TEST_ASSERT_EQUAL_UINT32(expectedOn, on);
  }
}

void test_nlm_boundary_and_linear_mode_unchanged() {
  // Band midpoint snaps up, just below snaps down
  TEST_ASSERT_EQUAL_UINT16(65535, lsCellDuty(49152, 1, 2, true)); // 50% into band -> up
  TEST_ASSERT_EQUAL_UINT16(0, lsCellDuty(49151, 1, 2, true));     // just below -> down
  // Default (linear) behaviour is untouched
  TEST_ASSERT_EQUAL_UINT16(32768, lsCellDuty(49152, 1, 2));
  TEST_ASSERT_EQUAL_UINT16(32768, lsCellDuty(49152, 1, 2, false));
  // Carrier-geometry complement stays exact at the NLM endpoints
  const CellPlan comp{true, true};
  TEST_ASSERT_EQUAL_UINT16(65535, modulationFinalDuty(lsCellDuty(0, 1, 2, true), comp));
  TEST_ASSERT_EQUAL_UINT16(0, modulationFinalDuty(lsCellDuty(65535, 1, 2, true), comp));
}

// ---------------------------------------------------------------------------
// Carrier dither (spread spectrum)
// ---------------------------------------------------------------------------

void test_dither_tables_span_and_matched_increments() {
  static uint16_t periods[DitherTableSize];
  static uint32_t increments[DitherTableSize];
  buildDitherTables(periods, increments, 150000000, 20000, 50, 10);

  const uint32_t nominal = 150000000 / 20000; // 7500 ticks
  TEST_ASSERT_EQUAL_UINT16(nominal - 750, periods[0]);
  TEST_ASSERT_EQUAL_UINT16(nominal + 750, periods[DitherTableSize - 1]);
  for (uint32_t i = 1; i < DitherTableSize; i++) {
    TEST_ASSERT_GREATER_THAN_UINT16(periods[i - 1], periods[i]);
  }
  // Each increment must correspond to its period: inc = mod * period * 2^32 / clk
  for (uint32_t i = 0; i < DitherTableSize; i++) {
    const uint32_t expected = (uint32_t)(((uint64_t)50 * periods[i] << 32) / 150000000ULL);
    TEST_ASSERT_UINT32_WITHIN(1, expected, increments[i]);
  }
  // Centre of the table matches the undithered increment closely
  const uint32_t nominalInc = spwmPhaseIncrement(20000, 50);
  TEST_ASSERT_UINT32_WITHIN(nominalInc / 100, nominalInc,
                            (increments[7] + increments[8]) / 2);
}

void test_dither_percent_clamped() {
  static uint16_t periods[DitherTableSize];
  static uint32_t increments[DitherTableSize];
  buildDitherTables(periods, increments, 150000000, 20000, 50, 99); // clamps to 30
  TEST_ASSERT_EQUAL_UINT16(7500 - 2250, periods[0]);
}

void test_lfsr_cycles_and_never_zero() {
  uint16_t s = 0xACE1;
  uint16_t seen1000 = 0;
  for (int i = 0; i < 1000; i++) {
    s = nextLfsr16(s);
    TEST_ASSERT_NOT_EQUAL(0, s);
    if (i == 999) seen1000 = s;
  }
  TEST_ASSERT_NOT_EQUAL(0xACE1, seen1000); // it moved
}

void test_triangle_index_sweeps() {
  // 0..15 then back down 14..0 then up again
  TEST_ASSERT_EQUAL_UINT32(0, triangleIndex(0, 16));
  TEST_ASSERT_EQUAL_UINT32(15, triangleIndex(15, 16));
  TEST_ASSERT_EQUAL_UINT32(14, triangleIndex(16, 16));
  TEST_ASSERT_EQUAL_UINT32(0, triangleIndex(30, 16));
  TEST_ASSERT_EQUAL_UINT32(1, triangleIndex(31, 16));
  for (uint32_t c = 0; c < 100; c++) {
    TEST_ASSERT_LESS_THAN_UINT32(16, triangleIndex(c, 16));
  }
}

// ---------------------------------------------------------------------------
// Cell plans and duty mapping
// ---------------------------------------------------------------------------

static void assertPlan(uint8_t scheme, uint8_t disp, uint8_t cell, uint8_t cells,
                       bool inverted, bool complement) {
  const CellPlan p = modulationCellPlan(scheme, disp, cell, cells);
  TEST_ASSERT_EQUAL(inverted, p.polarityInverted);
  TEST_ASSERT_EQUAL(complement, p.dutyComplement);
}

void test_plans_all_schemes() {
  assertPlan(ModSchemeSpwmUnipolar, CarrierPd, 1, 2, false, true);
  assertPlan(ModSchemeSpwmBipolar, CarrierPd, 1, 2, true, false);
  assertPlan(ModSchemeFixed, CarrierPd, 1, 2, false, false);
  assertPlan(ModSchemeSvpwm, CarrierPd, 1, 3, false, false);
  assertPlan(ModSchemeDpwm, CarrierPd, 1, 3, false, false); // three-phase: no carrier games
  for (uint8_t k = 0; k < 4; k++) {
    assertPlan(ModSchemePhaseShifted, CarrierPd, k, 4, k & 1, k & 1);
  }
  assertPlan(ModSchemeLevelShifted, CarrierPod, 1, 4, true, true);
  assertPlan(ModSchemeLevelShifted, CarrierApod, 2, 4, false, false);
}

void test_ls_band_split_and_final_duty() {
  TEST_ASSERT_EQUAL_UINT16(65535, lsCellDuty(49152, 0, 2));
  TEST_ASSERT_EQUAL_UINT16(32768, lsCellDuty(49152, 1, 2));
  const CellPlan comp{true, true};
  TEST_ASSERT_EQUAL_UINT16(65535, modulationFinalDuty(0, comp));
  TEST_ASSERT_EQUAL_UINT16(0, modulationFinalDuty(65535, comp));
}

void test_scheme_requires_polarity_inversion_matches_the_plans() {
  // Derived from the plans, not an allow-list - so it must agree with them exactly.
  TEST_ASSERT_FALSE(schemeRequiresPolarityInversion(ModSchemeSpwmUnipolar, CarrierPd, 2));
  TEST_ASSERT_FALSE(schemeRequiresPolarityInversion(ModSchemeThipwm, CarrierPd, 2));
  TEST_ASSERT_FALSE(schemeRequiresPolarityInversion(ModSchemeFixed, CarrierPd, 2));
  TEST_ASSERT_FALSE(schemeRequiresPolarityInversion(ModSchemeSvpwm, CarrierPd, 3));
  TEST_ASSERT_FALSE(schemeRequiresPolarityInversion(ModSchemeDpwm, CarrierPd, 3));

  TEST_ASSERT_TRUE(schemeRequiresPolarityInversion(ModSchemeSpwmBipolar, CarrierPd, 2));
  TEST_ASSERT_TRUE(schemeRequiresPolarityInversion(ModSchemePhaseShifted, CarrierPd, 4));

  // Level-shifted: PD never inverts, POD and APOD do. This is exactly why the gate
  // is derived rather than a scheme allow-list - the same scheme number differs by
  // carrier disposition.
  TEST_ASSERT_FALSE(schemeRequiresPolarityInversion(ModSchemeLevelShifted, CarrierPd, 4));
  TEST_ASSERT_TRUE(schemeRequiresPolarityInversion(ModSchemeLevelShifted, CarrierPod, 4));
  TEST_ASSERT_TRUE(schemeRequiresPolarityInversion(ModSchemeLevelShifted, CarrierApod, 4));
}

void test_scheme_inversion_agrees_with_every_plan() {
  // Exhaustive cross-check: the helper is true iff some cell's plan says so.
  const uint8_t disps[] = {CarrierPd, CarrierPod, CarrierApod};
  for (uint8_t scheme = 0; scheme <= ModSchemeLevelShifted; scheme++) {
    for (uint8_t d = 0; d < 3; d++) {
      for (uint8_t cells = 1; cells <= MaxModulationCells; cells++) {
        bool any = false;
        for (uint8_t c = 0; c < cells; c++) {
          if (modulationCellPlan(scheme, disps[d], c, cells).polarityInverted) any = true;
        }
        TEST_ASSERT_EQUAL(any, schemeRequiresPolarityInversion(scheme, disps[d], cells));
      }
    }
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_scheme_requires_polarity_inversion_matches_the_plans);
  RUN_TEST(test_scheme_inversion_agrees_with_every_plan);
  RUN_TEST(test_sine_unit_lut_key_points_and_antisymmetry);
  RUN_TEST(test_thipwm_unit_lut_flattened_crest);
  RUN_TEST(test_square_unit_lut);
  RUN_TEST(test_trapezoid_unit_lut);
  RUN_TEST(test_full_index_duty_matches_legacy_uint16_table);
  RUN_TEST(test_index_scaling_and_overmodulation_clamp);
  RUN_TEST(test_deadtime_comp_magnitude_and_sign);
  RUN_TEST(test_soft_start_ramp);
  RUN_TEST(test_degrees_to_phase);
  RUN_TEST(test_svm_zss_centres_envelope_and_preserves_line_to_line);
  RUN_TEST(test_svm_linear_at_max_index);
  RUN_TEST(test_dpwm_min_max_gdpwm_variants);
  RUN_TEST(test_dpwm_min_clamps_low_max_clamps_high);
  RUN_TEST(test_dpwm3_clamps_intermediate_phase);
  RUN_TEST(test_fourleg_phase_to_neutral_is_pure_reference);
  RUN_TEST(test_nlm_snaps_cells_to_rails);
  RUN_TEST(test_nlm_boundary_and_linear_mode_unchanged);
  RUN_TEST(test_dither_tables_span_and_matched_increments);
  RUN_TEST(test_dither_percent_clamped);
  RUN_TEST(test_lfsr_cycles_and_never_zero);
  RUN_TEST(test_triangle_index_sweeps);
  RUN_TEST(test_plans_all_schemes);
  RUN_TEST(test_ls_band_split_and_final_duty);
  return UNITY_END();
}
