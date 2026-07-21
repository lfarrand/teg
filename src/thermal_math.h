#ifndef THERMAL_MATH_H
#define THERMAL_MATH_H

// Thermal derating curve: no hardware dependencies, unit-tested natively.

#include <stdint.h>

// Linear derate factor in thousandths: 1000 (no derating) at or below startC,
// 0 (fully off) at or above endC, linear in between. A degenerate window
// (endC <= startC) behaves as a hard cutoff at startC.
inline uint16_t thermalDerateMilli(float temperatureC, float startC, float endC) {
  if (temperatureC <= startC) {
    return 1000;
  }
  if (endC <= startC || temperatureC >= endC) {
    return 0;
  }
  return static_cast<uint16_t>(1000.0f * (endC - temperatureC) / (endC - startC));
}

#endif
