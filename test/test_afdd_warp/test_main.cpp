// Host Unity tests for WARP math (afdd_warp.h). Not ISR/OUTEN proof. Not UL 1699B.

#include <math.h>
#include <unity.h>

#include <afdd_warp.h>

void setUp() {}
void tearDown() {}

static void fillTone(float *x, size_t n, float fs, float fHz, float amp) {
  for (size_t i = 0; i < n; ++i) {
    const float t = static_cast<float>(i) / fs;
    x[i] = amp * sinf(2.0f * 3.14159265f * fHz * t);
  }
}

static void fillSparseImpulses(float *x, size_t n, unsigned seed) {
  uint32_t s = seed ? seed : 1u;
  for (size_t i = 0; i < n; ++i) {
    x[i] = 0.0f;
  }
  for (int k = 0; k < 8; ++k) {
    s = s * 1664525u + 1013904223u;
    const size_t idx = (s % (n - 4u)) + 2u;
    x[idx] = 6.0f;
    x[idx + 1] = -4.0f;
  }
}

void test_warp_default_disarmed() {
  const AfddWarpConfig c = afddWarpDefaultConfig();
  TEST_ASSERT_FALSE(c.ditherActive);
  TEST_ASSERT_TRUE(c.blankingAvailable);
  TEST_ASSERT_FALSE(c.afeFault);
}

void test_warp_dither_inhibits() {
  AfddWarpConfig c = afddWarpDefaultConfig();
  c.ditherActive = true;
  AfddWarpState st{};
  afddWarpReset(&st);
  float x[128] = {};
  uint8_t ones[128];
  for (size_t i = 0; i < 128; ++i) {
    ones[i] = 1;
  }
  const AfddWarpFeatures f = afddWarpProcessFrame(c, &st, x, 128, ones, 0.0f, 0.0f);
  TEST_ASSERT_EQUAL_UINT8(AfddWarpInhibited, st.sense);
  TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 0.0f, f.sWarp);
}

void test_warp_tone_not_precursor_watch() {
  AfddWarpConfig c = afddWarpDefaultConfig();
  c.tPre = 5.0f; // high — pure tone should stay Quiet
  c.thetaEnergy = 1.0e6f;
  c.gammaEnergy = 1.0f;
  AfddWarpState st{};
  afddWarpReset(&st);
  float x[256];
  uint8_t ones[256];
  fillTone(x, 256, c.sampleRateHz, 20000.0f, 1.0f);
  for (size_t i = 0; i < 256; ++i) {
    ones[i] = 1;
  }
  for (int frame = 0; frame < 12; ++frame) {
    afddWarpProcessFrame(c, &st, x, 256, ones, 0.0f, 0.8f); // high tonal penalty path
  }
  TEST_ASSERT_NOT_EQUAL(AfddWarpPrecursorWatch, st.sense);
  TEST_ASSERT_NOT_EQUAL(AfddWarpInhibited, st.sense);
}

void test_warp_sparse_impulses_can_raise_irregularity() {
  AfddWarpConfig c = afddWarpDefaultConfig();
  c.tPre = 0.2f;
  c.nPre = 2;
  c.thetaEnergy = 100.0f; // keep energy "low" vs gamma*theta
  c.gammaEnergy = 1.0f;
  c.wT = 0.0f;
  AfddWarpState st{};
  afddWarpReset(&st);
  float x[256];
  uint8_t ones[256];
  for (size_t i = 0; i < 256; ++i) {
    ones[i] = 1;
  }
  AfddWarpSenseState last = AfddWarpQuiet;
  for (int frame = 0; frame < 20; ++frame) {
    fillSparseImpulses(x, 256, static_cast<unsigned>(10 + frame * 3));
    const AfddWarpFeatures f = afddWarpProcessFrame(c, &st, x, 256, ones, 0.0f, 0.0f);
    last = st.sense;
    (void)f;
    if (last == AfddWarpPrecursorWatch || last == AfddWarpPrecursorConfirmed ||
        last == AfddWarpCandidateLow || last == AfddWarpCandidateHigh) {
      break;
    }
  }
  TEST_ASSERT_TRUE(last == AfddWarpPrecursorWatch || last == AfddWarpPrecursorConfirmed ||
                   last == AfddWarpCandidateLow || last == AfddWarpCandidateHigh);
  // Horizon packet CV is zero until four history entries exist. Feed enough
  // frames that I_irr can rise; silence must stay strictly below impulses.
  AfddWarpState st2{};
  afddWarpReset(&st2);
  AfddWarpFeatures f1{};
  for (int frame = 0; frame < 8; ++frame) {
    fillSparseImpulses(x, 256, static_cast<unsigned>(99 + frame));
    f1 = afddWarpProcessFrame(c, &st2, x, 256, ones, 0.0f, 0.0f);
  }
  float silence[256] = {};
  AfddWarpState st3{};
  afddWarpReset(&st3);
  AfddWarpFeatures f0{};
  for (int frame = 0; frame < 8; ++frame) {
    f0 = afddWarpProcessFrame(c, &st3, silence, 256, ones, 0.0f, 0.0f);
  }
  TEST_ASSERT_TRUE(isfinite(f1.iIrr));
  TEST_ASSERT_TRUE(f1.iIrr > f0.iIrr);
}

