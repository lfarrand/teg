#include <unity.h>
#include <initializer_list>
#include "pwm_pair.h"

void setUp() {}
void tearDown() {}

// --- dead-time conversion ---------------------------------------------------

void test_dead_time_cycles_converts_at_bus_clock() {
  // 150 MHz -> 6.667ns per cycle.
  TEST_ASSERT_EQUAL_UINT16(0, pairDeadTimeCycles(0));
  TEST_ASSERT_EQUAL_UINT16(15, pairDeadTimeCycles(100));
  TEST_ASSERT_EQUAL_UINT16(22, pairDeadTimeCycles(150));
  TEST_ASSERT_EQUAL_UINT16(150, pairDeadTimeCycles(1000));
}

void test_dead_time_cycles_saturates_instead_of_wrapping() {
  // DTCNT is 11 bits. The failure this pins is a LARGER configured dead time
  // silently programming a SMALLER one: 65535ns is 9830 cycles, and keeping only
  // bits 10:0 of that gives 614 cycles - about 4.1us instead of 65.5us.
  TEST_ASSERT_EQUAL_UINT16(MaxDeadTimeCycles, pairDeadTimeCycles(65535));
  TEST_ASSERT_EQUAL_UINT16(MaxDeadTimeCycles, pairDeadTimeCycles(20000));
  TEST_ASSERT_EQUAL_UINT16(MaxDeadTimeCycles, pairDeadTimeCycles(0xFFFFFFFFu));

  // The ns round-trip loses at most one cycle to double truncation: 2047 cycles is
  // 13646.67ns, pairMaxDeadTimeNs() reports 13646, and that converts back to 2046.
  // Erring one cycle (6.67ns) short of the register maximum is the harmless
  // direction at a clamp boundary; what must never happen is exceeding it.
  TEST_ASSERT_TRUE(pairDeadTimeCycles(pairMaxDeadTimeNs()) <= MaxDeadTimeCycles);
  TEST_ASSERT_UINT16_WITHIN(1, MaxDeadTimeCycles, pairDeadTimeCycles(pairMaxDeadTimeNs()));
}

void test_max_dead_time_ns_matches_the_register_width() {
  // 2047 cycles at 150 MHz is ~13.6us. Quoted in the docs, so pin it.
  TEST_ASSERT_UINT32_WITHIN(2, 13646, pairMaxDeadTimeNs());
}

void test_dead_time_conversion_handles_other_bus_clocks() {
  TEST_ASSERT_EQUAL_UINT16(100, pairDeadTimeCycles(1000, 100000000u));
  TEST_ASSERT_EQUAL_UINT16(MaxDeadTimeCycles, pairDeadTimeCycles(1000000, 100000000u));
}

// --- mode predicates --------------------------------------------------------

void test_complementary_modes_are_exactly_the_two() {
  TEST_ASSERT_FALSE(pairIsComplementary(PairIndependent));
  TEST_ASSERT_TRUE(pairIsComplementary(PairHalfBridge));
  TEST_ASSERT_TRUE(pairIsComplementary(PairDifferential));
}

void test_only_a_half_bridge_uses_dead_time() {
  TEST_ASSERT_FALSE(pairUsesDeadTime(PairIndependent));
  TEST_ASSERT_TRUE(pairUsesDeadTime(PairHalfBridge));
  TEST_ASSERT_FALSE(pairUsesDeadTime(PairDifferential));
}

// --- the programmed dead time ----------------------------------------------

void test_half_bridge_floors_but_does_not_cap_below_the_floor() {
  TEST_ASSERT_EQUAL_UINT32(MinHalfBridgeDeadTimeNs, pairDeadTimeNs(PairHalfBridge, 0));
  TEST_ASSERT_EQUAL_UINT32(MinHalfBridgeDeadTimeNs, pairDeadTimeNs(PairHalfBridge, 1));
  TEST_ASSERT_EQUAL_UINT32(MinHalfBridgeDeadTimeNs,
                           pairDeadTimeNs(PairHalfBridge, MinHalfBridgeDeadTimeNs - 1));
  // At and above the floor the operator's value is respected.
  TEST_ASSERT_EQUAL_UINT32(MinHalfBridgeDeadTimeNs,
                           pairDeadTimeNs(PairHalfBridge, MinHalfBridgeDeadTimeNs));
  TEST_ASSERT_EQUAL_UINT32(500, pairDeadTimeNs(PairHalfBridge, 500));
}

