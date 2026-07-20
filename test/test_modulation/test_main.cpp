// Tests for the inverter modulation engine (modulation.h)

#include <unity.h>
#include <modulation.h>

static uint16_t lut[SpwmLutSize];

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Reference LUT: sine, modulation index, THIPWM
// ---------------------------------------------------------------------------

void test_sine_at_full_index_matches_legacy_table() {
  buildReferenceLut(lut, SpwmLutSize, false, 1000);
  static uint16_t legacy[SpwmLutSize];
  fillSpwmLut(legacy, SpwmLutSize);
  for (uint32_t i = 0; i < SpwmLutSize; i++) {
    TEST_ASSERT_EQUAL_UINT16(legacy[i], lut[i]);
  }
}

void test_modulation_index_scales_amplitude() {
  buildReferenceLut(lut, SpwmLutSize, false, 500); // m = 0.5
  TEST_ASSERT_EQUAL_UINT16(32768, lut[0]);
  TEST_ASSERT_EQUAL_UINT16(32768 + 16384, lut[SpwmLutSize / 4]);     // +peak at half amplitude
  TEST_ASSERT_EQUAL_UINT16(32768 - 16384, lut[3 * SpwmLutSize / 4]); // -peak
}

void test_zero_index_gives_flat_midpoint() {
  buildReferenceLut(lut, SpwmLutSize, false, 0);
  for (uint32_t i = 0; i < SpwmLutSize; i += 17) {
    TEST_ASSERT_EQUAL_UINT16(32768, lut[i]);
  }
}

void test_thipwm_reaches_full_rail_without_clipping_elsewhere() {
  // At m = 1.1547 the composite sin(x) + sin(3x)/6 peaks at exactly 1.0
  // (at 60 and 120 degrees) - the whole point of third harmonic injection
  buildReferenceLut(lut, SpwmLutSize, true, 1155);
  uint16_t maxDuty = 0;
  for (uint32_t i = 0; i < SpwmLutSize; i++) {
    if (lut[i] > maxDuty) maxDuty = lut[i];
  }
  TEST_ASSERT_UINT16_WITHIN(3, 65535, maxDuty);                  // touches the rail at 60 deg
  TEST_ASSERT_LESS_THAN_UINT16(65535, lut[SpwmLutSize / 4]);     // but dips below it at 90 deg
}

void test_thipwm_quarter_point_value() {
  // At 90 deg: ref = m*(1 - 1/6); for m=1.155 that's 0.9625 -> 32768 + 31538
  buildReferenceLut(lut, SpwmLutSize, true, 1155);
  TEST_ASSERT_UINT16_WITHIN(2, 64306, lut[SpwmLutSize / 4]);

  // Plain sine at the same 1155 clamps flat at the rail around 90 deg instead
  buildReferenceLut(lut, SpwmLutSize, false, 1155);
  TEST_ASSERT_EQUAL_UINT16(65535, lut[SpwmLutSize / 4]);
}

void test_reference_antisymmetry_and_bounds() {
  buildReferenceLut(lut, SpwmLutSize, true, 1155);
  for (uint32_t i = 0; i < SpwmLutSize / 2; i++) {
    const uint32_t sum = (uint32_t)lut[i] + (uint32_t)lut[i + SpwmLutSize / 2];
    TEST_ASSERT_EQUAL_UINT32(65536, sum);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(1, lut[i]);
  }
}

// ---------------------------------------------------------------------------
// Level-shifted cell duties
// ---------------------------------------------------------------------------

void test_ls_two_cells_band_split() {
  // ref at 75%: bottom cell saturated on, top cell at 50% of its band
  TEST_ASSERT_EQUAL_UINT16(65535, lsCellDuty(49152, 0, 2));
  TEST_ASSERT_EQUAL_UINT16(32768, lsCellDuty(49152, 1, 2));
  // ref at 0: both off; ref at band boundary: top cell just off
  TEST_ASSERT_EQUAL_UINT16(0, lsCellDuty(0, 0, 2));
  TEST_ASSERT_EQUAL_UINT16(0, lsCellDuty(0, 1, 2));
  TEST_ASSERT_EQUAL_UINT16(0, lsCellDuty(32768, 1, 2));
}

void test_ls_four_cells() {
  const uint16_t ref = 40960; // 62.5% -> cells 0,1 on, cell 2 at half band, cell 3 off
  TEST_ASSERT_EQUAL_UINT16(65535, lsCellDuty(ref, 0, 4));
  TEST_ASSERT_EQUAL_UINT16(65535, lsCellDuty(ref, 1, 4));
  TEST_ASSERT_EQUAL_UINT16(32768, lsCellDuty(ref, 2, 4));
  TEST_ASSERT_EQUAL_UINT16(0, lsCellDuty(ref, 3, 4));
}

void test_ls_monotone_in_reference() {
  for (uint8_t cell = 0; cell < 4; cell++) {
    uint16_t prev = 0;
    for (uint32_t ref = 0; ref <= 65535; ref += 991) {
      const uint16_t d = lsCellDuty((uint16_t)ref, cell, 4);
      TEST_ASSERT_GREATER_OR_EQUAL_UINT16(prev, d);
      prev = d;
    }
  }
}

// ---------------------------------------------------------------------------
// Cell plans (carrier geometry per scheme)
// ---------------------------------------------------------------------------

