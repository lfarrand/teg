// Tests for the spectrum analysis chain (spectrum_math.h)

#include <unity.h>
#include <math.h>
#include <string.h>
#include <spectrum_math.h>
#include <spectrum_wire.h>

static float re[SpectrumMaxPoints];
static float im[SpectrumMaxPoints];
static float mag[SpectrumMaxPoints / 2];
static int16_t samples[SpectrumMaxPoints];

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// FFT core
// ---------------------------------------------------------------------------

void test_fft_impulse_is_flat() {
  const uint32_t n = 256;
  for (uint32_t i = 0; i < n; i++) { re[i] = 0.0f; im[i] = 0.0f; }
  re[0] = 1.0f;
  fftRadix2(re, im, n);
  for (uint32_t i = 0; i < n; i++) {
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, sqrtf(re[i] * re[i] + im[i] * im[i]));
  }
}

void test_fft_single_tone_lands_on_its_bin() {
  const uint32_t n = 1024;
  const uint32_t k = 37;
  for (uint32_t i = 0; i < n; i++) {
    re[i] = sinf(6.28318530717958648f * k * i / n);
    im[i] = 0.0f;
  }
  fftRadix2(re, im, n);
  spectrumMagnitudes(re, im, mag, n / 2);
  // Peak at bin k with magnitude n/2; everything else tiny
  TEST_ASSERT_FLOAT_WITHIN(n / 2 * 0.01f, n / 2.0f, mag[k]);
  for (uint32_t i = 1; i < n / 2; i++) {
    if (i < k - 1 || i > k + 1) {
      TEST_ASSERT_LESS_THAN_FLOAT(n / 2 * 0.01f, mag[i]);
    }
  }
  TEST_ASSERT_EQUAL_UINT32(k, findFundamentalBin(mag, n / 2));
}

// ---------------------------------------------------------------------------
// Input preparation
// ---------------------------------------------------------------------------

void test_prepare_removes_dc_and_windows() {
  const uint32_t n = 512;
  for (uint32_t i = 0; i < n; i++) {
    samples[i] = 2048; // pure DC, as an unipolar ADC would give
  }
  prepareSpectrumInput(samples, n, re, im);
  for (uint32_t i = 0; i < n; i++) {
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, re[i]);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, im[i]);
  }
}

// ---------------------------------------------------------------------------
// Full chain: THD of a known signal
// ---------------------------------------------------------------------------

void test_full_chain_measures_known_thd() {
  const uint32_t n = 4096;
  const uint32_t fundBin = 40;
  // Fundamental 1000 counts + 3rd harmonic at 10% + 5th at 5%, on a DC offset:
  // THD = sqrt(0.1^2 + 0.05^2) = 11.18%
  for (uint32_t i = 0; i < n; i++) {
    const float phi = 6.28318530717958648f * fundBin * i / n;
    samples[i] = static_cast<int16_t>(2048 + 1000.0f * sinf(phi) + 100.0f * sinf(3 * phi) +
                                      50.0f * sinf(5 * phi));
  }
  prepareSpectrumInput(samples, n, re, im);
  fftRadix2(re, im, n);
  spectrumMagnitudes(re, im, mag, n / 2);

  const uint32_t found = findFundamentalBin(mag, n / 2);
  TEST_ASSERT_UINT32_WITHIN(1, fundBin, found);

  const float thd = thdPercent(mag, n / 2, found);
  TEST_ASSERT_FLOAT_WITHIN(0.6f, 11.18f, thd);
}

void test_clean_sine_has_negligible_thd() {
  const uint32_t n = 4096;
  for (uint32_t i = 0; i < n; i++) {
    samples[i] = static_cast<int16_t>(2048 + 1500.0f * sinf(6.28318530717958648f * 25 * i / n));
  }
  prepareSpectrumInput(samples, n, re, im);
  fftRadix2(re, im, n);
  spectrumMagnitudes(re, im, mag, n / 2);
  TEST_ASSERT_LESS_THAN_FLOAT(0.5f, thdPercent(mag, n / 2, findFundamentalBin(mag, n / 2)));
}

