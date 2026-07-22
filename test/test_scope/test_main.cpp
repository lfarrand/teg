// Tests for the triggered-scope state machine (scope_math.h)

#include <unity.h>
#include <scope_math.h>

void setUp() {}
void tearDown() {}

// Feed samples until scopeStep returns true (freeze) or the array runs out;
// returns the index of the freezing sample or -1
static int feedUntilFreeze(ScopeMachine &m, const uint16_t *samples, int n) {
  for (int i = 0; i < n; i++) {
    if (scopeStep(m, samples[i])) {
      return i;
    }
  }
  return -1;
}

void test_rising_trigger_fires_on_crossing() {
  ScopeMachine m;
  scopeArm(m, 2000, false, 0);
  const uint16_t s[] = {1000, 1500, 1999, 2000, 2500};
  // postSamples = 0: freeze on the trigger sample itself (index 3, first >= level)
  TEST_ASSERT_EQUAL_INT(3, feedUntilFreeze(m, s, 5));
  TEST_ASSERT_EQUAL_UINT8(ScopeComplete, m.state);
}

void test_rising_requires_priming() {
  // Signal starts above the level: no instant trigger; must dip below first
  ScopeMachine m;
  scopeArm(m, 2000, false, 0);
  const uint16_t s[] = {3000, 2500, 2100, 1900, 2100};
  TEST_ASSERT_EQUAL_INT(4, feedUntilFreeze(m, s, 5)); // fires only after the dip
}

void test_falling_trigger() {
  ScopeMachine m;
  scopeArm(m, 2000, true, 0);
  const uint16_t s[] = {1000, 2500, 3000, 1999, 1500};
  // Primed by 2500 (>= level), fires at 1999 (first < level)
  TEST_ASSERT_EQUAL_INT(3, feedUntilFreeze(m, s, 5));
}

void test_falling_requires_priming() {
  ScopeMachine m;
  scopeArm(m, 2000, true, 0);
  const uint16_t s[] = {1000, 1500, 1999};
  TEST_ASSERT_EQUAL_INT(-1, feedUntilFreeze(m, s, 3)); // never above level
  TEST_ASSERT_EQUAL_UINT8(ScopeArmed, m.state);
}

void test_post_trigger_countdown() {
  ScopeMachine m;
  scopeArm(m, 2000, false, 3);
  TEST_ASSERT_FALSE(scopeStep(m, 1000)); // primes
  TEST_ASSERT_FALSE(scopeStep(m, 2500)); // triggers, post = 3
  TEST_ASSERT_EQUAL_UINT8(ScopeTriggered, m.state);
  TEST_ASSERT_FALSE(scopeStep(m, 100));
  TEST_ASSERT_FALSE(scopeStep(m, 100));
  TEST_ASSERT_TRUE(scopeStep(m, 100)); // 3rd post sample freezes
  TEST_ASSERT_EQUAL_UINT8(ScopeComplete, m.state);
  // Further samples are inert
  TEST_ASSERT_FALSE(scopeStep(m, 3000));
  TEST_ASSERT_EQUAL_UINT8(ScopeComplete, m.state);
}

void test_level_boundary_is_inclusive() {
  // Rising: a sample exactly at the level counts as crossed
  ScopeMachine m;
  scopeArm(m, 2000, false, 0);
  TEST_ASSERT_FALSE(scopeStep(m, 1999));
  TEST_ASSERT_TRUE(scopeStep(m, 2000));
}

void test_rearm_resets_priming() {
  ScopeMachine m;
  scopeArm(m, 2000, false, 0);
  TEST_ASSERT_FALSE(scopeStep(m, 1000)); // primed
  scopeArm(m, 2000, false, 0);           // re-arm clears priming
  TEST_ASSERT_FALSE(scopeStep(m, 2500)); // above level but unprimed: no fire
  TEST_ASSERT_EQUAL_UINT8(ScopeArmed, m.state);
  scopeDisarm(m);
  TEST_ASSERT_EQUAL_UINT8(ScopeIdle, m.state);
  TEST_ASSERT_FALSE(scopeStep(m, 100)); // idle machine ignores samples
}

void test_trig_offset() {
  // Window of 100 samples, 10 post-trigger: trigger sits at index 89
  TEST_ASSERT_EQUAL_UINT32(89, scopeTrigOffset(true, 10, 100));
  // postSamples = 0: trigger is the last sample
  TEST_ASSERT_EQUAL_UINT32(99, scopeTrigOffset(true, 0, 100));
  // Trigger fell outside the window
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, scopeTrigOffset(true, 100, 100));
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, scopeTrigOffset(true, 500, 100));
  // No trigger at all / empty window
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, scopeTrigOffset(false, 10, 100));
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, scopeTrigOffset(true, 0, 0));
}

void test_level_counts_conversion() {
  TEST_ASSERT_EQUAL_UINT16(0, scopeLevelCounts(0));
  TEST_ASSERT_EQUAL_UINT16(4095, scopeLevelCounts(3300));
  TEST_ASSERT_EQUAL_UINT16(4095, scopeLevelCounts(5000)); // clamped
  TEST_ASSERT_EQUAL_UINT16(2048, scopeLevelCounts(1650)); // mid-rail
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_rising_trigger_fires_on_crossing);
  RUN_TEST(test_rising_requires_priming);
  RUN_TEST(test_falling_trigger);
  RUN_TEST(test_falling_requires_priming);
  RUN_TEST(test_post_trigger_countdown);
  RUN_TEST(test_level_boundary_is_inclusive);
  RUN_TEST(test_rearm_resets_priming);
  RUN_TEST(test_trig_offset);
  RUN_TEST(test_level_counts_conversion);
  return UNITY_END();
}
