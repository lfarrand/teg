// Spectral verification of the modulation schemes' harmonic claims.
//
// Synthesizes the switched output waveform on the host by driving the SAME
// per-cycle pipeline the ISR runs (modulationCycleDuties + modulationCellPlan
// from modulation.h), rendering each cell's centre-aligned comparator output
// at 64 samples per carrier cycle, and FFTs the combined waveform with the
// firmware's own radix-2 FFT (spectrum_math.h).
//
// Window geometry is chosen for ZERO spectral leakage: carrier ratio mf = 32
// exactly (DDS increment 2^32/32), 8 fundamental cycles per window, so every
// harmonic lands on an exact bin and a rectangular window is exact.
// Fundamental = bin 8, k-th harmonic = bin 8k, carrier = bin 256.
//
// Baseline model error (why tolerances are what they are): regular sampling
// at mf=32 attenuates the fundamental by ~sin(pi/32)/(pi/32) = 0.16% and adds
// low-order distortion of the same order; the 2048-entry interpolated LUT and
// Q15/duty quantization contribute <0.1%; pulse edges quantize to the 64x
// render grid but centred pulses keep first-order symmetry, so per-harmonic
// error stays ~1e-3. Absolute tolerances of 0.01-0.02 leave ~10x headroom.

#include <unity.h>
#include <math.h>
#include <string.h>
#include <modulation.h>
#include <spectrum_math.h>

void setUp() {}
void tearDown() {}

constexpr uint32_t OS = 64;                  // rendered samples per carrier cycle
constexpr uint32_t MF = 32;                  // carrier cycles per fundamental
constexpr uint32_t FUNDS = 8;                // fundamentals per analysis window
constexpr uint32_t CYCLES = MF * FUNDS;      // 256 carrier cycles per window
constexpr uint32_t N = OS * CYCLES;          // 16384 samples
constexpr uint32_t F0 = FUNDS;               // fundamental bin
constexpr uint32_t FC = FUNDS * MF;          // carrier bin (256)
constexpr uint32_t PHASE_INC = 1UL << 27;    // 2^32 / 32, exact

static int16_t lut[SpwmLutSize];
static float waveA[N];
static float waveB[N];
static float fftRe[N];
static float fftIm[N];
static float mag[N / 2];
static uint16_t dutyLog[CYCLES]; // one chosen cell's duty per cycle

// Render one carrier cycle of a centre-aligned comparator: a pulse of width
// duty/65535 centred in the cycle. Fractional-area (boxcar) sampling - each
// sample carries the pulse's overlap with its [s, s+1) interval - keeps the
// per-cycle area exact. Hard 0/1 thresholding would quantize pulse width to
// 1/64 of a cycle and alias the near-sample-rate carrier groups (63/64/65 fc)
// onto baseband at the ~1% level, swamping every tolerance in this file.
static void renderCycle(float *dst, uint16_t duty, bool inverted) {
  const double w = static_cast<double>(duty) / 65535.0 * static_cast<double>(OS);
  const double lo = (static_cast<double>(OS) - w) / 2.0;
  const double hi = (static_cast<double>(OS) + w) / 2.0;
  for (uint32_t s = 0; s < OS; s++) {
    double a = hi < s + 1.0 ? hi : s + 1.0;
    double b = lo > s ? lo : s;
    double on = a - b;
    if (on < 0.0) on = 0.0;
    dst[s] = static_cast<float>(inverted ? 1.0 - on : on);
  }
}

struct SynthSpec {
  uint8_t scheme = ModSchemeSpwmUnipolar;
  uint8_t disposition = CarrierPd;
  uint8_t cells = 2;
  uint16_t indexMilli = 1000;
  uint8_t refWave = RefWaveSine;
  bool thirdHarmonic = false;
  uint8_t dpwmVariant = DpwmGeneralised;
  int32_t clampDeg = 0;
  bool nearestLevel = false;
  uint32_t phase0 = 0;
  int logCell = -1; // record this cell's duties into dutyLog
};

