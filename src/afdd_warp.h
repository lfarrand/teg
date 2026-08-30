#ifndef AFDD_WARP_H
#define AFDD_WARP_H

// WARP — Wavelet Arc-precursor Research Pipeline (host-testable math only).
// Does NOT trip OUTEN. Not UL 1699B / AFDD product code.
// Spec: docs/FEATURE_AFDD_MACAPD_RESEARCH_2026-08-30.md §6.
// Interim host bank: Haar WPT J=3 (db4 lifting remains the preferred research bank).

#include "afdd_macapd.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef AFDD_WARP_MAX_N
#define AFDD_WARP_MAX_N 512
#endif

#ifndef AFDD_WARP_J
#define AFDD_WARP_J 3
#endif

#ifndef AFDD_WARP_PACKETS
#define AFDD_WARP_PACKETS (1 << AFDD_WARP_J) // 8
#endif

#ifndef AFDD_WARP_HORIZON
#define AFDD_WARP_HORIZON 32
#endif

enum AfddWarpSenseState : uint8_t {
  AfddWarpInhibited = 0,
  AfddWarpQuiet = 1,
  AfddWarpPrecursorWatch = 2,
  AfddWarpCandidateLow = 3,
  AfddWarpCandidateHigh = 4,
  AfddWarpPrecursorConfirmed = 5, // log tag only
};

struct AfddWarpConfig {
  float sampleRateHz;
  float ewmaAlpha;
  float wI;   // irregularity
  float wH;   // packet entropy
  float wMu;  // micro-burst
  float wK;   // packet kurtosis
  float wPs;  // precursor slopes
  float wT;   // tonal penalty
  float wPk;  // packet concentration penalty
  float wM;   // masking
  float beta; // S_joint = beta*S_macapd + (1-beta)*S_warp
  float tPre;
  float tLo;
  float tHi;
  float gammaEnergy; // PrecursorWatch requires Earc < gamma * thetaEnergy
  float thetaEnergy; // energy trip proxy (research floor)
  uint16_t nPre;
  uint16_t nPersist;
  uint16_t keepMin;
  bool freezeEwmaOnArm;
  bool ditherActive;
  bool blankingAvailable;
  bool afeFault;
  float maskingPenalty;
};

struct AfddWarpFeatures {
  float eArc;
  float iIrr;
  float hNorm;
  float rMu;
  float kPkt;
  float rPkt;
  float slopeI;
  float slopeE;
  float sWarp;
  float sJoint;
  float rTonal;
  uint16_t keepCount;
};

struct AfddWarpState {
  float ewmaEarc;
  float ewmaEp[AFDD_WARP_PACKETS];
  float histEarc[AFDD_WARP_HORIZON];
  float histIirr[AFDD_WARP_HORIZON];
  uint8_t burstHist[AFDD_WARP_HORIZON];
  uint16_t histIdx;
  uint16_t histFilled;
  uint16_t prePersist;
  uint16_t highPersist;
  AfddWarpSenseState sense;
  bool initialized;
};

inline AfddWarpConfig afddWarpDefaultConfig() {
  AfddWarpConfig c{};
  c.sampleRateHz = 250000.0f;
  c.ewmaAlpha = 0.05f;
  c.wI = 1.2f;
  c.wH = 0.6f;
  c.wMu = 0.8f;
  c.wK = 0.5f;
  c.wPs = 0.7f;
  c.wT = 1.0f;
  c.wPk = 0.4f;
  c.wM = 1.0f;
  c.beta = 0.45f;
  c.tPre = 0.9f;
  c.tLo = 1.0f;
  c.tHi = 2.5f;
  c.gammaEnergy = 0.7f;
  c.thetaEnergy = 1.0e-3f;
  c.nPre = 5;
  c.nPersist = 3;
  c.keepMin = 0;
  c.freezeEwmaOnArm = true;
  c.ditherActive = false;
  c.blankingAvailable = true;
  c.afeFault = false;
  c.maskingPenalty = 0.0f;
  return c;
}

