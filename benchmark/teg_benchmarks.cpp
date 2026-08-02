#include <benchmark/benchmark.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "modulation.h"
#include "pll_math.h"
#include "spectrum_math.h"
#include "waveform_parse.h"

namespace {

std::array<int16_t, SpwmLutSize> makeLut() {
  std::array<int16_t, SpwmLutSize> lut{};
  buildUnitReferenceLut(lut.data(), lut.size(), RefWaveSine, false);
  return lut;
}

const std::array<int16_t, SpwmLutSize> kLut = makeLut();

void modulationCycle(benchmark::State &state) {
  ModCycleConfig config;
  config.scheme = static_cast<uint8_t>(state.range(0));
  config.cells = config.scheme == ModSchemeSvpwm3D ? 4 :
                 (config.scheme == ModSchemeSvpwm || config.scheme == ModSchemeDpwm ? 3 : 2);
  config.dpwmVariant = DpwmGeneralised;
  config.dtCompQ15 = 24;
  std::array<CellPlan, MaxModulationCells> plans{};
  std::array<uint16_t, MaxModulationCells> duties{};
  for (uint8_t cell = 0; cell < config.cells; ++cell) {
    plans[cell] = modulationCellPlan(config.scheme, CarrierPd, cell, config.cells);
  }
  uint32_t phase = 0;
  const uint32_t phaseIncrement = spwmPhaseIncrement(20000, 50);
  for (auto _ : state) {
    modulationCycleDuties(kLut.data(), phase, indexMilliToQ15(900), config,
                          plans.data(), duties.data());
    phase += phaseIncrement;
    benchmark::DoNotOptimize(duties);
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK(modulationCycle)->ArgName("scheme")
  ->Arg(ModSchemeSpwmUnipolar)->Arg(ModSchemeSvpwm)->Arg(ModSchemeDpwm)->Arg(ModSchemeSvpwm3D);

void fft2048(benchmark::State &state) {
  constexpr uint32_t n = 2048;
  std::array<int16_t, n> samples{};
  std::array<float, n> real{};
  std::array<float, n> imag{};
  for (uint32_t i = 0; i < n; ++i) {
    samples[i] = static_cast<int16_t>(12000.0 * std::sin(6.283185307179586 * 13.0 * i / n));
  }
  for (auto _ : state) {
    state.PauseTiming();
    prepareSpectrumInput(samples.data(), n, real.data(), imag.data());
    state.ResumeTiming();
    fftRadix2(real.data(), imag.data(), n);
    benchmark::DoNotOptimize(real.data());
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations() * n);
}

BENCHMARK(fft2048);

void pllSamples(benchmark::State &state) {
  PllParams params;
  pllParamsInit(params, 20000.0f, 50.0f, 20.0f, 45.0f, 55.0f, 1650, 50);
  PllState pll;
  pllReset(pll, 50.0f);
  std::array<float, 400> angles{};
  std::array<uint16_t, 400> raw{};
  for (uint32_t sample = 0; sample < angles.size(); ++sample) {
    angles[sample] = 6.28318530717958648f * static_cast<float>(sample) / 400.0f;
    raw[sample] = static_cast<uint16_t>(2048.0f + 1200.0f * std::sin(angles[sample]));
  }
  uint32_t sample = 0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(pllStep(pll, params, raw[sample], angles[sample]));
    if (++sample == raw.size()) {
      sample = 0;
    }
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK(pllSamples);

void waveformText2048(benchmark::State &state) {
  std::string input = "type=reference\n";
  input.reserve(18000);
  for (uint32_t i = 0; i < 2048; ++i) {
    input += std::to_string(std::sin(6.283185307179586 * i / 2048.0));
    input.push_back('\n');
  }
  std::vector<int16_t> samples(2048);
  std::array<int16_t, MaxWaveSegments> levels{};
  std::array<uint32_t, MaxWaveSegments> durations{};
  uint8_t type = WaveTypeNone;
  for (auto _ : state) {
    benchmark::DoNotOptimize(parseWaveform(input.c_str(), samples.data(), samples.size(),
                                           levels.data(), durations.data(), levels.size(), &type));
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations() * 2048);
}

BENCHMARK(waveformText2048);

}  // namespace

BENCHMARK_MAIN();
