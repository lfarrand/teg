#ifndef WAVEFORM_PARSE_H
#define WAVEFORM_PARSE_H

// Parser for the "teg-wave v1" custom waveform formats: no hardware
// dependencies, unit-tested natively.
//
// TEXT FORMAT - plain text, '#' starts a comment, blank lines skipped. The
// first content line selects the type:
//
//   type=reference
//     One normalized sample per line, -1.0 .. 1.0 (values outside are
//     clamped), 2 .. 2,097,152 points. Playback:
//       - period mode (default): the waveform is one fundamental period at
//         the configured modulation frequency (DDS; rendered through a
//         2048-point table, so extremely long files are downsampled here)
//       - sample-step mode: exactly one stored sample per carrier cycle at
//         full resolution, repeating when the end is reached - duration is
//         count/carrier (e.g. 2M samples at 20kHz = ~105s per repeat)
//     A non-zero mean produces DC in the output; permitted deliberately.
//
//   type=sequence
//     One "level, duration_us" pair per line: level -1.0 .. 1.0 (clamped),
//     duration in microseconds. Up to 64 segments, looped. Durations
//     quantize to the carrier period.
//
// BINARY FORMAT - for bulk reference uploads (and the on-SD representation):
//   12-byte header: magic "TEGW", u8 version(=1), u8 type, u16 reserved(=0),
//   u32 count (little-endian), then the payload:
//     reference: count x int16 Q15 samples (little-endian)
//     sequence:  count x { int16 levelQ15, u16 reserved, u32 micros }
//
// Parsing is incremental (WaveParser + waveParseLine) so multi-megabyte
// uploads stream straight into their destination buffer without ever holding
// the whole text in RAM. parseWaveform() wraps it for in-memory text.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

constexpr uint32_t MaxWaveSamples = 2UL * 1024UL * 1024UL; // PSRAM-resident limit (4MB as int16)
constexpr uint32_t MaxStreamSamples = 1UL << 29;           // SD-streamed limit (1GB as int16)
constexpr uint32_t MaxWaveSegments = 64;
constexpr uint32_t MaxWaveTextLineLength = 127;

enum : uint8_t {
  WaveTypeNone = 0,
  WaveTypeReference = 1,
  WaveTypeSequence = 2,
};

enum : int32_t {
  WaveErrNoType = -1,
  WaveErrBadValue = -2,
  WaveErrTooMany = -3,
  WaveErrTooFew = -4,
  WaveErrBadDuration = -5,
  WaveErrBadBinary = -6,
  WaveErrLineTooLong = -7,
};

inline float waveClampLevel(float v) {
  if (v > 1.0f) return 1.0f;
  if (v < -1.0f) return -1.0f;
  return v;
}

inline int16_t waveLevelToQ15(float v) {
  return static_cast<int16_t>(waveClampLevel(v) * 32767.0f);
}

struct WaveParser {
  uint8_t type = WaveTypeNone;
  uint32_t count = 0;
  int16_t *samples = nullptr;      // reference destination (Q15)
  uint32_t maxSamples = 0;
  int16_t *segLevelsQ15 = nullptr; // sequence destinations
  uint32_t *segMicros = nullptr;
  uint32_t maxSegments = 0;
};

// Feed one line (start..end exclusive, no terminator needed). Returns 0 on
// success (including blank/comment lines) or a WaveErr* code.
inline int32_t waveParseLine(WaveParser &p, const char *line, const char *end) {
  if (line == nullptr || end == nullptr || end < line) {
    return WaveErrBadValue;
  }

  const char *hash = static_cast<const char *>(
      memchr(line, '#', static_cast<size_t>(end - line)));
  if (hash != nullptr) {
    end = hash;
  }
  while (line < end && (*line == ' ' || *line == '\t' || *line == '\r')) {
    line++;
  }
  while (end > line && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
    end--;
  }
  if (line >= end) {
    return 0; // blank
  }

  const size_t length = static_cast<size_t>(end - line);
  if (length > MaxWaveTextLineLength) {
    return WaveErrLineTooLong;
  }

  // strtof() is a NUL-terminated API, whereas this parser's contract
  // is a bounded [line,end) span. Parsing the caller's storage directly lets a
  // numeric prefix consume bytes from the next line (or stale bytes in the
  // streaming buffer), after which end-num_end underflows into an unbounded
  // memchr(). Keep all conversion reads inside a small, fixed stack buffer.
  char bounded[MaxWaveTextLineLength + 1];
  memcpy(bounded, line, length);
  bounded[length] = '\0';
  char *const boundedEnd = bounded + length;

  if (p.type == WaveTypeNone) {
    if (length == 14 && memcmp(bounded, "type=reference", 14) == 0) {
      p.type = WaveTypeReference;
      return 0;
    }
    if (length == 13 && memcmp(bounded, "type=sequence", 13) == 0) {
      p.type = WaveTypeSequence;
      return 0;
    }
    return WaveErrNoType;
  }

  if (p.type == WaveTypeReference) {
    char *num_end;
    const float v = strtof(bounded, &num_end);
    while (num_end < boundedEnd && (*num_end == ' ' || *num_end == '\t')) {
      num_end++;
    }
    if (num_end == bounded || num_end != boundedEnd || v != v ||
        v > 1000.0f || v < -1000.0f) {
      return WaveErrBadValue;
    }
    if (p.count >= p.maxSamples) {
      return WaveErrTooMany;
    }
    if (p.samples == nullptr) {
      return WaveErrBadValue;
    }
    p.samples[p.count++] = waveLevelToQ15(v);
    return 0;
  }

  // sequence: "level, duration_us"
  char *const comma = static_cast<char *>(memchr(bounded, ',', length));
  if (comma == nullptr) {
    return WaveErrBadDuration;
  }
  *comma = '\0';

  char *num_end;
  const float level = strtof(bounded, &num_end);
  while (num_end < comma && (*num_end == ' ' || *num_end == '\t')) {
    num_end++;
  }
  if (num_end == bounded || num_end != comma || level != level ||
      level > 1000.0f || level < -1000.0f) {
    return WaveErrBadValue;
  }

  const char *duration = comma + 1;
  while (duration < boundedEnd && (*duration == ' ' || *duration == '\t')) {
    duration++;
  }
  if (duration == boundedEnd || *duration == '-') {
    return WaveErrBadDuration;
  }

  uint32_t us = 0;
  bool haveDigit = false;
  while (duration < boundedEnd && *duration >= '0' && *duration <= '9') {
    const uint32_t digit = static_cast<uint32_t>(*duration - '0');
    if (us > (UINT32_MAX - digit) / 10U) {
      return WaveErrBadDuration;
    }
    us = us * 10U + digit;
    haveDigit = true;
    duration++;
  }
  while (duration < boundedEnd && (*duration == ' ' || *duration == '\t')) {
    duration++;
  }
  if (!haveDigit || duration != boundedEnd || us == 0) {
    return WaveErrBadDuration;
  }

  if (p.count >= p.maxSegments) {
    return WaveErrTooMany;
  }
  if (p.segLevelsQ15 == nullptr || p.segMicros == nullptr) {
    return WaveErrBadValue;
  }
  p.segLevelsQ15[p.count] = waveLevelToQ15(level);
  p.segMicros[p.count] = us;
  p.count++;
  return 0;
}

