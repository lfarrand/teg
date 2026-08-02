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

// Magnitudes from CMSIS arm_rfft_fast_f32 packed output: element 0 is the DC
// real part, element 1 is the (real) Nyquist bin, then interleaved re/im
// pairs for bins 1..n/2-1. Kept here (portable) so the unpacking is covered
// by the native tests even though the CMSIS transform itself is ARM-only.
inline void spectrumMagnitudesPacked(const float *packed, float *mag, uint32_t halfN) {
  mag[0] = fabsf(packed[0]);
  for (uint32_t k = 1; k < halfN; k++) {
    const float re = packed[2 * k];
    const float im = packed[2 * k + 1];
    mag[k] = sqrtf(re * re + im * im);
  }
}

// Windowed real input for the CMSIS path (same DC removal + Hann as
// prepareSpectrumInput, without the imaginary array)
inline void prepareSpectrumInputReal(const int16_t *samples, uint32_t n, float *out) {
  float mean = 0.0f;
  for (uint32_t i = 0; i < n; i++) {
    mean += samples[i];
  }
  mean /= static_cast<float>(n);
  const float w = 6.28318530717958648f / static_cast<float>(n);
  for (uint32_t i = 0; i < n; i++) {
    const float hann = 0.5f * (1.0f - cosf(w * static_cast<float>(i)));
    out[i] = (static_cast<float>(samples[i]) - mean) * hann;
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

// Quadratic interpolation around a detected peak. Returning a fractional bin
// matters because the k-th harmonic sits at k*f1, not at k*round(f1): the
// integer error otherwise grows with harmonic order and a one-bin search can
// miss most of a non-coherent Hann-windowed harmonic.
inline float spectrumFractionalPeakBin(const float *mag, uint32_t bins, uint32_t peakBin) {
  if (peakBin == 0 || peakBin + 1 >= bins) {
    return static_cast<float>(peakBin);
  }
  const float left = mag[peakBin - 1];
  const float mid = mag[peakBin];
  const float right = mag[peakBin + 1];
  const float denom = left - 2.0f * mid + right;
  if (!isfinite(denom) || fabsf(denom) < 1.0e-20f) {
    return static_cast<float>(peakBin);
  }
  float offset = 0.5f * (left - right) / denom;
  if (offset > 0.5f) offset = 0.5f;
  if (offset < -0.5f) offset = -0.5f;
  return static_cast<float>(peakBin) + offset;
}

// Root-sum-square of the complete Hann main lobe around a fractional target.
// The same width is used for H1 and every harmonic, so the window's coherent
// gain cancels in the THD ratio. Two bins each side cover the Hann main lobe;
// very low fundamentals narrow the band to avoid adjacent-harmonic overlap.
inline float spectrumBandMagnitude(const float *mag, uint32_t bins, float center,
                                   uint32_t radius) {
  if (!isfinite(center) || center < 0.0f || bins == 0) return 0.0f;
  const int32_t nearest = static_cast<int32_t>(floorf(center + 0.5f));
  const int32_t lo = nearest - static_cast<int32_t>(radius);
  const int32_t hi = nearest + static_cast<int32_t>(radius);
  double sumSq = 0.0;
  for (int32_t i = lo; i <= hi; ++i) {
    if (i >= 0 && static_cast<uint32_t>(i) < bins) {
      const double v = mag[i];
      sumSq += v * v;
    }
  }
  return static_cast<float>(sqrt(sumSq));
}

// THD as a percentage: sqrt(sum of squared harmonic peaks 2..maxHarmonic)
// relative to the fundamental peak
inline float thdPercent(const float *mag, uint32_t bins, uint32_t fundamentalBin,
                        uint32_t maxHarmonic = 20) {
  if (fundamentalBin >= bins) return 0.0f;
  const float fundamental = spectrumFractionalPeakBin(mag, bins, fundamentalBin);
  const uint32_t radius = fundamentalBin >= 5 ? 2U : 1U;
  const float h1 = spectrumBandMagnitude(mag, bins, fundamental, radius);
  if (h1 <= 0.0f) {
    return 0.0f;
  }
  double sumSq = 0.0;
  for (uint32_t k = 2; k <= maxHarmonic; k++) {
    const float bin = fundamental * static_cast<float>(k);
    if (bin + static_cast<float>(radius) >= static_cast<float>(bins)) {
      break;
    }
    const float hk = spectrumBandMagnitude(mag, bins, bin, radius);
    sumSq += static_cast<double>(hk) * static_cast<double>(hk);
  }
  return 100.0f * static_cast<float>(sqrt(sumSq)) / h1;
}

#endif