void test_half_bridge_clamps_to_a_representable_value() {
  // The whole point: never hand the register something it will wrap.
  TEST_ASSERT_EQUAL_UINT32(pairMaxDeadTimeNs(), pairDeadTimeNs(PairHalfBridge, 65535));
  TEST_ASSERT_TRUE(pairDeadTimeCycles(pairDeadTimeNs(PairHalfBridge, 65535)) <= MaxDeadTimeCycles);
}

void test_differential_never_inserts_a_gap() {
  // A differential command pair drives one logical channel. A dead-time gap there
  // is a window where neither state is asserted, not a safety margin - so the
  // floor must NOT apply, at any configured value.
  TEST_ASSERT_EQUAL_UINT32(0, pairDeadTimeNs(PairDifferential, 0));
  TEST_ASSERT_EQUAL_UINT32(0, pairDeadTimeNs(PairDifferential, 500));
  TEST_ASSERT_EQUAL_UINT32(0, pairDeadTimeNs(PairDifferential, 65535));
}

void test_independent_programs_no_dead_time() {
  // The hardware ignores DTCNT while INDEP=1; programming a value anyway only
  // makes a bench register dump misleading.
  TEST_ASSERT_EQUAL_UINT32(0, pairDeadTimeNs(PairIndependent, 0));
  TEST_ASSERT_EQUAL_UINT32(0, pairDeadTimeNs(PairIndependent, 500));
}

void test_programmed_dead_time_is_always_representable() {
  // Property over the whole input space, not just the values I thought of.
  for (uint8_t mode = 0; mode < PairModeCount; mode++) {
    for (uint32_t ns = 0; ns < 70000; ns += 137) {
      TEST_ASSERT_TRUE(pairDeadTimeCycles(pairDeadTimeNs(mode, ns)) <= MaxDeadTimeCycles);
    }
  }
}

// --- hardware facts ---------------------------------------------------------

void test_submodules_without_a_b_pin_are_known() {
  TEST_ASSERT_FALSE(pairHasChannelB(PairSm21));
  TEST_ASSERT_FALSE(pairHasChannelB(PairSm40));
  TEST_ASSERT_FALSE(pairHasChannelB(PairSm41));
  TEST_ASSERT_TRUE(pairHasChannelB(PairSm13));
  TEST_ASSERT_TRUE(pairHasChannelB(PairSm20));
  TEST_ASSERT_TRUE(pairHasChannelB(PairSm22));
  TEST_ASSERT_TRUE(pairHasChannelB(PairSm23));
  TEST_ASSERT_TRUE(pairHasChannelB(PairSm31));
  TEST_ASSERT_TRUE(pairHasChannelB(PairSm42));
}

void test_sm42_has_a_b_pin_but_must_not_be_paired() {
  // Different reason from the B-less submodules, so it is asserted separately:
  // asymmetric induction mode drives A and B from independent start/stop values.
  TEST_ASSERT_TRUE(pairHasChannelB(PairSm42));
  TEST_ASSERT_FALSE(pairAllowsComplementary(PairSm42));
}

// --- validation -------------------------------------------------------------

void test_complementary_is_refused_where_it_is_physically_impossible() {
  // Putting a B-less submodule into a complementary mode would run DTCNT1 at its
  // reset value of 0x07FF - about 13.6us - as the falling-edge dead time, because
  // the Teensy core never writes that register.
  const uint8_t bless[] = {PairSm21, PairSm40, PairSm41};
  for (uint8_t sm : bless) {
    TEST_ASSERT_EQUAL_UINT8(PairIndependent, pairModeSanitised(PairHalfBridge, sm));
    TEST_ASSERT_EQUAL_UINT8(PairIndependent, pairModeSanitised(PairDifferential, sm));
    TEST_ASSERT_FALSE(pairModeValid(PairHalfBridge, sm));
    TEST_ASSERT_FALSE(pairModeValid(PairDifferential, sm));
  }
}

