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

// Haar WPT host path is hardcoded to J=3 → 8 terminal packets. Do not retarget J alone.
#if AFDD_WARP_J != 3
#error "afdd_warp.h Haar host path requires AFDD_WARP_J == 3 (db4/generic WPT is future work)"
#endif

#ifndef AFDD_WARP_PACKETS
#define AFDD_WARP_PACKETS 8
#elif AFDD_WARP_PACKETS != 8
#error "AFDD_WARP_PACKETS must be 8 while Haar WPT J=3 is hardcoded"
#endif

// Abbreviated host horizon (full research H is hop-derived; see hopSamples / watchHorizonMs).
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
  float hopSamples;  // frame hop (N/2 @ 50% overlap)
  float watchHorizonMs; // PrecursorConfirmed needs full watch age (~T_H)
  uint16_t nPre;
  uint16_t nPersist;
  uint16_t keepMin;
  bool freezeEwmaOnArm;
  bool ditherActive;
  bool blankingAvailable;
  bool afeFault;
  float maskingPenalty;
  float observabilityMin;
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
  float observability;
  uint16_t keepCount;
};

struct AfddWarpState {
  float ewmaEarc;
  float ewmaEp[AFDD_WARP_PACKETS];
  float histEarc[AFDD_WARP_HORIZON];
  float histIirr[AFDD_WARP_HORIZON];
  float histEp[AFDD_WARP_PACKETS][AFDD_WARP_HORIZON]; // frequency-ordered packet energies
  uint8_t burstHist[AFDD_WARP_HORIZON];
  uint16_t histIdx;
  uint16_t histFilled;
  uint16_t prePersist;
  uint16_t highPersist;
  uint16_t watchAge; // frames spent in PrecursorWatch
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
  c.hopSamples = 256.0f;
  c.watchHorizonMs = 1000.0f; // research T_H default; host ring may be shorter
  c.nPre = 5;
  c.nPersist = 3;
  c.keepMin = 0;
  c.freezeEwmaOnArm = true;
  c.ditherActive = false;
  c.blankingAvailable = true;
  c.afeFault = false;
  c.maskingPenalty = 0.0f;
  c.observabilityMin = 0.25f;
  return c;
}

inline uint16_t afddWarpWatchFramesNeeded(const AfddWarpConfig &cfg) {
  if (cfg.sampleRateHz <= 1.0f || cfg.hopSamples <= 1.0f || cfg.watchHorizonMs <= 0.0f) {
    return AFDD_WARP_HORIZON;
  }
  const float hopS = cfg.hopSamples / cfg.sampleRateHz;
  const float frames = cfg.watchHorizonMs * 1.0e-3f / hopS;
  // Host ring is abbreviated; require min(full research H, AFDD_WARP_HORIZON).
  float need = frames;
  if (need > static_cast<float>(AFDD_WARP_HORIZON)) {
    need = static_cast<float>(AFDD_WARP_HORIZON);
  }
  if (need < 4.0f) {
    need = 4.0f;
  }
  return static_cast<uint16_t>(need + 0.5f);
}

inline void afddWarpReset(AfddWarpState *s) {
  if (s == nullptr) {
    return;
  }
  *s = AfddWarpState{};
  s->sense = AfddWarpQuiet;
}

// Drop EWMA / horizon / persist memory so an inhibit gap cannot stitch a later precursor.
inline void afddWarpEnterInhibited(AfddWarpState *s) {
  if (s == nullptr) {
    return;
  }
  *s = AfddWarpState{};
  s->sense = AfddWarpInhibited;
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

  // Level 3 → 8 packets in natural tree order (LLL,LLH,LHL,LHH,HLL,HLH,HHL,HHH).
  float *parents[4] = {LL, LH, HL, HH};
  float natural[AFDD_WARP_PACKETS][AFDD_WARP_MAX_N / 8];
  for (int p = 0; p < 4; ++p) {
    afddWarpHaarStep(parents[p], n2, bufA, bufB);
    const size_t n3 = n2 / 2;
    memcpy(natural[2 * p], bufA, n3 * sizeof(float));
    memcpy(natural[2 * p + 1], bufB, n3 * sizeof(float));
  }
  // Frequency reorder via Gray-code map (ascending Hz for equal Haar bands).
  // natural indices in freq order: 0,1,3,2,6,7,5,4
  static const int kNatFromFreq[AFDD_WARP_PACKETS] = {0, 1, 3, 2, 6, 7, 5, 4};
  const size_t n3 = n / 8;
  for (int f = 0; f < AFDD_WARP_PACKETS; ++f) {
    memcpy(packets[f], natural[kNatFromFreq[f]], n3 * sizeof(float));
  }
  return n3;
}

// Arc interest after frequency reorder: p1–p4 (15.6–78 kHz @ 250 kSPS).
inline bool afddWarpIsArcPacket(int freqPacket) {
  return freqPacket >= 1 && freqPacket <= 4;
}

