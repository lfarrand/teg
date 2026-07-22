#ifndef SPECTRUM_MATH_H
#define SPECTRUM_MATH_H

// Spectrum analysis for the capture ring: Hann window, iterative radix-2 FFT,
// magnitude spectrum, fundamental detection and THD. Deliberately a portable
// implementation (not CMSIS) so the exact code that runs on the device is
// unit-tested natively; a 4096-point float FFT costs ~2ms at 600MHz, which is
// fine in HTTP-handler context.

#include <stdint.h>
#include <math.h>

constexpr uint32_t SpectrumMaxPoints = 4096;

// Remove DC (the feedback signal is unipolar) and apply a Hann window
inline void prepareSpectrumInput(const int16_t *samples, uint32_t n, float *re, float *im) {
  float mean = 0.0f;
  for (uint32_t i = 0; i < n; i++) {
    mean += samples[i];
  }
  mean /= static_cast<float>(n);

  const float w = 6.28318530717958648f / static_cast<float>(n);
  for (uint32_t i = 0; i < n; i++) {
    const float hann = 0.5f * (1.0f - cosf(w * static_cast<float>(i)));
    re[i] = (static_cast<float>(samples[i]) - mean) * hann;
    im[i] = 0.0f;
  }
}

// In-place iterative radix-2 FFT; n must be a power of two
inline void fftRadix2(float *re, float *im, uint32_t n) {
  // Bit-reversal permutation
  for (uint32_t i = 1, j = 0; i < n; i++) {
    uint32_t bit = n >> 1;
    for (; j & bit; bit >>= 1) {
      j ^= bit;
    }
    j |= bit;
    if (i < j) {
      float t = re[i]; re[i] = re[j]; re[j] = t;
      t = im[i]; im[i] = im[j]; im[j] = t;
    }
  }

  for (uint32_t len = 2; len <= n; len <<= 1) {
    const float ang = -6.28318530717958648f / static_cast<float>(len);
    const float wr = cosf(ang);
    const float wi = sinf(ang);
    for (uint32_t i = 0; i < n; i += len) {
      float cwr = 1.0f, cwi = 0.0f;
      for (uint32_t k = 0; k < len / 2; k++) {
        const uint32_t a = i + k;
        const uint32_t b = i + k + len / 2;
        const float tr = re[b] * cwr - im[b] * cwi;
        const float ti = re[b] * cwi + im[b] * cwr;
        re[b] = re[a] - tr;
        im[b] = im[a] - ti;
        re[a] += tr;
        im[a] += ti;
        const float t = cwr;
        cwr = t * wr - cwi * wi;
        cwi = t * wi + cwi * wr;
      }
    }
  }
}

// Single-sided magnitude spectrum (bins 0..n/2-1)
inline void spectrumMagnitudes(const float *re, const float *im, float *mag, uint32_t halfN) {
  for (uint32_t i = 0; i < halfN; i++) {
    mag[i] = sqrtf(re[i] * re[i] + im[i] * im[i]);
  }
}

// Largest bin above the near-DC region (Hann smears DC across ~2 bins)
inline uint32_t findFundamentalBin(const float *mag, uint32_t bins, uint32_t minBin = 3) {
  uint32_t best = minBin;
  for (uint32_t i = minBin; i < bins; i++) {
    if (mag[i] > mag[best]) {
      best = i;
    }
  }
  return best;
}

// Peak magnitude within +/-1 bin of the expected position (tolerates
// non-integer harmonic bin positions and window leakage)
inline float harmonicPeak(const float *mag, uint32_t bins, uint32_t center) {
  float peak = 0.0f;
  const uint32_t lo = center > 0 ? center - 1 : 0;
  for (uint32_t i = lo; i <= center + 1 && i < bins; i++) {
    if (mag[i] > peak) {
      peak = mag[i];
    }
  }
  return peak;
}

// THD as a percentage: sqrt(sum of squared harmonic peaks 2..maxHarmonic)
// relative to the fundamental peak
inline float thdPercent(const float *mag, uint32_t bins, uint32_t fundamentalBin,
                        uint32_t maxHarmonic = 20) {
  const float h1 = harmonicPeak(mag, bins, fundamentalBin);
  if (h1 <= 0.0f) {
    return 0.0f;
  }
  float sumSq = 0.0f;
  for (uint32_t k = 2; k <= maxHarmonic; k++) {
    const uint32_t bin = fundamentalBin * k;
    if (bin + 1 >= bins) {
      break;
    }
    const float hk = harmonicPeak(mag, bins, bin);
    sumSq += hk * hk;
  }
  return 100.0f * sqrtf(sumSq) / h1;
}

#endif
