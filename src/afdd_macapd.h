#ifndef AFDD_MACAPD_H
#define AFDD_MACAPD_H

// MACAPD — Masking-Aware Carrier-Blanked Arc Precursor Detector
// Host-testable math only. Does NOT trip OUTEN. Not UL 1699B / AFDD product code.
// See docs/FEATURE_AFDD_MACAPD_ALGORITHM.md, docs/FEATURE_AFDD_RESEARCH_2026-08-30.md,
// and docs/FEATURE_AFDD_MACAPD_RESEARCH_2026-08-30.md (literature / edge cases / MEF).

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#ifndef AFDD_MACAPD_MAX_N
#define AFDD_MACAPD_MAX_N 512
#endif

#ifndef AFDD_MACAPD_BURST_HIST
#define AFDD_MACAPD_BURST_HIST 64
#endif

enum AfddMacapdSenseState : uint8_t {
  AfddMacapdInhibited = 0,
  AfddMacapdQuiet = 1,
  AfddMacapdCandidateLow = 2,
  AfddMacapdCandidateHigh = 3,
};

struct AfddMacapdConfig {
  float sampleRateHz;     // e.g. 250000
  float carrierHz;        // FlexPWM carrier for tonal notches / blanking
  float blankHalfWidthS;  // ± blank around each edge (seconds)
  float ewmaAlpha;        // 0..1 adaptive floor / slope smoothing
  float wBand;            // weight on mid-band energy z-score proxy
  float wKurtosis;        // weight on excess kurtosis
  float wBurst;           // weight on burst duty
  float wSlope;           // weight on precursor slope
  float wTonal;           // penalty weight on tonal residual
  float wMask;            // penalty weight on maskingPenalty
  float wCoh;             // weight on I/V coherence (parallel / CM cue)
  float tLo;              // low candidate threshold on raw score
  float tHi;              // high candidate threshold on raw score
  uint16_t nPersist;      // frames above tHi before HIGH
  uint16_t keepMin;       // inhibit if kept samples < keepMin (0 → auto n/4)
  float tonalDeltaHz;     // ±Δ around each k·fc for residual (0 → fs/N)
  bool freezeEwmaOnCandidate; // freeze quiet floor while Candidate*
  bool ditherActive;      // if true → inhibited
  bool blankingAvailable; // if false → inhibited
  bool afeFault;          // if true → inhibited
  float maskingPenalty;   // 0..1 (series L / C-to-ground honesty)
};

struct AfddMacapdFeatures {
  float eL;         // ~5–20 kHz band energy (mean square)
  float eM;         // ~20–50 kHz
  float eH;         // ~50–100 kHz
  float rTonal;     // tonal residual ratio at k·f_c ±Δ (0..1+)
  float kurtosis;   // excess kurtosis of **kept** samples only
  float dBurst;     // recent burst duty (0..1)
  float slopeEm;    // dE_M / dt proxy
  float slopeSk;    // d kurtosis / dt proxy
  float coherence;  // |corr(i,v)| on kept samples if v present, else 0
  float scoreRaw;   // combined score before state machine
  uint16_t keepCount;
};

struct AfddMacapdState {
  float ewmaEm;
  float prevEm;
  float prevSk;
  uint16_t highPersist;
  uint8_t burstHist[AFDD_MACAPD_BURST_HIST];
  uint16_t burstIdx;
  uint16_t burstFilled;
  AfddMacapdSenseState sense;
  bool initialized;
};

inline AfddMacapdConfig afddMacapdDefaultConfig() {
  AfddMacapdConfig c{};
  c.sampleRateHz = 250000.0f;
  c.carrierHz = 20000.0f;
  c.blankHalfWidthS = 2.0e-6f;
  c.ewmaAlpha = 0.05f;
  c.wBand = 1.0f;
  c.wKurtosis = 0.75f;
  c.wBurst = 0.5f;
  c.wSlope = 0.5f;
  c.wTonal = 1.0f;
  c.wMask = 1.0f;
  c.wCoh = 0.25f;
  c.tLo = 1.0f;
  c.tHi = 2.5f;
  c.nPersist = 3;
  c.keepMin = 0; // auto: max(8, n/4)
  c.tonalDeltaHz = 0.0f; // auto: fs/n
  c.freezeEwmaOnCandidate = true;
  c.ditherActive = false;
  c.blankingAvailable = true;
  c.afeFault = false;
  c.maskingPenalty = 0.0f;
  return c;
}

inline void afddMacapdReset(AfddMacapdState *s) {
  if (s == nullptr) {
    return;
  }
  *s = AfddMacapdState{};
  s->sense = AfddMacapdQuiet;
}