inline void afddWarpReset(AfddWarpState *s) {
  if (s == nullptr) {
    return;
  }
  *s = AfddWarpState{};
  s->sense = AfddWarpQuiet;
}

// One Haar analysis step: even/odd → approx / detail (length n must be even).
inline void afddWarpHaarStep(const float *in, size_t n, float *approx, float *detail) {
  const size_t half = n / 2;
  const float s = 0.70710678f;
  for (size_t i = 0; i < half; ++i) {
    const float a = in[2 * i];
    const float b = in[2 * i + 1];
    approx[i] = s * (a + b);
    detail[i] = s * (a - b);
  }
}

// Full Haar WPT to J=3 → 8 equal-length packets written into packets[p][0..len-1].
// n must be >= 8 and divisible by 8. Returns packet length, or 0 on failure.
inline size_t afddWarpHaarWpt3(const float *x, size_t n, float packets[AFDD_WARP_PACKETS][AFDD_WARP_MAX_N / 8]) {
  if (x == nullptr || n < 8 || (n % 8) != 0 || n > AFDD_WARP_MAX_N) {
    return 0;
  }
  float bufA[AFDD_WARP_MAX_N];
  float bufB[AFDD_WARP_MAX_N];
  memcpy(bufA, x, n * sizeof(float));

  // Level 1
  float L[AFDD_WARP_MAX_N / 2];
  float H[AFDD_WARP_MAX_N / 2];
  afddWarpHaarStep(bufA, n, L, H);
  const size_t n1 = n / 2;

  // Level 2 on L and H
  float LL[AFDD_WARP_MAX_N / 4];
  float LH[AFDD_WARP_MAX_N / 4];
  float HL[AFDD_WARP_MAX_N / 4];
  float HH[AFDD_WARP_MAX_N / 4];
  afddWarpHaarStep(L, n1, LL, LH);
  afddWarpHaarStep(H, n1, HL, HH);
  const size_t n2 = n1 / 2;

  // Level 3 → 8 packets
  float *parents[4] = {LL, LH, HL, HH};
  for (int p = 0; p < 4; ++p) {
    afddWarpHaarStep(parents[p], n2, bufA, bufB);
    const size_t n3 = n2 / 2;
    memcpy(packets[2 * p], bufA, n3 * sizeof(float));
    memcpy(packets[2 * p + 1], bufB, n3 * sizeof(float));
  }
  return n / 8;
}

// Arc-oriented packet set for Haar WPT J=3 (mid/high detail indices). Research default.
inline bool afddWarpIsArcPacket(int p) {
  // Skip coarsest approx (0); use 2..7 as broadband-ish packets.
  return p >= 2 && p < AFDD_WARP_PACKETS;
}

inline float afddWarpPacketEnergy(const float *c, size_t len, const uint8_t *keepMaskParent,
                                  size_t parentN, size_t packetIndex) {
  (void)keepMaskParent;
  (void)parentN;
  (void)packetIndex;
  if (c == nullptr || len == 0) {
    return 0.0f;
  }
  double acc = 0.0;
  for (size_t i = 0; i < len; ++i) {
    const double v = c[i];
    acc += v * v;
  }
  return static_cast<float>(acc / static_cast<double>(len));
}

inline float afddWarpPacketEntropyNorm(const float *ep, int nPkt) {
  double sum = 0.0;
  for (int i = 0; i < nPkt; ++i) {
    sum += ep[i];
  }
  if (sum <= 1.0e-20) {
    return 0.0f;
  }
  double H = 0.0;
  for (int i = 0; i < nPkt; ++i) {
    const double q = ep[i] / sum;
    if (q > 1.0e-20) {
      H -= q * log(q) / log(2.0);
    }
  }
  const double Hmax = log(static_cast<double>(nPkt)) / log(2.0);
  if (Hmax <= 1.0e-20) {
    return 0.0f;
  }
  return static_cast<float>(H / Hmax);
}