// Drive the firmware pipeline for one window; out = sum of weights[k] * cell_k
static void synthesize(const SynthSpec &sp, const float *weights, float *out) {
  buildUnitReferenceLut(lut, SpwmLutSize, sp.refWave, sp.thirdHarmonic);
  ModCycleConfig mc;
  mc.scheme = sp.scheme;
  mc.cells = sp.cells;
  mc.dpwmVariant = sp.dpwmVariant;
  mc.dpwmClampPhase = degreesToPhase(sp.clampDeg);
  mc.nearestLevel = sp.nearestLevel;
  mc.dtCompQ15 = 0;
  CellPlan plans[MaxModulationCells];
  const uint8_t n = modulationCellCount(sp.scheme, sp.cells);
  for (uint8_t k = 0; k < n; k++) {
    plans[k] = modulationCellPlan(sp.scheme, sp.disposition, k, sp.cells);
  }
  const uint32_t idxQ15 = indexMilliToQ15(sp.indexMilli);
  uint32_t phase = sp.phase0;
  float cellBuf[OS];
  for (uint32_t cyc = 0; cyc < CYCLES; cyc++) {
    uint16_t duties[MaxModulationCells];
    modulationCycleDuties(lut, phase, idxQ15, mc, plans, duties);
    if (sp.logCell >= 0) {
      dutyLog[cyc] = duties[sp.logCell];
    }
    float *dst = out + cyc * OS;
    for (uint32_t s = 0; s < OS; s++) {
      dst[s] = 0.0f;
    }
    for (uint8_t k = 0; k < n; k++) {
      if (weights[k] == 0.0f) {
        continue;
      }
      renderCycle(cellBuf, duties[k], plans[k].polarityInverted);
      for (uint32_t s = 0; s < OS; s++) {
        dst[s] += weights[k] * cellBuf[s];
      }
    }
    phase += PHASE_INC;
  }
}

// FFT with DC removal, rectangular window (exact for our periodic signals),
// magnitudes normalized so a sine of amplitude A reads A at its bin
static void analyze(const float *w) {
  double mean = 0.0;
  for (uint32_t i = 0; i < N; i++) {
    mean += w[i];
  }
  mean /= N;
  for (uint32_t i = 0; i < N; i++) {
    fftRe[i] = static_cast<float>(w[i] - mean);
    fftIm[i] = 0.0f;
  }
  fftRadix2(fftRe, fftIm, N);
  for (uint32_t k = 0; k < N / 2; k++) {
    mag[k] = 2.0f * sqrtf(fftRe[k] * fftRe[k] + fftIm[k] * fftIm[k]) / static_cast<float>(N);
  }
}

static float harmonic(uint32_t k) { return mag[F0 * k]; }

static float bandPeak(uint32_t lo, uint32_t hi) {
  float peak = 0.0f;
  for (uint32_t i = lo; i <= hi && i < N / 2; i++) {
    if (mag[i] > peak) {
      peak = mag[i];
    }
  }
  return peak;
}

static const float DIFF2[4] = {1.0f, -1.0f, 0.0f, 0.0f}; // H-bridge / line-line
static const float SUM2[4] = {1.0f, 1.0f, 0.0f, 0.0f};   // cascaded / stacked pair
static const float LEG0[4] = {1.0f, 0.0f, 0.0f, 0.0f};

// ---------------------------------------------------------------------------
// Unipolar vs bipolar SPWM
// ---------------------------------------------------------------------------

void test_unipolar_fundamental_linearity() {
  // H-bridge differential fundamental = m, no even harmonics, no DC offset bias
  const uint16_t ms[3] = {400, 800, 1000};
  for (int i = 0; i < 3; i++) {
    SynthSpec sp;
    sp.scheme = ModSchemeSpwmUnipolar;
    sp.indexMilli = ms[i];
    synthesize(sp, DIFF2, waveA);
    analyze(waveA);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, ms[i] / 1000.0f, harmonic(1));
    TEST_ASSERT_LESS_THAN_FLOAT(0.005f, harmonic(2));
    TEST_ASSERT_LESS_THAN_FLOAT(0.005f, harmonic(4));
  }
}