void test_warp_haar_wpt_length() {
  float x[64];
  for (size_t i = 0; i < 64; ++i) {
    x[i] = static_cast<float>(i);
  }
  float packets[AFDD_WARP_PACKETS][AFDD_WARP_MAX_N / 8];
  const size_t plen = afddWarpHaarWpt3(x, 64, packets);
  TEST_ASSERT_EQUAL(8u, plen);
}

void test_warp_packet_energy_excludes_blank_haar_windows() {
  // Constant frame + one blanked sample per Haar support. Zero-stuffing those
  // blanks before WPT creates edges that leak into arc packets if the mask is
  // ignored; mask-exclude must drop those contaminated coeffs.
  const size_t n = 256;
  float x[n];
  uint8_t mask[n];
  for (size_t i = 0; i < n; ++i) {
    x[i] = 1.0f;
    mask[i] = 1u;
  }
  for (size_t i = 0; i < n; i += 8) {
    x[i] = 0.0f;
    mask[i] = 0u;
  }
  float packets[AFDD_WARP_PACKETS][AFDD_WARP_MAX_N / 8];
  const size_t plen = afddWarpHaarWpt3(x, n, packets);
  TEST_ASSERT_EQUAL(32u, plen);

  float eArcIgnored = 0.0f;
  float eArcMasked = 0.0f;
  for (int p = 0; p < AFDD_WARP_PACKETS; ++p) {
    if (!afddWarpIsArcPacket(p)) {
      continue;
    }
    eArcIgnored += afddWarpPacketEnergy(packets[p], plen, nullptr, n, static_cast<size_t>(p));
    eArcMasked += afddWarpPacketEnergy(packets[p], plen, mask, n, static_cast<size_t>(p));
  }
  TEST_ASSERT_TRUE(eArcIgnored > 1.0e-4f);
  TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 0.0f, eArcMasked);
}

void test_warp_process_frame_mask_stops_blank_precursor() {
  AfddWarpConfig c = afddWarpDefaultConfig();
  c.keepMin = 8;
  const size_t n = 256;
  float x[n];
  uint8_t keepAll[n];
  uint8_t blankMask[n];
  for (size_t i = 0; i < n; ++i) {
    x[i] = 1.0f;
    keepAll[i] = 1u;
    blankMask[i] = 1u;
  }
  for (size_t i = 0; i < n; i += 8) {
    x[i] = 0.0f;
    blankMask[i] = 0u;
  }

  AfddWarpState stIgnore{};
  afddWarpReset(&stIgnore);
  const AfddWarpFeatures ignored = afddWarpProcessFrame(c, &stIgnore, x, n, keepAll, 0.0f, 0.0f);
  AfddWarpState stMask{};
  afddWarpReset(&stMask);
  const AfddWarpFeatures masked = afddWarpProcessFrame(c, &stMask, x, n, blankMask, 0.0f, 0.0f);

  TEST_ASSERT_NOT_EQUAL(AfddWarpInhibited, stIgnore.sense);
  TEST_ASSERT_EQUAL_UINT8(AfddWarpInhibited, stMask.sense);
  TEST_ASSERT_TRUE(ignored.eArc > 1.0e-4f);
  TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 0.0f, masked.eArc);
}

void test_warp_default_blanking_inhibits_when_haar_blind() {
  // Default 250 kSPS / 20 kHz / 50% / 2 µs blanks every 8-sample Haar support
  // while sample keepCount still clears the frame floor.
  const AfddMacapdConfig mac = afddMacapdDefaultConfig();
  const size_t n = 256;
  float x[n];
  uint8_t mask[n];
  for (size_t i = 0; i < n; ++i) {
    x[i] = 1.0f;
  }
  afddMacapdBuildBlankMask(mac, n, mask);
  const uint16_t keep = afddMacapdKeepCount(mask, n);
  TEST_ASSERT_TRUE(keep > n / 4);
  TEST_ASSERT_EQUAL(0u, afddWarpHaarKeptCoeffCount(mask, n, n / 8));

  AfddWarpConfig c = afddWarpDefaultConfig();
  AfddWarpState st{};
  afddWarpReset(&st);
  const AfddWarpFeatures f = afddWarpProcessFrame(c, &st, x, n, mask, 0.0f, 0.0f);
  TEST_ASSERT_EQUAL_UINT8(AfddWarpInhibited, st.sense);
  TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 0.0f, f.sWarp);
}