void test_out_of_range_modes_degrade_to_independent() {
  // A corrupt or hand-edited settings file must land on the always-valid mode,
  // not on a plausible-looking wrong one.
  TEST_ASSERT_EQUAL_UINT8(PairIndependent, pairModeSanitised(PairModeCount, PairSm20));
  TEST_ASSERT_EQUAL_UINT8(PairIndependent, pairModeSanitised(99, PairSm20));
  TEST_ASSERT_EQUAL_UINT8(PairIndependent, pairModeSanitised(255, PairSm20));
}

void test_valid_combinations_pass_through_unchanged() {
  const uint8_t legs[] = {PairSm13, PairSm20, PairSm22, PairSm23, PairSm31};
  for (uint8_t sm : legs) {
    TEST_ASSERT_EQUAL_UINT8(PairHalfBridge, pairModeSanitised(PairHalfBridge, sm));
    TEST_ASSERT_EQUAL_UINT8(PairDifferential, pairModeSanitised(PairDifferential, sm));
    TEST_ASSERT_TRUE(pairModeValid(PairHalfBridge, sm));
    TEST_ASSERT_TRUE(pairModeValid(PairDifferential, sm));
  }
}

void test_independent_is_valid_everywhere() {
  // The default must never be corrected away, on any submodule.
  for (uint8_t sm = 0; sm < PairSubmoduleCount; sm++) {
    TEST_ASSERT_EQUAL_UINT8(PairIndependent, pairModeSanitised(PairIndependent, sm));
    TEST_ASSERT_TRUE(pairModeValid(PairIndependent, sm));
  }
}

void test_sanitising_is_idempotent_over_the_whole_matrix() {
  // Applying the correction twice must not move again - otherwise a config that
  // round-trips through save/load could drift.
  for (uint8_t sm = 0; sm < PairSubmoduleCount; sm++) {
    for (uint16_t mode = 0; mode < 260; mode++) {
      const uint8_t once = pairModeSanitised(static_cast<uint8_t>(mode), sm);
      TEST_ASSERT_EQUAL_UINT8(once, pairModeSanitised(once, sm));
      TEST_ASSERT_TRUE(once < PairModeCount);
    }
  }
}

void test_no_sanitised_mode_ever_pairs_a_b_less_submodule() {
  // The safety property, over the entire input space rather than the cases above.
  for (uint8_t sm = 0; sm < PairSubmoduleCount; sm++) {
    for (uint16_t mode = 0; mode < 260; mode++) {
      const uint8_t out = pairModeSanitised(static_cast<uint8_t>(mode), sm);
      if (pairIsComplementary(out)) {
        TEST_ASSERT_TRUE(pairHasChannelB(sm));
        TEST_ASSERT_TRUE(pairAllowsComplementary(sm));
      }
    }
  }
}

// --- scheme gate ------------------------------------------------------------
// A complementary pair cannot invert a cell's output polarity: it would turn every
// dead-time gap into an overlap, and mask/fault force the pins low BEFORE polarity,
// so an inverted pair goes HIGH on fault. Until the two real mechanisms land
// (DTSRCSEL + FORCE_OUT for an inverted output, VAL2/VAL3 edge bias for a displaced
// carrier), a pair refuses any modulation that needs inversion.

void test_scheme_gate_refuses_complementary_when_inversion_needed() {
  for (uint8_t sm : {PairSm20, PairSm22, PairSm23}) {
    TEST_ASSERT_EQUAL_UINT8(PairIndependent,
                            pairModeSanitisedForScheme(PairHalfBridge, sm, true));
    TEST_ASSERT_EQUAL_UINT8(PairIndependent,
                            pairModeSanitisedForScheme(PairDifferential, sm, true));
  }
}