// Zero samples within ±blankHalfWidth of each integer carrier period edge.
// maskOut[i] = 1 if sample is valid for scoring, 0 if blanked.
inline void afddMacapdBuildBlankMask(const AfddMacapdConfig &cfg, size_t n, uint8_t *maskOut) {
  if (maskOut == nullptr || n == 0) {
    return;
  }
  const float fs = (cfg.sampleRateHz > 1.0f) ? cfg.sampleRateHz : 250000.0f;
  const float fc = (cfg.carrierHz > 1.0f) ? cfg.carrierHz : 20000.0f;
  const float period = fs / fc;
  const float halfBlank = cfg.blankHalfWidthS * fs;
  for (size_t i = 0; i < n; ++i) {
    if (!cfg.blankingAvailable) {
      maskOut[i] = 1;
      continue;
    }
    const float phase = fmodf(static_cast<float>(i), period);
    const float dist = fminf(phase, period - phase);
    maskOut[i] = (dist > halfBlank) ? 1u : 0u;
  }
}

inline uint16_t afddMacapdKeepCount(const uint8_t *mask, size_t n) {
  if (mask == nullptr || n == 0) {
    return static_cast<uint16_t>(n);
  }
  uint16_t k = 0;
  for (size_t i = 0; i < n; ++i) {
    if (mask[i] != 0) {
      ++k;
    }
  }
  return k;
}

inline void afddMacapdApplyBlank(const float *x, const uint8_t *mask, size_t n, float *yOut) {
  if (x == nullptr || yOut == nullptr || n == 0) {
    return;
  }
  for (size_t i = 0; i < n; ++i) {
    const bool keep = (mask == nullptr) || (mask[i] != 0);
    yOut[i] = keep ? x[i] : 0.0f;
  }
}