// Haar J=3: packet coeff i is a linear combination of x[8*i .. 8*i+8) only
// (compact, non-overlapping support). Valid iff every parent sample is kept.
// Zero-stuffed blanks still enter Haar, but contaminated coeffs are excluded
// from energy / kurtosis so periodic zeros cannot mint a broadband precursor.
inline bool afddWarpHaarCoeffKept(const uint8_t *keepMaskParent, size_t parentN, size_t coeffIndex) {
  if (keepMaskParent == nullptr) {
    return true;
  }
  const size_t base = coeffIndex * 8u;
  if (base + 8u > parentN) {
    return false;
  }
  for (size_t k = 0; k < 8u; ++k) {
    if (keepMaskParent[base + k] == 0) {
      return false;
    }
  }
  return true;
}

inline void afddWarpHaarFillCoeffMask(const uint8_t *keepMaskParent, size_t parentN, uint8_t *coeffMask,
                                      size_t plen) {
  if (coeffMask == nullptr) {
    return;
  }
  for (size_t i = 0; i < plen; ++i) {
    coeffMask[i] = afddWarpHaarCoeffKept(keepMaskParent, parentN, i) ? 1u : 0u;
  }
}

inline float afddWarpPacketEnergy(const float *c, size_t len, const uint8_t *keepMaskParent,
                                  size_t parentN, size_t packetIndex) {
  (void)packetIndex; // Haar time support is packet-independent; reserved for db4.
  if (c == nullptr || len == 0) {
    return 0.0f;
  }
  double acc = 0.0;
  size_t keep = 0;
  for (size_t i = 0; i < len; ++i) {
    if (!afddWarpHaarCoeffKept(keepMaskParent, parentN, i)) {
      continue;
    }
    const double v = c[i];
    acc += v * v;
    ++keep;
  }
  if (keep == 0) {
    return 0.0f;
  }
  return static_cast<float>(acc / static_cast<double>(keep));
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
                                              size_t len, const uint8_t *keepMaskParent, size_t parentN) {
  if (len == 0 || len > AFDD_WARP_MAX_N / 8) {
    return 0.0f;
  }
  uint8_t coeffMask[AFDD_WARP_MAX_N / 8];
  afddWarpHaarFillCoeffMask(keepMaskParent, parentN, coeffMask, len);
  double acc = 0.0;
  int count = 0;
  for (int p = 0; p < AFDD_WARP_PACKETS; ++p) {
    if (!afddWarpIsArcPacket(p)) {
      continue;
    }
    acc += afddMacapdExcessKurtosisMasked(packets[p], coeffMask, len);
    ++count;
  }
  if (count == 0) {
    return 0.0f;
  }
  return static_cast<float>(acc / static_cast<double>(count));
}

// Mean(newer half) - mean(older half) over a circular history of `filled` samples.
// `oldestIdx` is the index of the oldest sample (histIdx after write+advance when full).
inline float afddWarpHalfHorizonDelta(const float *hist, uint16_t filled, uint16_t oldestIdx) {
  if (hist == nullptr || filled < 4) {
    return 0.0f;
  }
  const uint16_t half = static_cast<uint16_t>(filled / 2);
  const uint16_t cap = AFDD_WARP_HORIZON;
  double older = 0.0;
  double newer = 0.0;
  for (uint16_t k = 0; k < half; ++k) {
    older += hist[(oldestIdx + k) % cap];
  }
  for (uint16_t k = half; k < filled; ++k) {
    newer += hist[(oldestIdx + k) % cap];
  }
  older /= half;
  newer /= static_cast<double>(filled - half);
  return static_cast<float>(newer - older);
}

inline float afddWarpHorizonPacketCv(const float *hist, uint16_t filled, uint16_t oldestIdx) {
  if (hist == nullptr || filled < 4) {
    return 0.0f;
  }
  const uint16_t cap = AFDD_WARP_HORIZON;
  double mean = 0.0;
  for (uint16_t k = 0; k < filled; ++k) {
    mean += hist[(oldestIdx + k) % cap];
  }
  mean /= static_cast<double>(filled);
  double var = 0.0;
  for (uint16_t k = 0; k < filled; ++k) {
    const double d = hist[(oldestIdx + k) % cap] - mean;
    var += d * d;
  }
  var /= static_cast<double>(filled);
  const double mu = fmax(mean, 1.0e-12);
  return static_cast<float>(sqrt(var) / mu);
}