// Final validation once all lines are fed. Returns count or a WaveErr* code.
inline int32_t waveParseFinish(const WaveParser &p) {
  if (p.type == WaveTypeNone) {
    return WaveErrNoType;
  }
  if ((p.type == WaveTypeReference && p.count < 2) ||
      (p.type == WaveTypeSequence && p.count < 1)) {
    return WaveErrTooFew;
  }
  return static_cast<int32_t>(p.count);
}

// Whole-text convenience wrapper (used by the unit tests and small uploads)
inline int32_t parseWaveform(const char *text, int16_t *samples, uint32_t maxSamples,
                             int16_t *segLevelsQ15, uint32_t *segMicros, uint32_t maxSegments,
                             uint8_t *outType) {
  WaveParser p;
  p.samples = samples;
  p.maxSamples = maxSamples;
  p.segLevelsQ15 = segLevelsQ15;
  p.segMicros = segMicros;
  p.maxSegments = maxSegments;

  const char *cursor = text;
  while (*cursor != '\0') {
    const char *nl = strchr(cursor, '\n');
    const char *end = nl != nullptr ? nl : cursor + strlen(cursor);
    const int32_t err = waveParseLine(p, cursor, end);
    if (err != 0) {
      *outType = p.type;
      return err;
    }
    cursor = nl != nullptr ? nl + 1 : end;
  }
  *outType = p.type;
  return waveParseFinish(p);
}

// ---------------------------------------------------------------------------
// Binary header (also the on-SD representation)
// ---------------------------------------------------------------------------

constexpr uint32_t WaveBinaryHeaderSize = 12;

inline void waveBinaryHeaderWrite(uint8_t *out, uint8_t type, uint32_t count) {
  out[0] = 'T'; out[1] = 'E'; out[2] = 'G'; out[3] = 'W';
  out[4] = 1; // version
  out[5] = type;
  out[6] = 0; out[7] = 0;
  out[8] = static_cast<uint8_t>(count);
  out[9] = static_cast<uint8_t>(count >> 8);
  out[10] = static_cast<uint8_t>(count >> 16);
  out[11] = static_cast<uint8_t>(count >> 24);
}

// Returns count, or WaveErrBadBinary. Validates magic/version/type/limits.
inline int32_t waveBinaryHeaderRead(const uint8_t *in, uint8_t *outType) {
  if (in[0] != 'T' || in[1] != 'E' || in[2] != 'G' || in[3] != 'W' || in[4] != 1) {
    return WaveErrBadBinary;
  }
  const uint8_t type = in[5];
  const uint32_t count = static_cast<uint32_t>(in[8]) | (static_cast<uint32_t>(in[9]) << 8) |
                         (static_cast<uint32_t>(in[10]) << 16) | (static_cast<uint32_t>(in[11]) << 24);
  if (type == WaveTypeReference) {
    if (count < 2 || count > MaxStreamSamples) {
      return WaveErrBadBinary;
    }
  } else if (type == WaveTypeSequence) {
    if (count < 1 || count > MaxWaveSegments) {
      return WaveErrBadBinary;
    }
  } else {
    return WaveErrBadBinary;
  }
  *outType = type;
  return static_cast<int32_t>(count);
}

// ---------------------------------------------------------------------------
// Playback helpers
// ---------------------------------------------------------------------------

// Periodic linear resample of n Q15 samples onto the (power-of-two) DDS table
inline void resampleReference(const int16_t *points, uint32_t n, int16_t *lut, uint32_t lutSize) {
  for (uint32_t i = 0; i < lutSize; i++) {
    const uint64_t scaled = (static_cast<uint64_t>(i) * n << 16) / lutSize; // 16-bit fraction
    const uint32_t idx = static_cast<uint32_t>(scaled >> 16);
    const int32_t frac = static_cast<int32_t>(scaled & 0xFFFF);
    const int32_t a = points[idx % n];
    const int32_t b = points[(idx + 1) % n];
    lut[i] = static_cast<int16_t>(a + (((b - a) * frac) >> 16));
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