void test_unipolar_carrier_cancellation() {
  // The fc carrier group cancels in the differential output. Mechanism: each
  // leg's fc group holds the carrier line plus EVEN-n sidebands (double-edge
  // parity already zeroes odd n per leg); the complementary leg flips the
  // sign of odd-n terms only, so subtracting cancels the line and even-n
  // exactly - and doubles the small odd-n residue. That residue is the
  // regular-sampling image pair of the fundamental at (mf +/- 1) f0
  // (bins 248/264), amplitude ~ m/mf ~ 0.03 - physically real, present in
  // the hardware too, and far below the 2fc switching group.
  SynthSpec sp;
  sp.scheme = ModSchemeSpwmUnipolar;
  sp.indexMilli = 800;
  synthesize(sp, DIFF2, waveA);
  analyze(waveA);
  TEST_ASSERT_LESS_THAN_FLOAT(0.005f, mag[FC]);          // carrier line: gone
  TEST_ASSERT_LESS_THAN_FLOAT(0.005f, mag[FC - 2 * F0]); // even sidebands: gone
  TEST_ASSERT_LESS_THAN_FLOAT(0.005f, mag[FC + 2 * F0]);
  const float fcPeak = bandPeak(FC - 3 * F0, FC + 3 * F0); // sampling images only
  const float fc2Peak = bandPeak(2 * FC - 3 * F0, 2 * FC + 3 * F0);
  TEST_ASSERT_LESS_THAN_FLOAT(0.05f, fcPeak);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.15f, fc2Peak);
  TEST_ASSERT_LESS_THAN_FLOAT(fc2Peak / 5.0f, fcPeak);
}

void test_bipolar_carrier_present() {
  // Two-level output: dominant switching component sits AT the carrier
  SynthSpec sp;
  sp.scheme = ModSchemeSpwmBipolar;
  sp.indexMilli = 800;
  synthesize(sp, DIFF2, waveA);
  analyze(waveA);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.8f, harmonic(1)); // fundamental intact
  TEST_ASSERT_GREATER_THAN_FLOAT(0.4f, mag[FC]);      // strong carrier line
  // Unipolar at the same index for comparison: >10x quieter at fc
  synthesize(SynthSpec{ModSchemeSpwmUnipolar, CarrierPd, 2, 800}, DIFF2, waveB);
  const float bipolarFc = mag[FC];
  analyze(waveB);
  TEST_ASSERT_LESS_THAN_FLOAT(bipolarFc / 10.0f, bandPeak(FC - 3 * F0, FC + 3 * F0));
}

// ---------------------------------------------------------------------------
// THIPWM
// ---------------------------------------------------------------------------

void test_thipwm_third_harmonic_in_hbridge() {
  // On a single-phase H-bridge the injected third does NOT cancel: the
  // complement leg mirrors the whole reference, so H3/H1 = 1/6
  SynthSpec sp;
  sp.scheme = ModSchemeThipwm;
  sp.thirdHarmonic = true;
  sp.indexMilli = 1000;
  synthesize(sp, DIFF2, waveA);
  analyze(waveA);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f / 6.0f, harmonic(3) / harmonic(1));
}

void test_thipwm_extended_linear_range() {
  // Crest flattening lets the index reach 1.155 without rail clipping:
  // fundamental tracks m beyond the SPWM limit of 1.0, low-order distortion
  // (5th/7th) stays small
  SynthSpec sp;
  sp.scheme = ModSchemeThipwm;
  sp.thirdHarmonic = true;
  sp.indexMilli = 1155;
  synthesize(sp, DIFF2, waveA);
  analyze(waveA);
  TEST_ASSERT_FLOAT_WITHIN(0.012f, 1.155f, harmonic(1));
  TEST_ASSERT_LESS_THAN_FLOAT(0.015f, harmonic(5));
  TEST_ASSERT_LESS_THAN_FLOAT(0.015f, harmonic(7));
}

void test_thipwm_three_phase_line_line_cancels_third() {
  // Two THIPWM legs 120deg apart: the injected third is common-mode and
  // cancels in line-line, while the fundamental scales by sqrt(3)
  SynthSpec sp;
  sp.scheme = ModSchemeThipwm;
  sp.thirdHarmonic = true;
  sp.indexMilli = 1000;
  synthesize(sp, LEG0, waveA);
  sp.phase0 = static_cast<uint32_t>(0) - PhaseShift120Deg;
  synthesize(sp, LEG0, waveB);
  for (uint32_t i = 0; i < N; i++) {
    waveA[i] -= waveB[i];
  }
  analyze(waveA);
  TEST_ASSERT_FLOAT_WITHIN(0.012f, 0.866f, harmonic(1)); // sqrt(3) * m/2
  TEST_ASSERT_LESS_THAN_FLOAT(0.01f, harmonic(3));
}

// ---------------------------------------------------------------------------
// SVPWM / DPWM
// ---------------------------------------------------------------------------