void test_warp_half_horizon_delta_survives_ring_wrap() {
  float hist[AFDD_WARP_HORIZON];
  for (uint16_t i = 0; i < AFDD_WARP_HORIZON; ++i) {
    hist[i] = static_cast<float>(i);
  }
  const float unrotated = afddWarpHalfHorizonDelta(hist, AFDD_WARP_HORIZON, 0);
  TEST_ASSERT_TRUE(unrotated > 0.0f);

  // Rotate so physical index 0 is the newest sample (the first wrap case).
  float rotated[AFDD_WARP_HORIZON];
  const uint16_t oldest = 1;
  for (uint16_t chrono = 0; chrono < AFDD_WARP_HORIZON; ++chrono) {
    rotated[(oldest + chrono) % AFDD_WARP_HORIZON] = static_cast<float>(chrono);
  }
  const float wrapped = afddWarpHalfHorizonDelta(rotated, AFDD_WARP_HORIZON, oldest);
  TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, unrotated, wrapped);

  // Physical-index halves on the rotated buffer would flip sign; chronological must not.
  TEST_ASSERT_TRUE(wrapped > 0.0f);
}

void test_warp_arc_packets_are_freq_midband() {
  TEST_ASSERT_FALSE(afddWarpIsArcPacket(0));
  TEST_ASSERT_TRUE(afddWarpIsArcPacket(1));
  TEST_ASSERT_TRUE(afddWarpIsArcPacket(4));
  TEST_ASSERT_FALSE(afddWarpIsArcPacket(5));
  TEST_ASSERT_FALSE(afddWarpIsArcPacket(7));
}

void test_warp_persist_ms_helpers() {
  AfddWarpConfig c = afddWarpDefaultConfig();
  c.hopSamples = 256.0f;
  c.sampleRateHz = 250000.0f;
  c.watchHorizonMs = 1000.0f;
  const uint16_t need = afddWarpWatchFramesNeeded(c);
  TEST_ASSERT_EQUAL_UINT16(AFDD_WARP_HORIZON, need); // host ring caps research H
  TEST_ASSERT_EQUAL_UINT16(3, afddWarpPersistFrames(c));
  c.nPersist = 0;
  TEST_ASSERT_EQUAL_UINT16(3, afddWarpPersistFrames(c));
  c.nPersist = 8;
  TEST_ASSERT_EQUAL_UINT16(8, afddWarpPersistFrames(c));
}

void test_warp_zero_persist_is_not_zero_frame_high() {
  AfddWarpConfig c = afddWarpDefaultConfig();
  c.nPersist = 0;
  c.tHi = 100.0f;
  AfddWarpState st{};
  afddWarpReset(&st);
  float x[256] = {};
  uint8_t ones[256];
  for (size_t i = 0; i < 256; ++i) {
    ones[i] = 1;
  }
  afddWarpProcessFrame(c, &st, x, 256, ones, 0.0f, 0.0f);
  TEST_ASSERT_NOT_EQUAL(AfddWarpCandidateHigh, st.sense);
  TEST_ASSERT_EQUAL_UINT16(0, st.highPersist);
}

void test_warp_burst_uses_pre_update_ewma() {
  AfddWarpConfig c = afddWarpDefaultConfig();
  c.freezeEwmaOnArm = false;
  c.ewmaAlpha = 0.05f;
  uint8_t ones[256];
  float seed[256];
  fillSparseImpulses(seed, 256, 7);
  for (size_t i = 0; i < 256; ++i) {
    ones[i] = 1;
  }
  AfddWarpState st{};
  afddWarpReset(&st);
  afddWarpProcessFrame(c, &st, seed, 256, ones, 0.0f, 0.0f);
  const float mu = st.ewmaEarc;
  TEST_ASSERT_TRUE(mu > 1.0e-12f);

  // eArc scales with amp^2. 2.05× previous μ is a spec burst; the updated
  // floor would already be ~2.105× and miss.
  const float scale = sqrtf(2.05f);
  float jump[256];
  for (size_t i = 0; i < 256; ++i) {
    jump[i] = seed[i] * scale;
  }
  afddWarpProcessFrame(c, &st, jump, 256, ones, 0.0f, 0.0f);
  const uint16_t last = static_cast<uint16_t>((st.histIdx + AFDD_WARP_HORIZON - 1u) % AFDD_WARP_HORIZON);
  TEST_ASSERT_EQUAL_UINT8(1, st.burstHist[last]);
}

