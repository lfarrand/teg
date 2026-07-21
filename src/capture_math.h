#ifndef CAPTURE_MATH_H
#define CAPTURE_MATH_H

// Ring-buffer statistics for the waveform capture subsystem: no hardware
// dependencies, unit-tested natively.

#include <stdint.h>

// Mean of the most recent n samples, counting backwards from head (exclusive).
// ringSize must be a power of two only for the caller's write path; here any
// size works.
inline uint32_t ringMean(const uint16_t *ring, uint32_t ringSize, uint32_t head, uint32_t n) {
  if (n == 0 || ringSize == 0) {
    return 0;
  }
  if (n > ringSize) {
    n = ringSize;
  }
  uint64_t sum = 0;
  uint32_t idx = head;
  for (uint32_t i = 0; i < n; i++) {
    idx = (idx == 0 ? ringSize : idx) - 1;
    sum += ring[idx];
  }
  return static_cast<uint32_t>(sum / n);
}

// Min/max-envelope decimation of the most recent `count` samples (ending at
// head, exclusive) into `bins` pairs. Preserves peaks that plain averaging
// would smear away - the right reduction for an oscilloscope-style display.
inline void ringDecimate(const uint16_t *ring, uint32_t ringSize, uint32_t head, uint32_t count,
                         uint32_t bins, uint16_t *outMin, uint16_t *outMax) {
  if (bins == 0 || count == 0 || ringSize == 0) {
    return;
  }
  if (count > ringSize) {
    count = ringSize;
  }
  const uint32_t start = (head + ringSize - (count % ringSize)) % ringSize;
  for (uint32_t b = 0; b < bins; b++) {
    const uint32_t from = static_cast<uint32_t>((static_cast<uint64_t>(b) * count) / bins);
    uint32_t to = static_cast<uint32_t>((static_cast<uint64_t>(b + 1) * count) / bins);
    if (to <= from) {
      to = from + 1;
    }
    uint16_t mn = 0xFFFF, mx = 0;
    for (uint32_t i = from; i < to; i++) {
      const uint16_t v = ring[(start + i) % ringSize];
      if (v < mn) mn = v;
      if (v > mx) mx = v;
    }
    outMin[b] = mn;
    outMax[b] = mx;
  }
}

#endif