void test_svpwm_line_line_triplen_free_and_linear() {
  const uint16_t ms[3] = {600, 1000, 1150};
  for (int i = 0; i < 3; i++) {
    SynthSpec sp;
    sp.scheme = ModSchemeSvpwm;
    sp.cells = 3;
    sp.indexMilli = ms[i];
    synthesize(sp, DIFF2, waveA); // legA - legB = line-line
    analyze(waveA);
    // line-line fundamental = sqrt(3)/2 * m in leg units
    TEST_ASSERT_FLOAT_WITHIN(0.012f, 0.8660f * ms[i] / 1000.0f, harmonic(1));
    TEST_ASSERT_LESS_THAN_FLOAT(0.008f, harmonic(3));
    TEST_ASSERT_LESS_THAN_FLOAT(0.008f, harmonic(9));
  }
}

void test_svpwm_leg_carries_triplens() {
  // The min-max injection must actually be there: the single-leg (phase to
  // DC midpoint) spectrum contains triplen content that line-line lacks
  SynthSpec sp;
  sp.scheme = ModSchemeSvpwm;
  sp.cells = 3;
  sp.indexMilli = 1000;
  synthesize(sp, LEG0, waveA);
  analyze(waveA);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.02f, harmonic(3));
}

void test_dpwm_variants_line_line() {
  // Every DPWM variant preserves the line-line fundamental and triplen-free
  // property at the same index as SVPWM
  const uint8_t variants[4] = {DpwmMin, DpwmMax, DpwmGeneralised, Dpwm3};
  for (int i = 0; i < 4; i++) {
    SynthSpec sp;
    sp.scheme = ModSchemeDpwm;
    sp.cells = 3;
    sp.indexMilli = 1000;
    sp.dpwmVariant = variants[i];
    synthesize(sp, DIFF2, waveA);
    analyze(waveA);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.8660f, harmonic(1));
    TEST_ASSERT_LESS_THAN_FLOAT(0.015f, harmonic(3));
    TEST_ASSERT_LESS_THAN_FLOAT(0.015f, harmonic(9));
  }
}

void test_dpwm_clamp_dwell_one_third() {
  // The whole point of DPWM: every cycle pins exactly one leg to a rail, and
  // each leg dwells there 1/3 of the time. Duty-domain counting, no FFT.
  // Rail = duty >= 65534 or <= 1 (dutyFromScaled maps -32767 to 1, never 0).
  // Counts are deterministic (the pattern repeats every fundamental); the
  // per-leg bands cover 32-sample grid-rounding of the clamp arcs: a 120deg
  // arc holds 10-11 of 32 samples (Min/Max), 2x60deg arcs 10-12 (DPWM1),
  // 4x30deg arcs 8-12 (DPWM3) - the wider the arc split, the wider the band.
  struct { uint8_t variant; uint32_t lo, hi; } cases[4] = {
    {DpwmMin, 78, 97},
    {DpwmMax, 78, 97},
    {DpwmGeneralised, 74, 97}, // clamp angle 0 = DPWM1
    {Dpwm3, 64, 107},
  };
  buildUnitReferenceLut(lut, SpwmLutSize, RefWaveSine, false);
  CellPlan plans[MaxModulationCells] = {};
  for (int i = 0; i < 4; i++) {
    ModCycleConfig mc;
    mc.scheme = ModSchemeDpwm;
    mc.cells = 3;
    mc.dpwmVariant = cases[i].variant;
    mc.dpwmClampPhase = 0;
    uint32_t railedPerLeg[3] = {0, 0, 0};
    uint32_t railedTotal = 0;
    for (uint32_t c = 0; c < CYCLES; c++) {
      uint16_t duties[MaxModulationCells];
      modulationCycleDuties(lut, c * PHASE_INC, indexMilliToQ15(1000), mc, plans, duties);
      uint32_t railedThisCycle = 0;
      for (int k = 0; k < 3; k++) {
        if (duties[k] <= 1 || duties[k] >= 65534) {
          railedPerLeg[k]++;
          railedThisCycle++;
        }
      }
      TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1, railedThisCycle); // defining invariant
      railedTotal += railedThisCycle;
    }
    // One railed leg per cycle, except grid points where two legs TIE for the
    // selected extremum and both saturate: Min ties once per fundamental
    // (90deg: sin(-30) == sin(-150), exactly on the 11.25deg grid), Max once
    // (270deg), and Dpwm3 at BOTH 90 and 270deg - so the budget is two ties
    // per fundamental and Dpwm3 consumes it exactly (272 == 256 + 16).
    // Do not tighten: the ties are structural (bit-exact LUT symmetry).
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(CYCLES, railedTotal);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(CYCLES + 2 * FUNDS, railedTotal);
    for (int k = 0; k < 3; k++) {
      TEST_ASSERT_GREATER_OR_EQUAL_UINT32(cases[i].lo, railedPerLeg[k]);
      TEST_ASSERT_LESS_OR_EQUAL_UINT32(cases[i].hi, railedPerLeg[k]);
    }
  }
  // SVPWM by contrast never rails at m=1.0
  SynthSpec sp;
  sp.scheme = ModSchemeSvpwm;
  sp.cells = 3;
  sp.indexMilli = 1000;
  sp.logCell = 0;
  synthesize(sp, LEG0, waveA);
  for (uint32_t c = 0; c < CYCLES; c++) {
    TEST_ASSERT_TRUE(dutyLog[c] > 1 && dutyLog[c] < 65534);
  }
}

