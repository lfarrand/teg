#ifndef SPECTRUM_WIRE_H
#define SPECTRUM_WIRE_H

// Little-endian TEGS spectrum body: 32-byte header then uint16 bins.
// Field-by-field memcpy (same pattern as TEGC), not a packed struct.
//   0  "TEGS"   4 magic
//   4  u8       version (1)
//   5  u8       flags: bit0 available
//   6  u16 LE   binCount
//   8  u32 LE   sampleHz
//  12  u32 LE   points
//  16  u32 LE   computeMicros
//  20  f32 LE   binHz
//  24  f32 LE   fundamentalHz
//  28  f32 LE   thdPercent
//  32+ u16 LE   bins (mag/fund * SpectrumWireScale)

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

constexpr uint8_t SpectrumWireVersion = 1;
constexpr size_t SpectrumWireHeaderSize = 32;
constexpr uint16_t SpectrumJsonMaxBins = 128;
constexpr float SpectrumWireScale = 10000.0f;
constexpr size_t SpectrumWireMaxBody =
    SpectrumWireHeaderSize + static_cast<size_t>(SpectrumJsonMaxBins) * sizeof(uint16_t);

struct SpectrumWireFields {
  bool available;
  uint32_t sampleHz;
  uint32_t points;
  uint32_t computeMicros;
  float binHz;
  float fundamentalHz;
  float thdPercent;
};

inline uint16_t spectrumWireQuantize(float mag, float fund) {
  if (fund <= 0.0f || mag <= 0.0f) {
    return 0;
  }
  return static_cast<uint16_t>(roundf((mag / fund) * SpectrumWireScale));
}

inline uint16_t spectrumWireBinCount(uint32_t points, bool available) {
  if (!available) {
    return 0;
  }
  const uint32_t half = points / 2;
  return static_cast<uint16_t>(half < SpectrumJsonMaxBins ? half : SpectrumJsonMaxBins);
}

inline size_t spectrumWirePayloadSize(uint16_t binCount) {
  return SpectrumWireHeaderSize + static_cast<size_t>(binCount) * sizeof(uint16_t);
}

inline size_t spectrumWirePack(uint8_t *out, size_t outCap, const SpectrumWireFields &f,
                               const float *mag, float fund) {
  const uint16_t binCount = spectrumWireBinCount(f.points, f.available);
  const size_t need = spectrumWirePayloadSize(binCount);
  if (out == nullptr || outCap < need) {
    return 0;
  }
  if (binCount > 0 && mag == nullptr) {
    return 0;
  }

  memcpy(out, "TEGS", 4);
  out[4] = SpectrumWireVersion;
  out[5] = f.available ? 1 : 0;
  memcpy(out + 6, &binCount, 2);
  memcpy(out + 8, &f.sampleHz, 4);
  memcpy(out + 12, &f.points, 4);
  memcpy(out + 16, &f.computeMicros, 4);
  memcpy(out + 20, &f.binHz, 4);
  memcpy(out + 24, &f.fundamentalHz, 4);
  memcpy(out + 28, &f.thdPercent, 4);

  for (uint16_t i = 0; i < binCount; ++i) {
    const uint16_t q = spectrumWireQuantize(mag[i], fund);
    memcpy(out + SpectrumWireHeaderSize + static_cast<size_t>(i) * 2, &q, 2);
  }
  return need;
}

inline bool spectrumWireUnpack(const uint8_t *in, size_t inLen, SpectrumWireFields &f,
                               uint16_t *bins, uint16_t binsCap, uint16_t *binCountOut) {
  if (in == nullptr || inLen < SpectrumWireHeaderSize) {
    return false;
  }
  if (memcmp(in, "TEGS", 4) != 0) {
    return false;
  }
  if (in[4] != SpectrumWireVersion) {
    return false;
  }

  uint16_t binCount = 0;
  memcpy(&binCount, in + 6, 2);
  const size_t need = spectrumWirePayloadSize(binCount);
  if (inLen < need) {
    return false;
  }

  f.available = (in[5] & 1) != 0;
  memcpy(&f.sampleHz, in + 8, 4);
  memcpy(&f.points, in + 12, 4);
  memcpy(&f.computeMicros, in + 16, 4);
  memcpy(&f.binHz, in + 20, 4);
  memcpy(&f.fundamentalHz, in + 24, 4);
  memcpy(&f.thdPercent, in + 28, 4);

  if (binCountOut != nullptr) {
    *binCountOut = binCount;
  }
  if (bins != nullptr) {
    const uint16_t n = binCount < binsCap ? binCount : binsCap;
    for (uint16_t i = 0; i < n; ++i) {
      memcpy(&bins[i], in + SpectrumWireHeaderSize + static_cast<size_t>(i) * 2, 2);
    }
  }
  return true;
}

#endif