void test_packed_magnitudes_match_complex_layout() {
  // CMSIS rfft packing: [DC, Nyquist, re1, im1, re2, im2, ...]. Build a packed
  // buffer whose bins are known and check the unpacked magnitudes.
  float packed[16] = {0};
  packed[0] = -3.0f; // DC (sign must not survive)
  packed[1] = 9.0f;  // Nyquist, ignored by the single-sided output
  packed[2] = 3.0f;  // bin 1: 3+4i -> 5
  packed[3] = 4.0f;
  packed[4] = 0.0f;  // bin 2: 0-2i -> 2
  packed[5] = -2.0f;
  float m[8];
  spectrumMagnitudesPacked(packed, m, 8);
  TEST_ASSERT_EQUAL_FLOAT(3.0f, m[0]);
  TEST_ASSERT_EQUAL_FLOAT(5.0f, m[1]);
  TEST_ASSERT_EQUAL_FLOAT(2.0f, m[2]);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, m[3]);
}

void test_real_and_complex_prep_agree() {
  const uint32_t n = 512;
  for (uint32_t i = 0; i < n; i++) {
    samples[i] = static_cast<int16_t>(2048 + 900.0f * sinf(6.28318530717958648f * 7 * i / n));
  }
  prepareSpectrumInput(samples, n, re, im);
  static float realOnly[512];
  prepareSpectrumInputReal(samples, n, realOnly);
  for (uint32_t i = 0; i < n; i++) {
    TEST_ASSERT_EQUAL_FLOAT(re[i], realOnly[i]);
  }
}

void test_thd_guards() {
  for (uint32_t i = 0; i < 64; i++) mag[i] = 0.0f;
  TEST_ASSERT_EQUAL_FLOAT(0.0f, thdPercent(mag, 64, 10)); // silent input
  mag[10] = 1.0f;
  TEST_ASSERT_EQUAL_FLOAT(0.0f, thdPercent(mag, 64, 10)); // no harmonics present
  // Harmonics beyond the spectrum end are ignored, not read out of bounds
  mag[60] = 5.0f;
  TEST_ASSERT_EQUAL_FLOAT(0.0f, thdPercent(mag, 64, 40));
}

void test_noncoherent_high_order_harmonic_is_not_missed() {
  const uint32_t n = 4096;
  const float cycles = 25.37f;
  for (uint32_t i = 0; i < n; ++i) {
    const float phase = 6.28318530717958648f * cycles * i / n;
    // The seventh harmonic is 2.6 bins away from 7*round(f1), which defeated
    // the old integer-bin +/-1 search.
    samples[i] = static_cast<int16_t>(2048.0f + 1300.0f * sinf(phase) +
                                      130.0f * sinf(7.0f * phase));
  }
  prepareSpectrumInput(samples, n, re, im);
  fftRadix2(re, im, n);
  spectrumMagnitudes(re, im, mag, n / 2);
  const uint32_t fundamental = findFundamentalBin(mag, n / 2);
  const float fractional = spectrumFractionalPeakBin(mag, n / 2, fundamental);
  TEST_ASSERT_FLOAT_WITHIN(0.20f, cycles, fractional);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 10.0f, thdPercent(mag, n / 2, fundamental, 10));
}

void test_spectrum_max_points_is_4096() {
  TEST_ASSERT_EQUAL_UINT32(4096, SpectrumMaxPoints);
}

void test_spectrum_wire_roundtrip() {
  const float fund = 2.0f;
  const float mags[4] = {2.0f, 1.0f, 0.5f, 0.0f};
  SpectrumWireFields in{};
  in.available = true;
  in.sampleHz = 10000;
  in.points = 8;
  in.computeMicros = 1234;
  in.binHz = 1250.0f;
  in.fundamentalHz = 1250.0f;
  in.thdPercent = 11.18f;

  uint8_t buf[64];
  const size_t n = spectrumWirePack(buf, sizeof(buf), in, mags, fund);
  TEST_ASSERT_EQUAL_UINT32(spectrumWirePayloadSize(4), n);
  TEST_ASSERT_EQUAL_UINT8('T', buf[0]);
  TEST_ASSERT_EQUAL_UINT8('E', buf[1]);
  TEST_ASSERT_EQUAL_UINT8('G', buf[2]);
  TEST_ASSERT_EQUAL_UINT8('S', buf[3]);
  TEST_ASSERT_EQUAL_UINT8(SpectrumWireVersion, buf[4]);
  TEST_ASSERT_EQUAL_UINT8(1, buf[5]);

  SpectrumWireFields out{};
  uint16_t bins[8] = {0};
  uint16_t binCount = 0;
  TEST_ASSERT_TRUE(spectrumWireUnpack(buf, n, out, bins, 8, &binCount));
  TEST_ASSERT_TRUE(out.available);
  TEST_ASSERT_EQUAL_UINT32(10000, out.sampleHz);
  TEST_ASSERT_EQUAL_UINT32(8, out.points);
  TEST_ASSERT_EQUAL_UINT32(1234, out.computeMicros);
  TEST_ASSERT_EQUAL_FLOAT(1250.0f, out.binHz);
  TEST_ASSERT_EQUAL_FLOAT(1250.0f, out.fundamentalHz);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 11.18f, out.thdPercent);
  TEST_ASSERT_EQUAL_UINT16(4, binCount);
  TEST_ASSERT_EQUAL_UINT16(10000, bins[0]);
  TEST_ASSERT_EQUAL_UINT16(5000, bins[1]);
  TEST_ASSERT_EQUAL_UINT16(2500, bins[2]);
  TEST_ASSERT_EQUAL_UINT16(0, bins[3]);
}