inline float afddWarpMeanExcessKurtosisPackets(float packets[AFDD_WARP_PACKETS][AFDD_WARP_MAX_N / 8],
                                              size_t len) {
  double acc = 0.0;
  int count = 0;
  for (int p = 0; p < AFDD_WARP_PACKETS; ++p) {
    if (!afddWarpIsArcPacket(p)) {
      continue;
    }
    acc += afddMacapdExcessKurtosis(packets[p], len);
    ++count;
  }
  if (count == 0) {
    return 0.0f;
  }
  return static_cast<float>(acc / static_cast<double>(count));
}

inline AfddWarpFeatures afddWarpProcessFrame(const AfddWarpConfig &cfg, AfddWarpState *st,
                                             const float *iBlanked, size_t n, const uint8_t *mask,
                                             float macapdScoreRaw, float macapdRTonal) {
  AfddWarpFeatures f{};
  if (st == nullptr || iBlanked == nullptr || n == 0 || n > AFDD_WARP_MAX_N) {
    if (st != nullptr) {
      st->sense = AfddWarpInhibited;
    }
    return f;
  }

  f.keepCount = afddMacapdKeepCount(mask, n);
  const uint16_t keepFloor =
      (cfg.keepMin > 0) ? cfg.keepMin : static_cast<uint16_t>(fmaxf(8.0f, static_cast<float>(n) * 0.25f));
  f.rTonal = macapdRTonal;

  if (cfg.ditherActive || !cfg.blankingAvailable || cfg.afeFault || f.keepCount < keepFloor) {
    st->sense = AfddWarpInhibited;
    st->prePersist = 0;
    st->highPersist = 0;
    return f;
  }

  // Ensure length divisible by 8 for Haar WPT.
  size_t nUse = n - (n % 8);
  if (nUse < 8) {
    st->sense = AfddWarpInhibited;
    return f;
  }

  float packets[AFDD_WARP_PACKETS][AFDD_WARP_MAX_N / 8];
  const size_t plen = afddWarpHaarWpt3(iBlanked, nUse, packets);
  if (plen == 0) {
    st->sense = AfddWarpInhibited;
    return f;
  }

  float ep[AFDD_WARP_PACKETS];
  float eArc = 0.0f;
  float eSum = 0.0f;
  float eMax = 0.0f;
  for (int p = 0; p < AFDD_WARP_PACKETS; ++p) {
    ep[p] = afddWarpPacketEnergy(packets[p], plen, mask, nUse, static_cast<size_t>(p));
    eSum += ep[p];
    if (ep[p] > eMax) {
      eMax = ep[p];
    }
    if (afddWarpIsArcPacket(p)) {
      eArc += ep[p];
    }
  }
  f.eArc = eArc;
  f.hNorm = afddWarpPacketEntropyNorm(ep, AFDD_WARP_PACKETS);
  f.kPkt = afddWarpMeanExcessKurtosisPackets(packets, plen);
  f.rPkt = (eSum > 1.0e-20f) ? (eMax / eSum) : 0.0f;

  const bool armed = (st->sense == AfddWarpPrecursorWatch || st->sense == AfddWarpCandidateLow ||
                      st->sense == AfddWarpCandidateHigh || st->sense == AfddWarpPrecursorConfirmed);
  const bool freeze = cfg.freezeEwmaOnArm && armed;
  const float a = cfg.ewmaAlpha;

  if (!st->initialized) {
    st->ewmaEarc = eArc;
    for (int p = 0; p < AFDD_WARP_PACKETS; ++p) {
      st->ewmaEp[p] = ep[p];
    }
    st->initialized = true;
  }

  float iIrr = 0.0f;
  int nArcPkt = 0;
  for (int p = 0; p < AFDD_WARP_PACKETS; ++p) {
    if (!freeze) {
      st->ewmaEp[p] = (1.0f - a) * st->ewmaEp[p] + a * ep[p];
    }
    if (afddWarpIsArcPacket(p)) {
      const float mu = fmaxf(st->ewmaEp[p], 1.0e-12f);
      const float cv = fabsf(ep[p] - st->ewmaEp[p]) / mu;
      iIrr += cv;
      ++nArcPkt;
    }
  }
  if (nArcPkt > 0) {
    iIrr /= static_cast<float>(nArcPkt);
  }
  f.iIrr = iIrr;

  if (!freeze) {
    st->ewmaEarc = (1.0f - a) * st->ewmaEarc + a * eArc;
  }

  const float floor = fmaxf(st->ewmaEarc * 2.0f, 1.0e-9f);
  const uint8_t burst = (eArc > floor) ? 1u : 0u;
  st->burstHist[st->histIdx] = burst;
  st->histEarc[st->histIdx] = eArc;
  st->histIirr[st->histIdx] = iIrr;
  st->histIdx = static_cast<uint16_t>((st->histIdx + 1u) % AFDD_WARP_HORIZON);
  if (st->histFilled < AFDD_WARP_HORIZON) {
    ++st->histFilled;
  }

  uint32_t ones = 0;
  for (uint16_t i = 0; i < st->histFilled; ++i) {
    ones += st->burstHist[i];
  }
  f.rMu = (st->histFilled > 0) ? (static_cast<float>(ones) / static_cast<float>(st->histFilled)) : 0.0f;

  // Horizon slopes: half-horizon mean delta.
  f.slopeI = 0.0f;
  f.slopeE = 0.0f;
  if (st->histFilled >= 4) {
    const uint16_t half = static_cast<uint16_t>(st->histFilled / 2);
    double mI0 = 0.0;
    double mI1 = 0.0;
    double mE0 = 0.0;
    double mE1 = 0.0;
    for (uint16_t i = 0; i < half; ++i) {
      mI0 += st->histIirr[i];
      mE0 += st->histEarc[i];
    }
    for (uint16_t i = half; i < st->histFilled; ++i) {
      mI1 += st->histIirr[i];
      mE1 += st->histEarc[i];
    }
    mI0 /= half;
    mE0 /= half;
    mI1 /= (st->histFilled - half);
    mE1 /= (st->histFilled - half);
    f.slopeI = static_cast<float>(mI1 - mI0);
    f.slopeE = static_cast<float>(mE1 - mE0);
  }

  const float zI = f.iIrr;
  const float zH = f.hNorm;
  const float zMu = f.rMu;
  const float zK = fmaxf(f.kPkt, 0.0f);
  const float zPs = fmaxf(f.slopeI, 0.0f) + 0.5f * fmaxf(f.slopeE, 0.0f);

  f.sWarp = cfg.wI * zI + cfg.wH * zH + cfg.wMu * zMu + cfg.wK * zK + cfg.wPs * zPs -
            cfg.wT * f.rTonal - cfg.wPk * f.rPkt - cfg.wM * cfg.maskingPenalty;
  f.sJoint = cfg.beta * macapdScoreRaw + (1.0f - cfg.beta) * f.sWarp;

  // State machine (research log only — never OUTEN).
  const bool energyLow = (f.eArc < cfg.gammaEnergy * cfg.thetaEnergy);
  if (f.sWarp >= cfg.tPre && energyLow) {
    if (st->prePersist < 0xffffu) {
      ++st->prePersist;
    }
  } else {
    st->prePersist = 0;
  }

  if (f.sJoint > cfg.tHi) {
    if (st->highPersist < 0xffffu) {
      ++st->highPersist;
    }
  } else {
    st->highPersist = 0;
  }

  if (st->highPersist >= cfg.nPersist) {
    st->sense = AfddWarpCandidateHigh;
  } else if (f.sJoint > cfg.tLo) {
    if (st->sense == AfddWarpPrecursorWatch) {
      st->sense = AfddWarpPrecursorConfirmed;
    } else {
      st->sense = AfddWarpCandidateLow;
    }
  } else if (st->prePersist >= cfg.nPre) {
    st->sense = AfddWarpPrecursorWatch;
  } else {
    st->sense = AfddWarpQuiet;
  }

  return f;
}

#endif
