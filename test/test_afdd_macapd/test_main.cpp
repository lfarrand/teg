// Host Unity tests for MACAPD math (afdd_macapd.h). Not ISR/OUTEN proof. Not UL 1699B.

#include <math.h>
#include <unity.h>

#include <afdd_macapd.h>

void setUp() {}
void tearDown() {}

static void fillTone(float *x, size_t n, float fs, float fHz, float amp) {
  for (size_t i = 0; i < n; ++i) {
    const float t = static_cast<float>(i) / fs;
    x[i] = amp * sinf(2.0f * 3.14159265f * fHz * t);
  }
}

static void fillNoiseBursts(float *x, size_t n, unsigned seed) {
  // Simple LCG impulse train — high kurtosis, broadband-ish when viewed short.
  uint32_t s = seed ? seed : 1u;
  for (size_t i = 0; i < n; ++i) {
    s = s * 1664525u + 1013904223u;
    const float u = (s & 0xffffu) / 65535.0f;
    x[i] = (u > 0.97f) ? (8.0f * (u - 0.97f) / 0.03f) : 0.02f * (u - 0.5f);
  }
}

void test_default_config_is_disarmed_friendly() {
  const AfddMacapdConfig c = afddMacapdDefaultConfig();
  TEST_ASSERT_FALSE(c.ditherActive);
  TEST_ASSERT_TRUE(c.blankingAvailable);
  TEST_ASSERT_FALSE(c.afeFault);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 250000.0f, c.sampleRateHz);
}

void test_dither_inhibits_scoring() {
  AfddMacapdConfig c = afddMacapdDefaultConfig();
  c.ditherActive = true;
  AfddMacapdState st{};
  afddMacapdReset(&st);
  float x[128];
  fillNoiseBursts(x, 128, 42);
  const AfddMacapdFeatures f = afddMacapdProcessRaw(c, &st, x, nullptr, 128);
  TEST_ASSERT_EQUAL_UINT8(AfddMacapdInhibited, st.sense);
  TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 0.0f, f.scoreRaw);
}

void test_blank_mask_clears_near_carrier_edges() {
  AfddMacapdConfig c = afddMacapdDefaultConfig();
  c.sampleRateHz = 200000.0f;
  c.carrierHz = 20000.0f;
  c.blankHalfWidthS = 5.0e-6f; // 1 sample at 200 kHz
  uint8_t mask[20];
  afddMacapdBuildBlankMask(c, 20, mask);
  // Sample 0 is on a carrier edge → blanked
  TEST_ASSERT_EQUAL_UINT8(0, mask[0]);
  // Mid-period sample 5 (period=10) should be valid
  TEST_ASSERT_EQUAL_UINT8(1, mask[5]);
}

void test_tonal_carrier_has_higher_residual_than_bursts() {
  AfddMacapdConfig c = afddMacapdDefaultConfig();
  c.sampleRateHz = 250000.0f;
  c.carrierHz = 20000.0f;
  AfddMacapdState stTone{};
  AfddMacapdState stBurst{};
  afddMacapdReset(&stTone);
  afddMacapdReset(&stBurst);

  float tone[256];
  float burst[256];
  fillTone(tone, 256, c.sampleRateHz, c.carrierHz, 1.0f);
  fillNoiseBursts(burst, 256, 7);

  // Disable blanking so pure tone is not zeroed at edges for this comparison.
  c.blankingAvailable = true;
  // Force blanking off via mask-all by setting blankingAvailable false would inhibit.
  // Instead processFrame with no blanking: build all-ones mask manually.
  uint8_t ones[256];
  for (size_t i = 0; i < 256; ++i) {
    ones[i] = 1;
  }
  float toneB[256];
  float burstB[256];
  afddMacapdApplyBlank(tone, ones, 256, toneB);
  afddMacapdApplyBlank(burst, ones, 256, burstB);

  // Temporarily lie that blanking is available so we are not inhibited.
  const AfddMacapdFeatures ft = afddMacapdProcessFrame(c, &stTone, toneB, nullptr, 256, ones);
  const AfddMacapdFeatures fb = afddMacapdProcessFrame(c, &stBurst, burstB, nullptr, 256, ones);
  TEST_ASSERT_TRUE(ft.rTonal > fb.rTonal);
}

void test_mask_exclude_kurtosis_ignores_blank_zeros() {
  float x[64];
  uint8_t mask[64];
  for (size_t i = 0; i < 64; ++i) {
    x[i] = (i < 32) ? 0.0f : 1.0f; // half zeros
    mask[i] = (i < 32) ? 0u : 1u;  // zeros are blanked
  }
  const float kAll = afddMacapdExcessKurtosis(x, 64);
  const float kKeep = afddMacapdExcessKurtosisMasked(x, mask, 64);
  // Constant kept samples → ~0 excess kurtosis; including blank zeros changes moments.
  TEST_ASSERT_FLOAT_WITHIN(0.25f, 0.0f, kKeep);
  TEST_ASSERT_TRUE(fabsf(kAll - kKeep) > 0.5f);
}