void test_svpwm3d_phase_to_neutral_is_pure_reference() {
  // Four-leg: the neutral leg carries the zero sequence, so leg0 - leg3 is
  // the pure reference: fundamental m/2, NO triplens (unlike leg0 alone)
  SynthSpec sp;
  sp.scheme = ModSchemeSvpwm3D;
  sp.cells = 4;
  sp.indexMilli = 1000;
  const float phaseToNeutral[4] = {1.0f, 0.0f, 0.0f, -1.0f};
  synthesize(sp, phaseToNeutral, waveA);
  analyze(waveA);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, harmonic(1));
  TEST_ASSERT_LESS_THAN_FLOAT(0.008f, harmonic(3));
  synthesize(sp, LEG0, waveA);
  analyze(waveA);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.02f, harmonic(3)); // injection present per leg
}

// ---------------------------------------------------------------------------
// Multi-cell: phase-shifted and level-shifted
// ---------------------------------------------------------------------------

void test_pspwm_carrier_doubling() {
  // Two cells on 180deg-shifted carriers: summed output cancels the fc group,
  // first switching group appears at 2fc (effective frequency doubling)
  SynthSpec sp;
  sp.scheme = ModSchemePhaseShifted;
  sp.indexMilli = 800;
  synthesize(sp, SUM2, waveA);
  analyze(waveA);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.8f, harmonic(1)); // sum fundamental = m
  // The fc line and even sidebands cancel between the 180deg-shifted cells;
  // the residual band peak is the regular-sampling image pair at (mf+/-1) f0
  // (same as unipolar - see test_unipolar_carrier_cancellation)
  TEST_ASSERT_LESS_THAN_FLOAT(0.005f, mag[FC]);
  TEST_ASSERT_LESS_THAN_FLOAT(0.005f, mag[FC - 2 * F0]);
  TEST_ASSERT_LESS_THAN_FLOAT(0.005f, mag[FC + 2 * F0]);
  const float fcPeak = bandPeak(FC - 3 * F0, FC + 3 * F0);
  const float fc2Peak = bandPeak(2 * FC - 3 * F0, 2 * FC + 3 * F0);
  TEST_ASSERT_LESS_THAN_FLOAT(0.05f, fcPeak);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.1f, fc2Peak);
  TEST_ASSERT_LESS_THAN_FLOAT(fc2Peak / 2.0f, fcPeak);
}

void test_lspwm_pd_three_level() {
  // Stacked bands: sum output is 3-level, fundamental = m (band slicing is
  // exact: cell duties sum to 2*ref), PD keeps carrier energy AT fc
  SynthSpec sp;
  sp.scheme = ModSchemeLevelShifted;
  sp.disposition = CarrierPd;
  sp.indexMilli = 900;
  synthesize(sp, SUM2, waveA);
  analyze(waveA);
  TEST_ASSERT_FLOAT_WITHIN(0.012f, 0.9f, harmonic(1));
  TEST_ASSERT_LESS_THAN_FLOAT(0.008f, harmonic(2));
  TEST_ASSERT_GREATER_THAN_FLOAT(0.05f, bandPeak(FC - 3 * F0, FC + 3 * F0));
}