void test_warp_strong_irregularity_holds_precursor_watch() {
  AfddWarpConfig c = afddWarpDefaultConfig();
  c.tPre = 0.01f;
  c.nPre = 2;
  c.tLo = 0.01f;
  c.tHi = 100.0f;
  c.nPersist = 8;
  c.thetaEnergy = 100.0f;
  c.gammaEnergy = 1.0f;
  c.wT = 0.0f;
  c.beta = 0.0f; // sJoint == sWarp; old Low-first order would skip Watch
  AfddWarpState st{};
  afddWarpReset(&st);
  float x[256];
  uint8_t ones[256];
  for (size_t i = 0; i < 256; ++i) {
    ones[i] = 1;
  }
  for (int frame = 0; frame < 20; ++frame) {
    fillSparseImpulses(x, 256, static_cast<unsigned>(10 + frame * 3));
    afddWarpProcessFrame(c, &st, x, 256, ones, 0.0f, 0.0f);
    if (st.sense == AfddWarpPrecursorWatch) {
      break;
    }
  }
  TEST_ASSERT_EQUAL_UINT8(AfddWarpPrecursorWatch, st.sense);
  TEST_ASSERT_TRUE(st.watchAge >= 1);
}

void test_warp_inhibit_clears_temporal_history() {
  AfddWarpConfig c = afddWarpDefaultConfig();
  AfddWarpState st{};
  afddWarpReset(&st);
  st.initialized = true;
  st.ewmaEarc = 4.0f;
  st.ewmaEp[1] = 1.5f;
  st.histFilled = 12;
  st.histIdx = 4;
  st.histEarc[0] = 3.5f;
  st.histIirr[0] = 0.8f;
  st.histEp[1][0] = 2.25f;
  st.burstHist[0] = 1;
  st.prePersist = 5;
  st.highPersist = 6;
  st.watchAge = 9;
  st.sense = AfddWarpPrecursorWatch;
  c.ditherActive = true;
  float x[128] = {};
  uint8_t ones[128];
  for (size_t i = 0; i < 128; ++i) {
    ones[i] = 1;
  }
  afddWarpProcessFrame(c, &st, x, 128, ones, 0.0f, 0.0f);
  TEST_ASSERT_EQUAL_UINT8(AfddWarpInhibited, st.sense);
  TEST_ASSERT_FALSE(st.initialized);
  TEST_ASSERT_EQUAL_UINT16(0, st.prePersist);
  TEST_ASSERT_EQUAL_UINT16(0, st.highPersist);
  TEST_ASSERT_EQUAL_UINT16(0, st.watchAge);
  TEST_ASSERT_EQUAL_UINT16(0, st.histFilled);
  TEST_ASSERT_EQUAL_UINT16(0, st.histIdx);
  TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 0.0f, st.ewmaEarc);
  TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 0.0f, st.ewmaEp[1]);
  TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 0.0f, st.histEarc[0]);
  TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 0.0f, st.histIirr[0]);
  TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 0.0f, st.histEp[1][0]);
  TEST_ASSERT_EQUAL_UINT8(0, st.burstHist[0]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_warp_default_disarmed);
  RUN_TEST(test_warp_dither_inhibits);
  RUN_TEST(test_warp_tone_not_precursor_watch);
  RUN_TEST(test_warp_sparse_impulses_can_raise_irregularity);
  RUN_TEST(test_warp_haar_wpt_length);
  RUN_TEST(test_warp_packet_energy_excludes_blank_haar_windows);
  RUN_TEST(test_warp_process_frame_mask_stops_blank_precursor);
  RUN_TEST(test_warp_default_blanking_inhibits_when_haar_blind);
  RUN_TEST(test_warp_half_horizon_delta_survives_ring_wrap);
  RUN_TEST(test_warp_arc_packets_are_freq_midband);
  RUN_TEST(test_warp_persist_ms_helpers);
  RUN_TEST(test_warp_zero_persist_is_not_zero_frame_high);
  RUN_TEST(test_warp_burst_uses_pre_update_ewma);
  RUN_TEST(test_warp_strong_irregularity_holds_precursor_watch);
  RUN_TEST(test_warp_inhibit_clears_temporal_history);
  return UNITY_END();
}