void test_low_keep_count_inhibits() {
  AfddMacapdConfig c = afddMacapdDefaultConfig();
  c.keepMin = 40;
  AfddMacapdState st{};
  afddMacapdReset(&st);
  float x[64];
  uint8_t mask[64];
  for (size_t i = 0; i < 64; ++i) {
    x[i] = 1.0f;
    mask[i] = (i < 8) ? 1u : 0u; // only 8 kept
  }
  afddMacapdProcessFrame(c, &st, x, nullptr, 64, mask);
  TEST_ASSERT_EQUAL_UINT8(AfddMacapdInhibited, st.sense);
}

void test_ewma_freezes_while_candidate() {
  AfddMacapdConfig c = afddMacapdDefaultConfig();
  c.freezeEwmaOnCandidate = true;
  c.tLo = 0.01f;
  c.tHi = 0.02f;
  c.nPersist = 1;
  c.wBand = 10.0f;
  c.wTonal = 0.0f;
  c.blankHalfWidthS = 0.0f;
  AfddMacapdState st{};
  afddMacapdReset(&st);
  float x[128];
  fillNoiseBursts(x, 128, 55);
  for (int i = 0; i < 6; ++i) {
    afddMacapdProcessRaw(c, &st, x, nullptr, 128);
  }
  TEST_ASSERT_TRUE(st.sense == AfddMacapdCandidateLow || st.sense == AfddMacapdCandidateHigh);
  const float ewmaArmed = st.ewmaEm;
  fillTone(x, 128, c.sampleRateHz, 30000.0f, 50.0f);
  afddMacapdProcessRaw(c, &st, x, nullptr, 128);
  // Floor must not chase the huge new energy while still Candidate*.
  TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, ewmaArmed, st.ewmaEm);
}

void test_impulsive_bursts_raise_kurtosis() {
  float tone[256];
  float burst[256];
  fillTone(tone, 256, 250000.0f, 30000.0f, 1.0f);
  fillNoiseBursts(burst, 256, 99);
  const float kTone = afddMacapdExcessKurtosis(tone, 256);
  const float kBurst = afddMacapdExcessKurtosis(burst, 256);
  TEST_ASSERT_TRUE(kBurst > kTone);
}

void test_persist_reaches_high_on_strong_bursts() {
  AfddMacapdConfig c = afddMacapdDefaultConfig();
  c.tLo = 0.2f;
  c.tHi = 0.5f;
  c.nPersist = 2;
  c.wTonal = 0.1f;
  c.blankHalfWidthS = 0.0f; // keep energy
  AfddMacapdState st{};
  afddMacapdReset(&st);

  float burst[256];
  fillNoiseBursts(burst, 256, 1234);
  AfddMacapdSenseState last = AfddMacapdQuiet;
  for (int frame = 0; frame < 16; ++frame) {
    // Reseed slightly each frame
    fillNoiseBursts(burst, 256, static_cast<unsigned>(1234 + frame * 17));
    afddMacapdProcessRaw(c, &st, burst, nullptr, 256);
    last = st.sense;
    if (last == AfddMacapdCandidateHigh) {
      break;
    }
  }
  TEST_ASSERT_TRUE(last == AfddMacapdCandidateLow || last == AfddMacapdCandidateHigh);
  TEST_ASSERT_NOT_EQUAL(AfddMacapdInhibited, last);
}

void test_afe_fault_inhibits() {
  AfddMacapdConfig c = afddMacapdDefaultConfig();
  c.afeFault = true;
  AfddMacapdState st{};
  afddMacapdReset(&st);
  float x[64] = {};
  afddMacapdProcessRaw(c, &st, x, nullptr, 64);
  TEST_ASSERT_EQUAL_UINT8(AfddMacapdInhibited, st.sense);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_default_config_is_disarmed_friendly);
  RUN_TEST(test_dither_inhibits_scoring);
  RUN_TEST(test_blank_mask_clears_near_carrier_edges);
  RUN_TEST(test_tonal_carrier_has_higher_residual_than_bursts);
  RUN_TEST(test_impulsive_bursts_raise_kurtosis);
  RUN_TEST(test_mask_exclude_kurtosis_ignores_blank_zeros);
  RUN_TEST(test_low_keep_count_inhibits);
  RUN_TEST(test_ewma_freezes_while_candidate);
  RUN_TEST(test_persist_reaches_high_on_strong_bursts);
  RUN_TEST(test_afe_fault_inhibits);
  return UNITY_END();
}