void test_lspwm_pod_suppresses_fc_line() {
  // POD inverts the lower band's carrier: the exact-fc line cancels in the
  // sum, leaving only sidebands - discriminates POD from PD
  SynthSpec pd;
  pd.scheme = ModSchemeLevelShifted;
  pd.disposition = CarrierPd;
  pd.indexMilli = 900;
  synthesize(pd, SUM2, waveA);
  analyze(waveA);
  const float pdFc = mag[FC];

  SynthSpec pod = pd;
  pod.disposition = CarrierPod;
  synthesize(pod, SUM2, waveA);
  analyze(waveA);
  const float podFc = mag[FC];
  TEST_ASSERT_FLOAT_WITHIN(0.012f, 0.9f, harmonic(1)); // fundamental unchanged
  TEST_ASSERT_LESS_THAN_FLOAT(0.03f, podFc);
  TEST_ASSERT_LESS_THAN_FLOAT(pdFc / 3.0f, podFc);
}

void test_lspwm_apod_matches_pod_for_two_cells() {
  // With only two cells APOD degenerates to POD (one band inverted either
  // way): fundamental and carrier-region behavior must match closely
  SynthSpec pod;
  pod.scheme = ModSchemeLevelShifted;
  pod.disposition = CarrierPod;
  pod.indexMilli = 900;
  synthesize(pod, SUM2, waveA);
  analyze(waveA);
  const float podH1 = harmonic(1);
  const float podFcRegion = bandPeak(FC - 3 * F0, FC + 3 * F0);

  SynthSpec apod = pod;
  apod.disposition = CarrierApod;
  synthesize(apod, SUM2, waveA);
  analyze(waveA);
  TEST_ASSERT_FLOAT_WITHIN(0.15f * podH1, podH1, harmonic(1));
  TEST_ASSERT_FLOAT_WITHIN(0.5f * podFcRegion + 0.01f, podFcRegion,
                           bandPeak(FC - 3 * F0, FC + 3 * F0));
}

// ---------------------------------------------------------------------------
// Fundamental-frequency schemes: six-step, trapezoid, NLM
// ---------------------------------------------------------------------------

void test_six_step_square_series() {
  // m=1.0 square LUT drives the legs to the rails: ideal +/-1 square wave.
  // Fourier: Hn = (4/pi)/n for odd n, zero for even n.
  SynthSpec sp;
  sp.scheme = ModSchemeSpwmUnipolar;
  sp.refWave = RefWaveSquare;
  sp.indexMilli = 1000;
  synthesize(sp, DIFF2, waveA);
  analyze(waveA);
  const float fourOverPi = 1.27324f;
  TEST_ASSERT_FLOAT_WITHIN(0.01f, fourOverPi, harmonic(1));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, fourOverPi / 3.0f, harmonic(3));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, fourOverPi / 5.0f, harmonic(5));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, fourOverPi / 7.0f, harmonic(7));
  TEST_ASSERT_LESS_THAN_FLOAT(0.005f, harmonic(2));
  TEST_ASSERT_LESS_THAN_FLOAT(0.005f, harmonic(4));
}

void test_trapezoid_triplen_free() {
  // 60deg-ramp trapezoid: Hn ~ (12/pi^2) sin(n pi/3)/n^2 - triplens vanish
  // by construction, H5/H1 = 1/25, H7/H1 = 1/49
  SynthSpec sp;
  sp.scheme = ModSchemeSpwmUnipolar;
  sp.refWave = RefWaveTrapezoid;
  sp.indexMilli = 1000;
  synthesize(sp, DIFF2, waveA);
  analyze(waveA);
  const float h1 = harmonic(1);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.0529f, h1); // (12/pi^2) sin(pi/3)
  TEST_ASSERT_LESS_THAN_FLOAT(0.012f, harmonic(3));
  TEST_ASSERT_LESS_THAN_FLOAT(0.012f, harmonic(9));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, h1 / 25.0f, harmonic(5));
  TEST_ASSERT_FLOAT_WITHIN(0.008f, h1 / 49.0f, harmonic(7));
}

