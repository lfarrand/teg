// Tests for the inverter modulation engine (modulation.h)

#include <unity.h>
#include <modulation.h>

static int16_t lut[SpwmLutSize];

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Unit reference LUT
// ---------------------------------------------------------------------------

void test_sine_unit_lut_key_points_and_antisymmetry() {
  buildUnitReferenceLut(lut, SpwmLutSize, false);
  TEST_ASSERT_EQUAL_INT16(0, lut[0]);
  TEST_ASSERT_EQUAL_INT16(32767, lut[SpwmLutSize / 4]);
  TEST_ASSERT_EQUAL_INT16(0, lut[SpwmLutSize / 2]);
  TEST_ASSERT_EQUAL_INT16(-32767, lut[3 * SpwmLutSize / 4]);
  for (uint32_t i = 0; i < SpwmLutSize / 2; i++) {
    TEST_ASSERT_EQUAL_INT16(-lut[i], lut[i + SpwmLutSize / 2]);
  }
}

void test_thipwm_unit_lut_flattened_crest() {
  buildUnitReferenceLut(lut, SpwmLutSize, true);
  // Composite peaks at ~0.866 near 60deg, dips to 5/6 at 90deg
  TEST_ASSERT_INT16_WITHIN(3, 28377, lut[SpwmLutSize / 6]);      // 0.866 * 32767
  TEST_ASSERT_INT16_WITHIN(3, 27306, lut[SpwmLutSize / 4]);      // (5/6) * 32767
  int16_t peak = 0;
  for (uint32_t i = 0; i < SpwmLutSize; i++) {
    if (lut[i] > peak) peak = lut[i];
  }
  TEST_ASSERT_INT16_WITHIN(3, 28377, peak);
}

void test_full_index_duty_matches_legacy_uint16_table() {
  // Scaling the signed unit table by Q15 1.0 must reproduce the legacy
  // duty-domain table (spwm_math.h fillSpwmLut) exactly
  buildUnitReferenceLut(lut, SpwmLutSize, false);
  static uint16_t legacy[SpwmLutSize];
  fillSpwmLut(legacy, SpwmLutSize);
  TEST_ASSERT_EQUAL_UINT32(32768, indexMilliToQ15(1000));
  for (uint32_t i = 0; i < SpwmLutSize; i++) {
    const uint32_t phase = i << (32 - SpwmLutBits);
    TEST_ASSERT_EQUAL_UINT16(legacy[i], refToDuty(refFromPhase(lut, phase), 32768, 0));
  }
}

// ---------------------------------------------------------------------------
// Runtime index scaling, overmodulation, THIPWM rail utilisation
// ---------------------------------------------------------------------------

void test_index_scaling_and_overmodulation_clamp() {
  buildUnitReferenceLut(lut, SpwmLutSize, false);
  const int32_t peak = refFromPhase(lut, 1UL << 30); // 90deg
  TEST_ASSERT_EQUAL_UINT16(32768 + 16383, refToDuty(peak, indexMilliToQ15(500), 0)); // Q15 floor
  TEST_ASSERT_EQUAL_UINT16(32768, refToDuty(peak, 0, 0));
  TEST_ASSERT_EQUAL_UINT16(65535, refToDuty(peak, indexMilliToQ15(1155), 0)); // clamps at the rail
  TEST_ASSERT_EQUAL_UINT16(1, refToDuty(-peak, indexMilliToQ15(1155), 0));
}

void test_thipwm_touches_rail_at_max_index_without_clipping_crest() {
  buildUnitReferenceLut(lut, SpwmLutSize, true);
  const uint32_t q = indexMilliToQ15(1155);
  uint16_t maxDuty = 0;
  for (uint32_t i = 0; i < SpwmLutSize; i++) {
    const uint16_t d = refToDuty(lut[i], q, 0);
    if (d > maxDuty) maxDuty = d;
  }
  TEST_ASSERT_UINT16_WITHIN(3, 65535, maxDuty);                       // reaches the rail at 60deg
  TEST_ASSERT_LESS_THAN_UINT16(65000, refToDuty(lut[SpwmLutSize / 4], q, 0)); // 90deg well below it
}

// ---------------------------------------------------------------------------
// Dead-time compensation
// ---------------------------------------------------------------------------

void test_deadtime_comp_magnitude() {
  TEST_ASSERT_EQUAL_INT32(65, deadtimeCompQ15(50, 20000));  // 2*50ns*20kHz = 0.2% -> 65/32768
  TEST_ASSERT_EQUAL_INT32(0, deadtimeCompQ15(0, 20000));
}