inline AfddWarpFeatures afddWarpProcessFrame(const AfddWarpConfig &cfg, AfddWarpState *st,
                                             const float *iBlanked, size_t n, const uint8_t *mask,
                                             float macapdScoreRaw, float macapdRTonal) {
  AfddWarpFeatures f{};
  if (st == nullptr || iBlanked == nullptr || n == 0 || n > AFDD_WARP_MAX_N) {
    afddWarpEnterInhibited(st);
    return f;
  }

  f.keepCount = afddMacapdKeepCount(mask, n);
  f.observability = 1.0f - cfg.maskingPenalty;
  if (f.observability < 0.0f) {
    f.observability = 0.0f;
  } else if (f.observability > 1.0f) {
    f.observability = 1.0f;
  }
  const uint16_t keepFloor =
      (cfg.keepMin > 0) ? cfg.keepMin : static_cast<uint16_t>(fmaxf(8.0f, static_cast<float>(n) * 0.25f));
  f.rTonal = macapdRTonal;

  if (cfg.ditherActive || !cfg.blankingAvailable || cfg.afeFault || f.keepCount < keepFloor) {
    afddWarpEnterInhibited(st);
    return f;
  }

  // Ensure length divisible by 8 for Haar WPT.
  size_t nUse = n - (n % 8);
  if (nUse < 8) {
    afddWarpEnterInhibited(st);
    return f;
  }

  float packets[AFDD_WARP_PACKETS][AFDD_WARP_MAX_N / 8];
  const size_t plen = afddWarpHaarWpt3(iBlanked, nUse, packets);
  if (plen == 0) {
    afddWarpEnterInhibited(st);
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
  f.kPkt = afddWarpMeanExcessKurtosisPackets(packets, plen, mask, nUse);
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

  if (!freeze) {
    for (int p = 0; p < AFDD_WARP_PACKETS; ++p) {
      st->ewmaEp[p] = (1.0f - a) * st->ewmaEp[p] + a * ep[p];
    }
    st->ewmaEarc = (1.0f - a) * st->ewmaEarc + a * eArc;
  }

  const float floor = fmaxf(st->ewmaEarc * 2.0f, 1.0e-9f);
  const uint8_t burst = (eArc > floor) ? 1u : 0u;
  const uint16_t writeIdx = st->histIdx;
  st->burstHist[writeIdx] = burst;
  st->histEarc[writeIdx] = eArc;
  for (int p = 0; p < AFDD_WARP_PACKETS; ++p) {
    st->histEp[p][writeIdx] = ep[p];
  }
  st->histIdx = static_cast<uint16_t>((st->histIdx + 1u) % AFDD_WARP_HORIZON);
  if (st->histFilled < AFDD_WARP_HORIZON) {
    ++st->histFilled;
  }

  const uint16_t oldest = (st->histFilled >= AFDD_WARP_HORIZON) ? st->histIdx : 0;

  // Horizon packet CV (σ/μ) over filled history — precursor irregularity core.
  float iIrr = 0.0f;
  int nArcPkt = 0;
  for (int p = 0; p < AFDD_WARP_PACKETS; ++p) {
    if (!afddWarpIsArcPacket(p)) {
      continue;
    }
    iIrr += afddWarpHorizonPacketCv(st->histEp[p], st->histFilled, oldest);
    ++nArcPkt;
  }
  if (nArcPkt > 0) {
    iIrr /= static_cast<float>(nArcPkt);
  }
  f.iIrr = iIrr;
  st->histIirr[writeIdx] = iIrr;

  uint32_t ones = 0;
  for (uint16_t i = 0; i < st->histFilled; ++i) {
    ones += st->burstHist[(oldest + i) % AFDD_WARP_HORIZON];
  }
  f.rMu = (st->histFilled > 0) ? (static_cast<float>(ones) / static_cast<float>(st->histFilled)) : 0.0f;

  f.slopeI = afddWarpHalfHorizonDelta(st->histIirr, st->histFilled, oldest);
  f.slopeE = afddWarpHalfHorizonDelta(st->histEarc, st->histFilled, oldest);

  const float zI = f.iIrr;
  const float zH = f.hNorm;
  const float zMu = f.rMu;
  const float zK = fmaxf(f.kPkt, 0.0f);
  const float zPs = fmaxf(f.slopeI, 0.0f) + 0.5f * fmaxf(f.slopeE, 0.0f);

  // Presence-oriented warp score; masking stays on observability.
  f.sWarp = cfg.wI * zI + cfg.wH * zH + cfg.wMu * zMu + cfg.wK * zK + cfg.wPs * zPs -
            cfg.wT * f.rTonal - cfg.wPk * f.rPkt;
  f.sJoint = cfg.beta * macapdScoreRaw + (1.0f - cfg.beta) * f.sWarp;

  if (f.observability < cfg.observabilityMin) {
    st->prePersist = 0;
    st->highPersist = 0;
    st->watchAge = 0;
    st->sense = AfddWarpQuiet;
    return f;
  }

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

  const uint16_t needWatch = afddWarpWatchFramesNeeded(cfg);

  if (st->highPersist >= cfg.nPersist) {
    st->sense = AfddWarpCandidateHigh;
    st->watchAge = 0;
  } else if (f.sJoint > cfg.tLo) {
    if (st->sense == AfddWarpPrecursorWatch && st->watchAge >= needWatch) {
      st->sense = AfddWarpPrecursorConfirmed;
    } else if (st->sense != AfddWarpPrecursorConfirmed) {
      st->sense = AfddWarpCandidateLow;
      st->watchAge = 0;
    }
  } else if (st->prePersist >= cfg.nPre) {
    if (st->sense != AfddWarpPrecursorWatch) {
      st->watchAge = 0;
    }
    st->sense = AfddWarpPrecursorWatch;
    if (st->watchAge < 0xffffu) {
      ++st->watchAge;
    }
  } else {
    st->sense = AfddWarpQuiet;
    st->watchAge = 0;
  }

  return f;
}

#endif
