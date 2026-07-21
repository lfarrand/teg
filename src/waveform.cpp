#include "waveform.h"
#include "waveform_parse.h"
#include "utils.h"
#include <SdFat.h>

extern SdFs sd;
extern bool sdAvailable;

static const char *const WaveformFile = "/waveform.bin";

// Full-resolution reference store: 4MB of the 8MB PSRAM (the capture ring
// has another 2MB). Sequential ISR reads are D-cache friendly.
EXTMEM static int16_t waveSamples[MaxWaveSamples];

static int16_t segLevelsQ15[MaxWaveSegments];
static uint32_t segMicros[MaxWaveSegments];
static uint8_t storedType = WaveTypeNone;
static uint32_t storedCount = 0;

static const char *errorText(int32_t code) {
  switch (code) {
    case WaveErrNoType: return "first line must be type=reference or type=sequence";
    case WaveErrBadValue: return "unparseable level value";
    case WaveErrTooMany: return "too many points/segments";
    case WaveErrTooFew: return "need at least 2 reference points or 1 segment";
    case WaveErrBadDuration: return "bad or zero duration_us";
    case WaveErrBadBinary: return "bad TEGW binary header";
    default: return "parse error";
  }
}

static void persistToSd(void (*progress)()) {
  if (!sdAvailable) {
    writeLog("Waveform accepted but not persisted (no SD)");
    return;
  }
  sd.remove(WaveformFile);
  FsFile f = sd.open(WaveformFile, O_RDWR | O_CREAT | O_TRUNC);
  if (!f) {
    writeLog("Waveform accepted but not persisted (SD write failed)");
    return;
  }
  uint8_t header[WaveBinaryHeaderSize];
  waveBinaryHeaderWrite(header, storedType, storedCount);
  f.write(header, sizeof(header));
  if (storedType == WaveTypeReference) {
    // Chunked so multi-MB writes keep servicing the watchdog
    constexpr uint32_t ChunkSamples = 16384;
    for (uint32_t off = 0; off < storedCount; off += ChunkSamples) {
      const uint32_t n = storedCount - off < ChunkSamples ? storedCount - off : ChunkSamples;
      f.write(reinterpret_cast<const uint8_t *>(&waveSamples[off]), n * sizeof(int16_t));
      if (progress != nullptr) {
        progress();
      }
    }
  } else {
    for (uint32_t i = 0; i < storedCount; i++) {
      uint8_t rec[8] = {static_cast<uint8_t>(segLevelsQ15[i]), static_cast<uint8_t>(segLevelsQ15[i] >> 8),
                        0, 0,
                        static_cast<uint8_t>(segMicros[i]), static_cast<uint8_t>(segMicros[i] >> 8),
                        static_cast<uint8_t>(segMicros[i] >> 16), static_cast<uint8_t>(segMicros[i] >> 24)};
      f.write(rec, sizeof(rec));
    }
  }
  f.close();
}

// Read exactly n bytes from a Stream with a bounded overall patience; returns
// bytes actually read
static uint32_t readFully(Stream &in, uint8_t *out, uint32_t n, void (*progress)()) {
  uint32_t got = 0;
  uint32_t idleSpins = 0;
  while (got < n && idleSpins < 200000) {
    const int c = in.read();
    if (c < 0) {
      idleSpins++;
      continue;
    }
    idleSpins = 0;
    out[got++] = static_cast<uint8_t>(c);
    if ((got & 0x3FFF) == 0 && progress != nullptr) {
      progress();
    }
  }
  return got;
}

static bool applyBinary(Stream &in, uint8_t firstFour[4], const char **errorOut, void (*progress)()) {
  uint8_t header[WaveBinaryHeaderSize];
  memcpy(header, firstFour, 4);
  if (readFully(in, header + 4, WaveBinaryHeaderSize - 4, progress) != WaveBinaryHeaderSize - 4) {
    *errorOut = "truncated binary header";
    return false;
  }
  uint8_t type;
  const int32_t n = waveBinaryHeaderRead(header, &type);
  if (n < 0) {
    *errorOut = errorText(n);
    return false;
  }

  storedType = WaveTypeNone; // invalidate while the store is being replaced
  if (type == WaveTypeReference) {
    const uint32_t bytes = static_cast<uint32_t>(n) * sizeof(int16_t);
    if (readFully(in, reinterpret_cast<uint8_t *>(waveSamples), bytes, progress) != bytes) {
      *errorOut = "truncated sample data";
      return false;
    }
  } else {
    for (int32_t i = 0; i < n; i++) {
      uint8_t rec[8];
      if (readFully(in, rec, sizeof(rec), progress) != sizeof(rec)) {
        *errorOut = "truncated segment data";
        return false;
      }
      segLevelsQ15[i] = static_cast<int16_t>(rec[0] | (rec[1] << 8));
      segMicros[i] = static_cast<uint32_t>(rec[4]) | (static_cast<uint32_t>(rec[5]) << 8) |
                     (static_cast<uint32_t>(rec[6]) << 16) | (static_cast<uint32_t>(rec[7]) << 24);
      if (segMicros[i] == 0) {
        *errorOut = "zero segment duration";
        return false;
      }
    }
  }
  storedType = type;
  storedCount = static_cast<uint32_t>(n);
  return true;
}

