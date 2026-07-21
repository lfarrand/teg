#ifndef WAVEFORM_PARSE_H
#define WAVEFORM_PARSE_H

// Parser for the "teg-wave v1" custom waveform file format: no hardware
// dependencies, unit-tested natively.
//
// The file is plain text. '#' starts a comment (rest of line ignored); blank
// lines are skipped. The first directive line selects the type:
//
//   type=reference
//     One normalized sample per line, -1.0 .. 1.0 (values outside are
//     clamped). 2..4096 points; the firmware resamples them to its internal
//     2048-point table with periodic linear interpolation. The waveform is
//     played by the DDS at the configured modulation frequency, scaled by the
//     modulation index, and fed through the selected scheme's cell mapping -
//     exactly like the built-in sine. A non-zero mean produces DC in the
//     output; that is permitted deliberately.
//
//   type=sequence
//     One "level, duration_us" pair per line: level -1.0 .. 1.0 (clamped),
//     duration in microseconds (1 .. 4,294,967,295). Up to 64 segments,
//     played in order and looped. Durations are quantized to the carrier
//     period (e.g. 50us steps at a 20kHz carrier). Levels pass through the
//     same index scaling and cell mapping as any reference, so +1/0/-1
//     on/off patterns drive complementary pairs with dead-time intact.
//
// Example (a simple burst):
//   # teg-wave v1
//   type=sequence
//   1.0, 1500
//   0.0, 500
//   -1.0, 300

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

constexpr uint32_t MaxWavePoints = 4096;
constexpr uint32_t MaxWaveSegments = 64;

enum : uint8_t {
  WaveTypeNone = 0,
  WaveTypeReference = 1,
  WaveTypeSequence = 2,
};

// Parse errors (negative return values)
enum : int32_t {
  WaveErrNoType = -1,     // missing/unknown type= directive
  WaveErrBadValue = -2,   // unparseable number or level out of a sane range
  WaveErrTooMany = -3,    // more points/segments than the limits
  WaveErrTooFew = -4,     // fewer than 2 reference points / 1 segment
  WaveErrBadDuration = -5 // zero or unparseable duration
};

inline float waveClampLevel(float v) {
  if (v > 1.0f) return 1.0f;
  if (v < -1.0f) return -1.0f;
  return v;
}

// Parses `text`. On success returns the number of points/segments and sets
// *outType; reference points land in refPoints, sequence segments in
// segLevelsQ15 (level * 32767) + segMicros.
inline int32_t parseWaveform(const char *text, float *refPoints, uint32_t maxPoints,
                             int16_t *segLevelsQ15, uint32_t *segMicros, uint32_t maxSegments,
                             uint8_t *outType) {
  *outType = WaveTypeNone;
  uint32_t count = 0;

  const char *p = text;
  while (*p != '\0') {
    // Isolate one line
    const char *lineEnd = strchr(p, '\n');
    const char *next = lineEnd != nullptr ? lineEnd + 1 : p + strlen(p);

    // Strip comment and leading whitespace
    const char *hash = static_cast<const char *>(memchr(p, '#', (lineEnd ? lineEnd : next) - p));
    const char *end = hash != nullptr ? hash : (lineEnd != nullptr ? lineEnd : next);
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) {
      p++;
    }
    const bool blank = p >= end;

    if (!blank) {
      if (*outType == WaveTypeNone) {
        if (strncmp(p, "type=reference", 14) == 0) {
          *outType = WaveTypeReference;
        } else if (strncmp(p, "type=sequence", 13) == 0) {
          *outType = WaveTypeSequence;
        } else {
          return WaveErrNoType; // first content line must declare the type
        }
      } else if (*outType == WaveTypeReference) {
        char *num_end;
        const float v = strtof(p, &num_end);
        if (num_end == p || v != v || v > 1000.0f || v < -1000.0f) {
          return WaveErrBadValue;
        }
        if (count >= maxPoints) {
          return WaveErrTooMany;
        }
        refPoints[count++] = waveClampLevel(v);
      } else { // sequence: "level, duration_us"
        char *num_end;
        const float level = strtof(p, &num_end);
        if (num_end == p || level != level || level > 1000.0f || level < -1000.0f) {
          return WaveErrBadValue;
        }
        const char *comma = strchr(num_end, ',');
        if (comma == nullptr || comma >= end) {
          return WaveErrBadDuration;
        }
        char *dur_end;
        const unsigned long us = strtoul(comma + 1, &dur_end, 10);
        if (dur_end == comma + 1 || us == 0) {
          return WaveErrBadDuration;
        }
        if (count >= maxSegments) {
          return WaveErrTooMany;
        }
        segLevelsQ15[count] = static_cast<int16_t>(waveClampLevel(level) * 32767.0f);
        segMicros[count] = static_cast<uint32_t>(us);
        count++;
      }
    }
    p = next;
  }

  if (*outType == WaveTypeNone) {
    return WaveErrNoType;
  }
  if ((*outType == WaveTypeReference && count < 2) || (*outType == WaveTypeSequence && count < 1)) {
    return WaveErrTooFew;
  }
  return static_cast<int32_t>(count);
}

// Resample n user points onto the internal LUT with periodic linear
// interpolation (the waveform is one cycle; the last point wraps to the first)
inline void resampleReference(const float *points, uint32_t n, int16_t *lut, uint32_t lutSize) {
  for (uint32_t i = 0; i < lutSize; i++) {
    const float pos = (static_cast<float>(i) * n) / static_cast<float>(lutSize);
    const uint32_t idx = static_cast<uint32_t>(pos);
    const float frac = pos - static_cast<float>(idx);
    const float a = points[idx % n];
    const float b = points[(idx + 1) % n];
    const float v = a + (b - a) * frac;
    lut[i] = static_cast<int16_t>(waveClampLevel(v) * 32767.0f);
  }
}

// Segment duration in carrier cycles (rounded, never zero)
inline uint32_t microsToCycles(uint32_t micros, uint32_t carrierHz) {
  const uint64_t cycles = (static_cast<uint64_t>(micros) * carrierHz + 500000ULL) / 1000000ULL;
  if (cycles == 0) {
    return 1;
  }
  return cycles > 0xFFFFFFFFULL ? 0xFFFFFFFFUL : static_cast<uint32_t>(cycles);
}

#endif