static void assertPlan(uint8_t scheme, uint8_t disp, uint8_t cell, uint8_t cells,
                       bool inverted, bool complement) {
  const CellPlan p = modulationCellPlan(scheme, disp, cell, cells);
  TEST_ASSERT_EQUAL(inverted, p.polarityInverted);
  TEST_ASSERT_EQUAL(complement, p.dutyComplement);
}

void test_plans_two_leg_schemes() {
  // Unipolar / THIPWM: leg 1 mirrored reference, no polarity games
  assertPlan(ModSchemeSpwmUnipolar, CarrierPd, 0, 2, false, false);
  assertPlan(ModSchemeSpwmUnipolar, CarrierPd, 1, 2, false, true);
  assertPlan(ModSchemeThipwm, CarrierPd, 1, 2, false, true);
  // Bipolar: leg 1 in exact opposition (inverted polarity, same duty)
  assertPlan(ModSchemeSpwmBipolar, CarrierPd, 0, 2, false, false);
  assertPlan(ModSchemeSpwmBipolar, CarrierPd, 1, 2, true, false);
  // Fixed: nothing
  assertPlan(ModSchemeFixed, CarrierPd, 1, 2, false, false);
}

void test_plans_phase_shifted() {
  // Alternating 180deg carriers: odd cells inverted + complemented
  for (uint8_t k = 0; k < 4; k++) {
    assertPlan(ModSchemePhaseShifted, CarrierPd, k, 4, k & 1, k & 1);
  }
}

void test_plans_level_shifted_dispositions() {
  // PD: all in phase
  for (uint8_t k = 0; k < 4; k++) {
    assertPlan(ModSchemeLevelShifted, CarrierPd, k, 4, false, false);
  }
  // POD: lower half (bands below zero) in antiphase
  assertPlan(ModSchemeLevelShifted, CarrierPod, 0, 4, true, true);
  assertPlan(ModSchemeLevelShifted, CarrierPod, 1, 4, true, true);
  assertPlan(ModSchemeLevelShifted, CarrierPod, 2, 4, false, false);
  assertPlan(ModSchemeLevelShifted, CarrierPod, 3, 4, false, false);
  assertPlan(ModSchemeLevelShifted, CarrierPod, 0, 2, true, true);
  assertPlan(ModSchemeLevelShifted, CarrierPod, 1, 2, false, false);
  // APOD: alternate cells
  assertPlan(ModSchemeLevelShifted, CarrierApod, 0, 4, false, false);
  assertPlan(ModSchemeLevelShifted, CarrierApod, 1, 4, true, true);
  assertPlan(ModSchemeLevelShifted, CarrierApod, 2, 4, false, false);
  assertPlan(ModSchemeLevelShifted, CarrierApod, 3, 4, true, true);
}

// ---------------------------------------------------------------------------
// Final duty (ISR half of carrier geometry)
// ---------------------------------------------------------------------------

void test_final_duty_complement_endpoints() {
  const CellPlan comp{true, true};
  const CellPlan plain{false, false};
  TEST_ASSERT_EQUAL_UINT16(65535, modulationFinalDuty(0, comp));     // idle inverted cell = off
  TEST_ASSERT_EQUAL_UINT16(0, modulationFinalDuty(65535, comp));     // saturated inverted cell = on
  TEST_ASSERT_EQUAL_UINT16(32767, modulationFinalDuty(32768, comp));
  TEST_ASSERT_EQUAL_UINT16(12345, modulationFinalDuty(12345, plain));
}

void test_unipolar_leg_pair_covers_both_half_cycles() {
  buildReferenceLut(lut, SpwmLutSize, false, 1000);
  const CellPlan leg1 = modulationCellPlan(ModSchemeSpwmUnipolar, CarrierPd, 1, 2);
  for (uint32_t i = 0; i < SpwmLutSize; i += 37) {
    const uint16_t ref = lut[i];
    const uint16_t d0 = modulationFinalDuty(modulationCellDuty(ModSchemeSpwmUnipolar, ref, 0, 2), CellPlan{false, false});
    const uint16_t d1 = modulationFinalDuty(modulationCellDuty(ModSchemeSpwmUnipolar, ref, 1, 2), leg1);
    TEST_ASSERT_EQUAL_UINT32(65535, (uint32_t)d0 + (uint32_t)d1); // exact mirror pair
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_sine_at_full_index_matches_legacy_table);
  RUN_TEST(test_modulation_index_scales_amplitude);
  RUN_TEST(test_zero_index_gives_flat_midpoint);
  RUN_TEST(test_thipwm_reaches_full_rail_without_clipping_elsewhere);
  RUN_TEST(test_thipwm_quarter_point_value);
  RUN_TEST(test_reference_antisymmetry_and_bounds);
  RUN_TEST(test_ls_two_cells_band_split);
  RUN_TEST(test_ls_four_cells);
  RUN_TEST(test_ls_monotone_in_reference);
  RUN_TEST(test_plans_two_leg_schemes);
  RUN_TEST(test_plans_phase_shifted);
  RUN_TEST(test_plans_level_shifted_dispositions);
  RUN_TEST(test_final_duty_complement_endpoints);
  RUN_TEST(test_unipolar_leg_pair_covers_both_half_cycles);
  return UNITY_END();
}