static bool applyText(Stream &in, uint8_t firstFour[4], uint32_t firstLen, const char **errorOut,
                      void (*progress)()) {
  WaveParser parser;
  parser.samples = waveSamples;
  parser.maxSamples = MaxWaveSamples;
  parser.segLevelsQ15 = segLevelsQ15;
  parser.segMicros = segMicros;
  parser.maxSegments = MaxWaveSegments;

  storedType = WaveTypeNone; // the PSRAM store is being replaced

  char line[128];
  uint32_t len = 0;
  for (uint32_t i = 0; i < firstLen; i++) {
    line[len++] = static_cast<char>(firstFour[i]);
  }

  uint32_t idleSpins = 0;
  uint32_t fed = 0;
  for (;;) {
    const int c = in.read();
    if (c < 0) {
      if (++idleSpins < 200000) {
        continue;
      }
    }
    idleSpins = 0;
    if (c >= 0 && c != '\n') {
      if (len < sizeof(line) - 1) {
        line[len++] = static_cast<char>(c);
      }
      continue;
    }
    // newline or end of stream: feed the buffered line
    const int32_t err = waveParseLine(parser, line, line + len);
    if (err != 0) {
      *errorOut = errorText(err);
      return false;
    }
    len = 0;
    if ((++fed & 0xFFF) == 0 && progress != nullptr) {
      progress();
    }
    if (c < 0) {
      break;
    }
  }

  const int32_t n = waveParseFinish(parser);
  if (n < 0) {
    *errorOut = errorText(n);
    return false;
  }
  storedType = parser.type;
  storedCount = static_cast<uint32_t>(n);
  return true;
}

bool waveformApplyStream(Stream &in, const char **errorOut, void (*progress)()) {
  // Sniff the first four bytes: TEGW binary vs text
  uint8_t firstFour[4];
  const uint32_t got = readFully(in, firstFour, 4, progress);

  bool ok;
  if (got == 4 && memcmp(firstFour, "TEGW", 4) == 0) {
    ok = applyBinary(in, firstFour, errorOut, progress);
  } else {
    ok = applyText(in, firstFour, got, errorOut, progress);
  }
  if (!ok) {
    return false;
  }

  persistToSd(progress);

  char buf[64];
  snprintf(buf, sizeof(buf), "Waveform loaded: %s, %lu %s",
           storedType == WaveTypeReference ? "reference" : "sequence",
           static_cast<unsigned long>(storedCount),
           storedType == WaveTypeReference ? "points" : "segments");
  writeLog(buf);
  return true;
}

void waveformLoadFromSd() {
  if (!sdAvailable || !sd.exists(WaveformFile)) {
    return;
  }
  FsFile f = sd.open(WaveformFile, O_RDONLY);
  if (!f) {
    return;
  }
  uint8_t header[WaveBinaryHeaderSize];
  if (f.read(header, sizeof(header)) != static_cast<int>(sizeof(header))) {
    f.close();
    return;
  }
  uint8_t type;
  const int32_t n = waveBinaryHeaderRead(header, &type);
  if (n < 0) {
    writeLog("Stored waveform header invalid - ignoring");
    f.close();
    return;
  }
  bool ok = true;
  if (type == WaveTypeReference) {
    const uint32_t bytes = static_cast<uint32_t>(n) * sizeof(int16_t);
    ok = f.read(reinterpret_cast<uint8_t *>(waveSamples), bytes) == static_cast<int>(bytes);
  } else {
    for (int32_t i = 0; ok && i < n; i++) {
      uint8_t rec[8];
      ok = f.read(rec, sizeof(rec)) == static_cast<int>(sizeof(rec));
      if (ok) {
        segLevelsQ15[i] = static_cast<int16_t>(rec[0] | (rec[1] << 8));
        segMicros[i] = static_cast<uint32_t>(rec[4]) | (static_cast<uint32_t>(rec[5]) << 8) |
                       (static_cast<uint32_t>(rec[6]) << 16) | (static_cast<uint32_t>(rec[7]) << 24);
      }
    }
  }
  f.close();
  if (!ok) {
    writeLog("Stored waveform truncated - ignoring");
    return;
  }
  storedType = type;
  storedCount = static_cast<uint32_t>(n);
}

uint8_t waveformType() {
  return storedType;
}

uint32_t waveformCount() {
  return storedCount;
}

const int16_t *waveformSamples() {
  return waveSamples;
}

uint32_t waveformSegments(const int16_t **levelsQ15, const uint32_t **micros) {
  *levelsQ15 = segLevelsQ15;
  *micros = segMicros;
  return storedType == WaveTypeSequence ? storedCount : 0;
}