void test_spectrum_wire_unavailable() {
  SpectrumWireFields in{};
  in.available = false;
  in.sampleHz = 8000;
  in.points = 1024;
  in.computeMicros = 9;
  in.binHz = 7.8125f;
  in.fundamentalHz = 50.0f;
  in.thdPercent = 3.0f;

  uint8_t buf[64];
  const size_t n = spectrumWirePack(buf, sizeof(buf), in, nullptr, 1.0f);
  TEST_ASSERT_EQUAL_UINT32(SpectrumWireHeaderSize, n);
  TEST_ASSERT_EQUAL_UINT8(0, buf[5]);
  uint16_t storedBins = 0xFFFF;
  memcpy(&storedBins, buf + 6, 2);
  TEST_ASSERT_EQUAL_UINT16(0, storedBins);
  TEST_ASSERT_EQUAL_UINT32(0, spectrumWirePack(buf, SpectrumWireHeaderSize - 1, in, nullptr, 1.0f));

  SpectrumWireFields out{};
  uint16_t bins[4] = {0};
  uint16_t binCount = 99;
  TEST_ASSERT_TRUE(spectrumWireUnpack(buf, n, out, bins, 4, &binCount));
  TEST_ASSERT_FALSE(out.available);
  TEST_ASSERT_EQUAL_UINT32(8000, out.sampleHz);
  TEST_ASSERT_EQUAL_UINT32(1024, out.points);
  TEST_ASSERT_EQUAL_UINT16(0, binCount);
}

void test_spectrum_wire_rejects_bad_header() {
  uint8_t buf[SpectrumWireHeaderSize] = {0};
  memcpy(buf, "TEGS", 4);
  buf[4] = SpectrumWireVersion;

  SpectrumWireFields f{};
  uint16_t bins[1] = {0};
  uint16_t binCount = 0;
  TEST_ASSERT_FALSE(spectrumWireUnpack(buf, SpectrumWireHeaderSize - 1, f, bins, 1, &binCount));

  buf[0] = 'X';
  TEST_ASSERT_FALSE(spectrumWireUnpack(buf, SpectrumWireHeaderSize, f, bins, 1, &binCount));
  buf[0] = 'T';

  buf[4] = 2;
  TEST_ASSERT_FALSE(spectrumWireUnpack(buf, SpectrumWireHeaderSize, f, bins, 1, &binCount));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_fft_impulse_is_flat);
  RUN_TEST(test_fft_single_tone_lands_on_its_bin);
  RUN_TEST(test_prepare_removes_dc_and_windows);
  RUN_TEST(test_full_chain_measures_known_thd);
  RUN_TEST(test_clean_sine_has_negligible_thd);
  RUN_TEST(test_packed_magnitudes_match_complex_layout);
  RUN_TEST(test_real_and_complex_prep_agree);
  RUN_TEST(test_thd_guards);
  RUN_TEST(test_noncoherent_high_order_harmonic_is_not_missed);
  RUN_TEST(test_spectrum_max_points_is_4096);
  RUN_TEST(test_spectrum_wire_roundtrip);
  RUN_TEST(test_spectrum_wire_unavailable);
  RUN_TEST(test_spectrum_wire_rejects_bad_header);
  return UNITY_END();
}