void test_deadtime_comp_signed_by_reference_polarity() {
  const int32_t comp = 65;
  const uint16_t plus = refToDuty(1000, 32768, comp);
  const uint16_t minus = refToDuty(-1000, 32768, comp);
  const uint16_t zero = refToDuty(0, 32768, comp);
  TEST_ASSERT_EQUAL_UINT16(32768 + 1000 + 65, plus);
  TEST_ASSERT_EQUAL_UINT16(32768 - 1000 - 65, minus);
  TEST_ASSERT_EQUAL_UINT16(32768, zero); // no correction at the zero crossing
}

// ---------------------------------------------------------------------------
// Soft-start ramp
// ---------------------------------------------------------------------------

void test_soft_start_step_sizing() {
  // 500ms to reach Q15 1.0 at 20kHz = 10000 steps of ~3
  TEST_ASSERT_EQUAL_UINT32(3, softStartStepQ15(32768, 500, 20000));
  TEST_ASSERT_EQUAL_UINT32(1UL << 30, softStartStepQ15(32768, 0, 20000)); // instant
  TEST_ASSERT_EQUAL_UINT32(1, softStartStepQ15(10, 10000, 20000));        // never zero
}

void test_ramp_converges_exactly_without_overshoot() {
  uint32_t idx = 0;
  int cycles = 0;
  while (idx != 32768 && cycles < 20000) {
    idx = rampIndexQ15(idx, 32768, 3);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(32768, idx);
    cycles++;
  }
  TEST_ASSERT_EQUAL_UINT32(32768, idx);
  TEST_ASSERT_INT_WITHIN(50, 10923, cycles); // 32768/3
  // Ramps down too
  TEST_ASSERT_EQUAL_UINT32(32765, rampIndexQ15(32768, 100, 3));
  TEST_ASSERT_EQUAL_UINT32(100, rampIndexQ15(102, 100, 3)); // no undershoot
  TEST_ASSERT_EQUAL_UINT32(500, rampIndexQ15(500, 500, 3)); // steady state
}

// ---------------------------------------------------------------------------
// SVPWM
// ---------------------------------------------------------------------------

void test_svm_zero_sequence_centres_the_envelope() {
  buildUnitReferenceLut(lut, SpwmLutSize, false);
  for (uint32_t phase = 0; phase < 0xF0000000UL; phase += 0x08000000UL) {
    int32_t s[3];
    svmUnitRefs(lut, phase, s);
    int32_t mx = s[0], mn = s[0];
    for (int k = 1; k < 3; k++) {
      if (s[k] > mx) mx = s[k];
      if (s[k] < mn) mn = s[k];
    }
    TEST_ASSERT_INT32_WITHIN(1, 0, mx + mn); // min-max injection centres the envelope
  }
}

void test_svm_line_to_line_unaffected_by_injection() {
  buildUnitReferenceLut(lut, SpwmLutSize, false);
  for (uint32_t phase = 0; phase < 0xF0000000UL; phase += 0x10000000UL) {
    int32_t s[3];
    svmUnitRefs(lut, phase, s);
    const int32_t rawA = refFromPhase(lut, phase);
    const int32_t rawB = refFromPhase(lut, phase - PhaseShift120Deg);
    TEST_ASSERT_INT32_WITHIN(1, rawA - rawB, s[0] - s[1]); // common mode cancels line-to-line
  }
}

void test_svm_linear_at_max_index() {
  // At index 1.1547 the injected refs peak at ~1.0: no more than LSB-level
  // clamping anywhere in the cycle
  buildUnitReferenceLut(lut, SpwmLutSize, false);
  const uint32_t q = indexMilliToQ15(1155);
  const uint32_t inc = spwmPhaseIncrement(20000, 50);
  uint32_t phase = 0;
  for (int i = 0; i < 400; i++) {
    int32_t s[3];
    svmUnitRefs(lut, phase, s);
    for (int k = 0; k < 3; k++) {
      const uint16_t d = refToDuty(s[k], q, 0);
      TEST_ASSERT_GREATER_OR_EQUAL_UINT16(1, d);
    }
    phase += inc;
  }
}

// ---------------------------------------------------------------------------
// Cell plans and duty mapping (unchanged semantics from the duty-domain engine)
// ---------------------------------------------------------------------------