void test_nlm_staircase() {
  // Nearest-level 2-cell staircase at m=0.8: cells snap to rails (verified
  // directly on the duty stream - fundamental-frequency switching only),
  // and the carrier region is far quieter than PWM level-shifting.
  //
  // Expected fundamental: cell k turns on where the sampled reference
  // crosses its band midpoint; at m=0.8, mf=32 the rendered on-interval per
  // half-cycle is 101.25deg wide, H1 = (4/pi) sin(101.25deg/2) = 0.984.
  SynthSpec sp;
  sp.scheme = ModSchemeLevelShifted;
  sp.disposition = CarrierPd;
  sp.indexMilli = 800;
  sp.nearestLevel = true;
  sp.logCell = 0;
  synthesize(sp, SUM2, waveA);
  for (uint32_t c = 0; c < CYCLES; c++) {
    TEST_ASSERT_TRUE(dutyLog[c] == 0 || dutyLog[c] == 65535);
  }
  analyze(waveA);
  TEST_ASSERT_FLOAT_WITHIN(0.03f, 0.984f, harmonic(1));
  const float nlmFcRegion = bandPeak(FC - 3 * F0, FC + 3 * F0);
  TEST_ASSERT_LESS_THAN_FLOAT(0.06f, nlmFcRegion);

  // Same scheme with real PWM for contrast: carrier region >4x louder
  sp.nearestLevel = false;
  sp.logCell = -1;
  synthesize(sp, SUM2, waveA);
  analyze(waveA);
  TEST_ASSERT_GREATER_THAN_FLOAT(nlmFcRegion * 4.0f, bandPeak(FC - 3 * F0, FC + 3 * F0));
}

// ---------------------------------------------------------------------------
// Spread-spectrum carrier dithering: variable-length carrier cycles rendered
// in the clock-tick domain. Nominal period = 64 ticks, mf = 32, window = 64
// fundamentals = 131072 ticks. The matched DDS increments keep the
// fundamental exactly periodic in the tick domain, so it stays on-bin
// (bin 64) with a rectangular window; carrier energy is measured as a
// leakage-robust band peak around the nominal carrier (bin 2048).
// ---------------------------------------------------------------------------

constexpr uint32_t TICK_CLOCK = 1u << 21;    // "effective clock" Hz (arbitrary)
constexpr uint32_t TICK_CARRIER = TICK_CLOCK / 64;  // nominal 64-tick period
constexpr uint32_t TICK_MOD = TICK_CARRIER / 32;    // mf = 32
constexpr uint32_t TICK_N = 131072;          // 64 fundamentals of 2048 ticks
constexpr uint32_t TICK_F0 = 64;
constexpr uint32_t TICK_FC = 2048;

static float tickWave[TICK_N];
static float tickRe[TICK_N];
static float tickIm[TICK_N];
static float tickMag[TICK_N / 2];

// Bipolar H-bridge at index m through a dithered carrier: the strongest
// single carrier line, so spreading is easiest to quantify
static void synthesizeDithered(uint8_t ditherMode, uint8_t ditherPercent, float *out) {
  buildUnitReferenceLut(lut, SpwmLutSize, RefWaveSine, false);
  uint16_t periods[DitherTableSize];
  uint32_t increments[DitherTableSize];
  buildDitherTables(periods, increments, TICK_CLOCK, TICK_CARRIER, TICK_MOD, ditherPercent);

  uint32_t phase = 0;
  uint16_t lfsr = 0xACE1;
  uint32_t sweep = 0;
  uint32_t tick = 0;
  while (tick < TICK_N) {
    uint32_t entry = 0;
    if (ditherMode == DitherRandom) {
      lfsr = nextLfsr16(lfsr);
      entry = lfsr & (DitherTableSize - 1);
    } else if (ditherMode == DitherSweep) {
      entry = triangleIndex(sweep++, DitherTableSize);
    }
    const uint32_t period = periods[entry];
    // Bipolar pair: legA duty d, legB inverted polarity; diff = 2*legA - 1.
    // Boxcar-rendered per tick (see renderCycle) to avoid width quantization.
    const int32_t s = refFromPhase(lut, phase);
    const uint16_t d = refToDuty(s, indexMilliToQ15(800), 0);
    const double w = static_cast<double>(d) / 65535.0 * static_cast<double>(period);
    const double lo = (static_cast<double>(period) - w) / 2.0;
    const double hi = (static_cast<double>(period) + w) / 2.0;
    for (uint32_t t = 0; t < period && tick < TICK_N; t++, tick++) {
      double a = hi < t + 1.0 ? hi : t + 1.0;
      double b = lo > t ? lo : t;
      double on = a - b;
      if (on < 0.0) on = 0.0;
      out[tick] = static_cast<float>(2.0 * on - 1.0);
    }
    phase += ditherMode == DitherOff ? (1UL << 27) : increments[entry];
  }
}