void test_scheme_gate_admits_complementary_when_no_inversion_needed() {
  for (uint8_t sm : {PairSm20, PairSm22, PairSm23}) {
    TEST_ASSERT_EQUAL_UINT8(PairHalfBridge,
                            pairModeSanitisedForScheme(PairHalfBridge, sm, false));
    TEST_ASSERT_EQUAL_UINT8(PairDifferential,
                            pairModeSanitisedForScheme(PairDifferential, sm, false));
  }
}

void test_scheme_gate_still_applies_the_hardware_facts() {
  // The gate composes with, rather than replaces, the B-pin and Sm42 rules.
  for (uint8_t sm : {PairSm21, PairSm40, PairSm41, PairSm42}) {
    TEST_ASSERT_EQUAL_UINT8(PairIndependent,
                            pairModeSanitisedForScheme(PairHalfBridge, sm, false));
  }
  // Independent is always fine, inversion or not.
  for (uint8_t sm = 0; sm < PairSubmoduleCount; sm++) {
    TEST_ASSERT_EQUAL_UINT8(PairIndependent, pairModeSanitisedForScheme(PairIndependent, sm, true));
    TEST_ASSERT_EQUAL_UINT8(PairIndependent, pairModeSanitisedForScheme(PairIndependent, sm, false));
  }
}

void test_scheme_gate_never_yields_a_complementary_pair_needing_inversion() {
  // The safety property, over the whole input space.
  for (uint8_t sm = 0; sm < PairSubmoduleCount; sm++) {
    for (uint16_t mode = 0; mode < 260; mode++) {
      const uint8_t out = pairModeSanitisedForScheme(static_cast<uint8_t>(mode), sm, true);
      TEST_ASSERT_FALSE(pairIsComplementary(out));
    }
  }
}

void test_scheme_gate_is_idempotent() {
  for (uint8_t sm = 0; sm < PairSubmoduleCount; sm++) {
    for (uint16_t mode = 0; mode < 260; mode++) {
      for (int inv = 0; inv < 2; inv++) {
        const uint8_t once = pairModeSanitisedForScheme(static_cast<uint8_t>(mode), sm, inv != 0);
        TEST_ASSERT_EQUAL_UINT8(once, pairModeSanitisedForScheme(once, sm, inv != 0));
      }
    }
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_scheme_gate_refuses_complementary_when_inversion_needed);
  RUN_TEST(test_scheme_gate_admits_complementary_when_no_inversion_needed);
  RUN_TEST(test_scheme_gate_still_applies_the_hardware_facts);
  RUN_TEST(test_scheme_gate_never_yields_a_complementary_pair_needing_inversion);
  RUN_TEST(test_scheme_gate_is_idempotent);
  RUN_TEST(test_dead_time_cycles_converts_at_bus_clock);
  RUN_TEST(test_dead_time_cycles_saturates_instead_of_wrapping);
  RUN_TEST(test_max_dead_time_ns_matches_the_register_width);
  RUN_TEST(test_dead_time_conversion_handles_other_bus_clocks);
  RUN_TEST(test_complementary_modes_are_exactly_the_two);
  RUN_TEST(test_only_a_half_bridge_uses_dead_time);
  RUN_TEST(test_half_bridge_floors_but_does_not_cap_below_the_floor);
  RUN_TEST(test_half_bridge_clamps_to_a_representable_value);
  RUN_TEST(test_differential_never_inserts_a_gap);
  RUN_TEST(test_independent_programs_no_dead_time);
  RUN_TEST(test_programmed_dead_time_is_always_representable);
  RUN_TEST(test_submodules_without_a_b_pin_are_known);
  RUN_TEST(test_sm42_has_a_b_pin_but_must_not_be_paired);
  RUN_TEST(test_complementary_is_refused_where_it_is_physically_impossible);
  RUN_TEST(test_out_of_range_modes_degrade_to_independent);
  RUN_TEST(test_valid_combinations_pass_through_unchanged);
  RUN_TEST(test_independent_is_valid_everywhere);
  RUN_TEST(test_sanitising_is_idempotent_over_the_whole_matrix);
  RUN_TEST(test_no_sanitised_mode_ever_pairs_a_b_less_submodule);
  return UNITY_END();
}