static void assertPlan(uint8_t scheme, uint8_t disp, uint8_t cell, uint8_t cells,
                       bool inverted, bool complement) {
  const CellPlan p = modulationCellPlan(scheme, disp, cell, cells);
  TEST_ASSERT_EQUAL(inverted, p.polarityInverted);
  TEST_ASSERT_EQUAL(complement, p.dutyComplement);
}

void test_plans_all_schemes() {
  assertPlan(ModSchemeSpwmUnipolar, CarrierPd, 0, 2, false, false);
  assertPlan(ModSchemeSpwmUnipolar, CarrierPd, 1, 2, false, true);
  assertPlan(ModSchemeThipwm, CarrierPd, 1, 2, false, true);
  assertPlan(ModSchemeSpwmBipolar, CarrierPd, 1, 2, true, false);
  assertPlan(ModSchemeFixed, CarrierPd, 1, 2, false, false);
  assertPlan(ModSchemeSvpwm, CarrierPd, 1, 3, false, false); // SVPWM: no carrier games
  for (uint8_t k = 0; k < 4; k++) {
    assertPlan(ModSchemePhaseShifted, CarrierPd, k, 4, k & 1, k & 1);
    assertPlan(ModSchemeLevelShifted, CarrierPd, k, 4, false, false);
  }
  assertPlan(ModSchemeLevelShifted, CarrierPod, 0, 4, true, true);
  assertPlan(ModSchemeLevelShifted, CarrierPod, 1, 4, true, true);
  assertPlan(ModSchemeLevelShifted, CarrierPod, 2, 4, false, false);
  assertPlan(ModSchemeLevelShifted, CarrierApod, 1, 4, true, true);
  assertPlan(ModSchemeLevelShifted, CarrierApod, 2, 4, false, false);
}

void test_ls_band_split_and_final_duty() {
  TEST_ASSERT_EQUAL_UINT16(65535, lsCellDuty(49152, 0, 2));
  TEST_ASSERT_EQUAL_UINT16(32768, lsCellDuty(49152, 1, 2));
  TEST_ASSERT_EQUAL_UINT16(0, lsCellDuty(32768, 1, 2));
  TEST_ASSERT_EQUAL_UINT16(32768, lsCellDuty(40960, 2, 4));

  const CellPlan comp{true, true};
  TEST_ASSERT_EQUAL_UINT16(65535, modulationFinalDuty(0, comp));
  TEST_ASSERT_EQUAL_UINT16(0, modulationFinalDuty(65535, comp));
  TEST_ASSERT_EQUAL_UINT16(777, modulationFinalDuty(777, CellPlan{false, false}));
}

void test_unipolar_leg_pair_mirror_invariant() {
  buildUnitReferenceLut(lut, SpwmLutSize, false);
  const CellPlan leg1 = modulationCellPlan(ModSchemeSpwmUnipolar, CarrierPd, 1, 2);
  for (uint32_t i = 0; i < SpwmLutSize; i += 37) {
    const uint32_t phase = i << (32 - SpwmLutBits);
    const uint16_t ref = refToDuty(refFromPhase(lut, phase), 32768, 0);
    const uint16_t d0 = modulationFinalDuty(ref, CellPlan{false, false});
    const uint16_t d1 = modulationFinalDuty(ref, leg1);
    TEST_ASSERT_EQUAL_UINT32(65535, (uint32_t)d0 + (uint32_t)d1);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_sine_unit_lut_key_points_and_antisymmetry);
  RUN_TEST(test_thipwm_unit_lut_flattened_crest);
  RUN_TEST(test_full_index_duty_matches_legacy_uint16_table);
  RUN_TEST(test_index_scaling_and_overmodulation_clamp);
  RUN_TEST(test_thipwm_touches_rail_at_max_index_without_clipping_crest);
  RUN_TEST(test_deadtime_comp_magnitude);
  RUN_TEST(test_deadtime_comp_signed_by_reference_polarity);
  RUN_TEST(test_soft_start_step_sizing);
  RUN_TEST(test_ramp_converges_exactly_without_overshoot);
  RUN_TEST(test_svm_zero_sequence_centres_the_envelope);
  RUN_TEST(test_svm_line_to_line_unaffected_by_injection);
  RUN_TEST(test_svm_linear_at_max_index);
  RUN_TEST(test_plans_all_schemes);
  RUN_TEST(test_ls_band_split_and_final_duty);
  RUN_TEST(test_unipolar_leg_pair_mirror_invariant);
  return UNITY_END();
}