// Single-bin Goertzel power (unnormalized) at frequency fHz.
inline float afddMacapdGoertzelPower(const float *x, size_t n, float fs, float fHz) {
  if (x == nullptr || n < 4 || fs <= 0.0f || fHz <= 0.0f || fHz >= fs * 0.5f) {
    return 0.0f;
  }
  const float omega = 2.0f * 3.14159265f * fHz / fs;
  const float coeff = 2.0f * cosf(omega);
  float s0 = 0.0f;
  float s1 = 0.0f;
  float s2 = 0.0f;
  for (size_t i = 0; i < n; ++i) {
    s0 = x[i] + coeff * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  const float power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
  return (power > 0.0f) ? power : 0.0f;
}

inline float afddMacapdMeanSquare(const float *x, size_t n) {
  if (x == nullptr || n == 0) {
    return 0.0f;
  }
  double acc = 0.0;
  for (size_t i = 0; i < n; ++i) {
    const double v = static_cast<double>(x[i]);
    acc += v * v;
  }
  return static_cast<float>(acc / static_cast<double>(n));
}

// Excess kurtosis on **kept** samples only (mask==nullptr → all). Gaussian → ~0.
inline float afddMacapdExcessKurtosisMasked(const float *x, const uint8_t *mask, size_t n) {
  if (x == nullptr || n < 8) {
    return 0.0f;
  }
  size_t keep = 0;
  double mean = 0.0;
  for (size_t i = 0; i < n; ++i) {
    if (mask != nullptr && mask[i] == 0) {
      continue;
    }
    mean += x[i];
    ++keep;
  }
  if (keep < 8) {
    return 0.0f;
  }
  mean /= static_cast<double>(keep);
  double m2 = 0.0;
  double m4 = 0.0;
  for (size_t i = 0; i < n; ++i) {
    if (mask != nullptr && mask[i] == 0) {
      continue;
    }
    const double d = x[i] - mean;
    const double d2 = d * d;
    m2 += d2;
    m4 += d2 * d2;
  }
  m2 /= static_cast<double>(keep);
  m4 /= static_cast<double>(keep);
  if (m2 <= 1.0e-20) {
    return 0.0f;
  }
  return static_cast<float>(m4 / (m2 * m2) - 3.0);
}

inline float afddMacapdExcessKurtosis(const float *x, size_t n) {
  return afddMacapdExcessKurtosisMasked(x, nullptr, n);
}

inline float afddMacapdAbsCorrMasked(const float *a, const float *b, const uint8_t *mask, size_t n) {
  if (a == nullptr || b == nullptr || n < 8) {
    return 0.0f;
  }
  size_t keep = 0;
  double ma = 0.0;
  double mb = 0.0;
  for (size_t i = 0; i < n; ++i) {
    if (mask != nullptr && mask[i] == 0) {
      continue;
    }
    ma += a[i];
    mb += b[i];
    ++keep;
  }
  if (keep < 8) {
    return 0.0f;
  }
  ma /= static_cast<double>(keep);
  mb /= static_cast<double>(keep);
  double num = 0.0;
  double da = 0.0;
  double db = 0.0;
  for (size_t i = 0; i < n; ++i) {
    if (mask != nullptr && mask[i] == 0) {
      continue;
    }
    const double xa = a[i] - ma;
    const double xb = b[i] - mb;
    num += xa * xb;
    da += xa * xa;
    db += xb * xb;
  }
  const double den = sqrt(da * db);
  if (den <= 1.0e-20) {
    return 0.0f;
  }
  const float c = static_cast<float>(num / den);
  return (c < 0.0f) ? -c : c;
}

inline float afddMacapdAbsCorr(const float *a, const float *b, size_t n) {
  return afddMacapdAbsCorrMasked(a, b, nullptr, n);
}

// Crude band energy: sum of Goertzel power at band centers (FFT-free).
inline float afddMacapdBandEnergy(const float *x, size_t n, float fs, float f0, float f1) {
  if (f1 <= f0) {
    return 0.0f;
  }
  const float c0 = 0.25f * f0 + 0.75f * ((f0 + f1) * 0.5f);
  const float c1 = (f0 + f1) * 0.5f;
  const float c2 = 0.75f * ((f0 + f1) * 0.5f) + 0.25f * f1;
  return afddMacapdGoertzelPower(x, n, fs, c0) + afddMacapdGoertzelPower(x, n, fs, c1) +
         afddMacapdGoertzelPower(x, n, fs, c2);
}

// Tonal residual: sum Goertzel at k·fc and ±Δ (carrier-drift honest), / totalEnergy.
inline float afddMacapdTonalResidual(const float *x, size_t n, float fs, float carrierHz,
                                     float totalEnergy, float tonalDeltaHz) {
  if (totalEnergy <= 1.0e-20f) {
    return 0.0f;
  }
  const float dF = (tonalDeltaHz > 0.0f) ? tonalDeltaHz : (fs / static_cast<float>(n > 0 ? n : 1));
  float tonal = 0.0f;
  for (int k = 1; k <= 4; ++k) {
    const float fk = carrierHz * static_cast<float>(k);
    if (fk >= fs * 0.45f) {
      break;
    }
    tonal += afddMacapdGoertzelPower(x, n, fs, fk);
    const float fm = fk - dF;
    const float fp = fk + dF;
    if (fm > 0.0f && fm < fs * 0.45f) {
      tonal += afddMacapdGoertzelPower(x, n, fs, fm);
    }
    if (fp > 0.0f && fp < fs * 0.45f) {
      tonal += afddMacapdGoertzelPower(x, n, fs, fp);
    }
  }
  return tonal / totalEnergy;
}

inline float afddMacapdBurstDuty(const AfddMacapdState &st) {
  if (st.burstFilled == 0) {
    return 0.0f;
  }
  uint32_t ones = 0;
  const uint16_t lim = st.burstFilled;
  for (uint16_t i = 0; i < lim; ++i) {
    ones += st.burstHist[i];
  }
  return static_cast<float>(ones) / static_cast<float>(lim);
}

// Process one frame. iBlanked may be zero-stuffed for Goertzel; moments use mask (kept only).
// mask may be nullptr (treat all kept). Optional v channel for coherence.
inline AfddMacapdFeatures afddMacapdProcessFrame(const AfddMacapdConfig &cfg, AfddMacapdState *st,
                                                 const float *iBlanked, const float *vBlanked,
                                                 size_t n, const uint8_t *mask) {
  AfddMacapdFeatures f{};
  if (st == nullptr || iBlanked == nullptr || n == 0 || n > AFDD_MACAPD_MAX_N) {
    if (st != nullptr) {
      st->sense = AfddMacapdInhibited;
      st->highPersist = 0;
    }
    return f;
  }

  f.keepCount = afddMacapdKeepCount(mask, n);
  const uint16_t keepFloor =
      (cfg.keepMin > 0) ? cfg.keepMin : static_cast<uint16_t>(fmaxf(8.0f, static_cast<float>(n) * 0.25f));

  if (cfg.ditherActive || !cfg.blankingAvailable || cfg.afeFault || f.keepCount < keepFloor) {
    st->sense = AfddMacapdInhibited;
    st->highPersist = 0;
    return f;
  }

  const float fs = cfg.sampleRateHz;
  f.eL = afddMacapdBandEnergy(iBlanked, n, fs, 5000.0f, 20000.0f);
  f.eM = afddMacapdBandEnergy(iBlanked, n, fs, 20000.0f, 50000.0f);
  f.eH = afddMacapdBandEnergy(iBlanked, n, fs, 50000.0f, 100000.0f);
  const float eTot = f.eL + f.eM + f.eH + 1.0e-12f;
  f.rTonal = afddMacapdTonalResidual(iBlanked, n, fs, cfg.carrierHz, eTot, cfg.tonalDeltaHz);
  f.kurtosis = afddMacapdExcessKurtosisMasked(iBlanked, mask, n);
  f.coherence =
      (vBlanked != nullptr) ? afddMacapdAbsCorrMasked(iBlanked, vBlanked, mask, n) : 0.0f;

  if (!st->initialized) {
    st->ewmaEm = f.eM;
    st->prevEm = f.eM;
    st->prevSk = f.kurtosis;
    st->initialized = true;
  }

  const bool freeze = cfg.freezeEwmaOnCandidate &&
                      (st->sense == AfddMacapdCandidateLow || st->sense == AfddMacapdCandidateHigh);
  const float a = cfg.ewmaAlpha;
  const float ewmaPrev = st->ewmaEm;
  if (!freeze) {
    st->ewmaEm = (1.0f - a) * st->ewmaEm + a * f.eM;
  }
  f.slopeEm = f.eM - st->prevEm;
  f.slopeSk = f.kurtosis - st->prevSk;
  st->prevEm = f.eM;
  st->prevSk = f.kurtosis;

  // Adaptive floor: burst if mid-band exceeds EWMA by 2× (or absolute floor).
  const float floor = fmaxf(ewmaPrev * 2.0f, 1.0e-6f);
  const uint8_t burst = (f.eM > floor) ? 1u : 0u;
  st->burstHist[st->burstIdx] = burst;
  st->burstIdx = static_cast<uint16_t>((st->burstIdx + 1u) % AFDD_MACAPD_BURST_HIST);
  if (st->burstFilled < AFDD_MACAPD_BURST_HIST) {
    ++st->burstFilled;
  }
  f.dBurst = afddMacapdBurstDuty(*st);

  // Soft feature scaling (not true z-scores): keep host-testable and stable.
  const float zBand = f.eM / fmaxf(st->ewmaEm, 1.0e-9f);
  const float zSk = fmaxf(f.kurtosis, 0.0f);
  const float zBurst = f.dBurst;
  const float zSlope = fmaxf(f.slopeEm, 0.0f) / fmaxf(st->ewmaEm, 1.0e-9f) +
                       0.25f * fmaxf(f.slopeSk, 0.0f);
  // High I/V coherence on HF can flag parallel / CM coupling (research cue).
  const float zCoh = f.coherence;

  f.scoreRaw = cfg.wBand * zBand + cfg.wKurtosis * zSk + cfg.wBurst * zBurst +
               cfg.wSlope * zSlope + cfg.wCoh * zCoh - cfg.wTonal * f.rTonal -
               cfg.wMask * cfg.maskingPenalty;

  if (f.scoreRaw > cfg.tHi) {
    if (st->highPersist < 0xffffu) {
      ++st->highPersist;
    }
  } else {
    st->highPersist = 0;
  }

  if (st->highPersist >= cfg.nPersist) {
    st->sense = AfddMacapdCandidateHigh;
  } else if (f.scoreRaw > cfg.tLo) {
    st->sense = AfddMacapdCandidateLow;
  } else {
    st->sense = AfddMacapdQuiet;
  }

  return f;
}

// Convenience: blank + process (stack temps — n <= AFDD_MACAPD_MAX_N).
inline AfddMacapdFeatures afddMacapdProcessRaw(const AfddMacapdConfig &cfg, AfddMacapdState *st,
                                               const float *iRaw, const float *vRaw, size_t n) {
  if (iRaw == nullptr || n == 0) {
    if (st != nullptr) {
      st->sense = AfddMacapdInhibited;
      st->highPersist = 0;
    }
    return AfddMacapdFeatures{};
  }
  float iBuf[AFDD_MACAPD_MAX_N];
  float vBuf[AFDD_MACAPD_MAX_N];
  uint8_t mask[AFDD_MACAPD_MAX_N];
  if (n > AFDD_MACAPD_MAX_N) {
    n = AFDD_MACAPD_MAX_N;
  }
  afddMacapdBuildBlankMask(cfg, n, mask);
  afddMacapdApplyBlank(iRaw, mask, n, iBuf);
  const float *vBlanked = nullptr;
  if (vRaw != nullptr) {
    afddMacapdApplyBlank(vRaw, mask, n, vBuf);
    vBlanked = vBuf;
  }
  return afddMacapdProcessFrame(cfg, st, iBuf, vBlanked, n, mask);
}

#endif