static void analyzeTicks(const float *w) {
  double mean = 0.0;
  for (uint32_t i = 0; i < TICK_N; i++) {
    mean += w[i];
  }
  mean /= TICK_N;
  for (uint32_t i = 0; i < TICK_N; i++) {
    tickRe[i] = static_cast<float>(w[i] - mean);
    tickIm[i] = 0.0f;
  }
  fftRadix2(tickRe, tickIm, TICK_N);
  for (uint32_t k = 0; k < TICK_N / 2; k++) {
    tickMag[k] =
      2.0f * sqrtf(tickRe[k] * tickRe[k] + tickIm[k] * tickIm[k]) / static_cast<float>(TICK_N);
  }
}

static float tickBandPeak(uint32_t lo, uint32_t hi) {
  float peak = 0.0f;
  for (uint32_t i = lo; i <= hi && i < TICK_N / 2; i++) {
    if (tickMag[i] > peak) {
      peak = tickMag[i];
    }
  }
  return peak;
}

static double tickBandEnergy(uint32_t lo, uint32_t hi) {
  double e = 0.0;
  for (uint32_t i = lo; i <= hi && i < TICK_N / 2; i++) {
    e += static_cast<double>(tickMag[i]) * tickMag[i];
  }
  return e;
}

void test_dither_spreads_carrier() {
  synthesizeDithered(DitherOff, 0, tickWave);
  analyzeTicks(tickWave);
  const float basePeak = tickBandPeak(TICK_FC - 512, TICK_FC + 512);
  const double baseEnergy = tickBandEnergy(TICK_FC - 512, TICK_FC + 512);
  const float baseH1 = tickMag[TICK_F0];
  TEST_ASSERT_GREATER_THAN_FLOAT(0.4f, basePeak); // bipolar carrier line is strong

  const uint8_t modes[2] = {DitherRandom, DitherSweep};
  for (int i = 0; i < 2; i++) {
    synthesizeDithered(modes[i], 20, tickWave);
    analyzeTicks(tickWave);
    // Fundamental unaffected (matched increments keep f0 exact and on-bin)
    TEST_ASSERT_FLOAT_WITHIN(0.015f, baseH1, tickMag[TICK_F0]);
    TEST_ASSERT_LESS_THAN_FLOAT(0.05f * baseH1, tickMag[TICK_F0 - 2]); // no smear
    TEST_ASSERT_LESS_THAN_FLOAT(0.05f * baseH1, tickMag[TICK_F0 + 2]);
    // Peak spectral line at least halved (>=6dB spread)
    const float peak = tickBandPeak(TICK_FC - 512, TICK_FC + 512);
    TEST_ASSERT_LESS_THAN_FLOAT(basePeak * 0.5f, peak);
    // Energy spread, not removed: same band keeps a comparable total
    const double energy = tickBandEnergy(TICK_FC - 512, TICK_FC + 512);
    TEST_ASSERT_TRUE(energy > baseEnergy * 0.4 && energy < baseEnergy * 1.6);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_unipolar_fundamental_linearity);
  RUN_TEST(test_unipolar_carrier_cancellation);
  RUN_TEST(test_bipolar_carrier_present);
  RUN_TEST(test_thipwm_third_harmonic_in_hbridge);
  RUN_TEST(test_thipwm_extended_linear_range);
  RUN_TEST(test_thipwm_three_phase_line_line_cancels_third);
  RUN_TEST(test_svpwm_line_line_triplen_free_and_linear);
  RUN_TEST(test_svpwm_leg_carries_triplens);
  RUN_TEST(test_dpwm_variants_line_line);
  RUN_TEST(test_dpwm_clamp_dwell_one_third);
  RUN_TEST(test_svpwm3d_phase_to_neutral_is_pure_reference);
  RUN_TEST(test_pspwm_carrier_doubling);
  RUN_TEST(test_lspwm_pd_three_level);
  RUN_TEST(test_lspwm_pod_suppresses_fc_line);
  RUN_TEST(test_lspwm_apod_matches_pod_for_two_cells);
  RUN_TEST(test_six_step_square_series);
  RUN_TEST(test_trapezoid_triplen_free);
  RUN_TEST(test_nlm_staircase);
  RUN_TEST(test_dither_spreads_carrier);
  return UNITY_END();
}
