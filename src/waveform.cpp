#include "waveform.h"
#include "waveform_parse.h"
#include "gzip_stream.h"
#include "stream_ring.h"
#include "utils.h"
#include <SdFat.h>

extern SdFs sd;
extern bool sdAvailable;

static const char *const WaveformFile = "/waveform.bin";

// PSRAM-resident store for waveforms up to MaxWaveSamples; larger files play
// by streaming from SD through the double buffer below.
EXTMEM static int16_t waveSamples[MaxWaveSamples];

static int16_t segLevelsQ15[MaxWaveSegments];
static uint32_t segMicros[MaxWaveSegments];
static uint8_t storedType = WaveTypeNone;
static uint32_t storedCount = 0;

enum WaveMode : uint8_t { ModeNone, ModeResident, ModeStreaming };
static uint8_t waveMode = ModeNone;

// ---------------------------------------------------------------------------
// SD streaming playback: 2 x 16384 samples (64KB OCRAM) = ~0.8s of headroom
// per buffer at a 20kHz carrier against SD latency spikes
// ---------------------------------------------------------------------------

constexpr uint32_t StreamBufSamples = 16384;
DMAMEM static int16_t streamBufs[2][StreamBufSamples];
static const int16_t *const streamBufPtrs[2] = {streamBufs[0], streamBufs[1]};
static StreamRing ring;
static FsFile streamFile;
static uint32_t streamNextSample = 0;

static void fillStreamBuffer(uint8_t k) {
  uint32_t total = 0;
  while (total < StreamBufSamples) {
    const uint32_t remaining = storedCount - streamNextSample;
    uint32_t n = StreamBufSamples - total;
    if (n > remaining) {
      n = remaining;
    }
    streamFile.seekSet(WaveBinaryHeaderSize + static_cast<uint64_t>(streamNextSample) * 2);
    const int got = streamFile.read(reinterpret_cast<uint8_t *>(&streamBufs[k][total]), n * 2);
    if (got != static_cast<int>(n * 2)) {
      break; // SD trouble: publish what we have (possibly nothing -> underrun hold)
    }
    total += n;
    streamNextSample += n;
    if (streamNextSample >= storedCount) {
      streamNextSample = 0; // wrap: the waveform repeats ad infinitum
    }
  }
  asm volatile("" ::: "memory"); // buffer contents land before valid[] flips
  ring.markFilled(k, static_cast<uint16_t>(total));
}

void waveformStreamTask() {
  if (waveMode != ModeStreaming || !streamFile) {
    return;
  }
  for (uint8_t k = 0; k < 2; k++) {
    if (ring.refillNeeded[k]) {
      fillStreamBuffer(k);
    }
  }
}

FASTRUN int16_t waveformStreamNext() {
  return ring.next(streamBufPtrs);
}

uint32_t waveformStreamUnderruns() {
  return ring.underruns;
}

bool waveformIsStreaming() {
  return waveMode == ModeStreaming;
}

// ---------------------------------------------------------------------------
// Activation: adopt whatever /waveform.bin holds (fast path: bulk PSRAM read;
// beyond-PSRAM references switch to SD streaming)
// ---------------------------------------------------------------------------

static void deactivate() {
  waveMode = ModeNone;
  storedType = WaveTypeNone;
  storedCount = 0;
  if (streamFile) {
    streamFile.close();
  }
  ring.reset();
}

static void activateFromSd() {
  if (!sdAvailable || !sd.exists(WaveformFile)) {
    return;
  }
  FsFile f = sd.open(WaveformFile, O_RDONLY);
  if (!f) {
    return;
  }
  uint8_t header[WaveBinaryHeaderSize];
  uint8_t type;
  int32_t n = -1;
  if (f.read(header, sizeof(header)) == static_cast<int>(sizeof(header))) {
    n = waveBinaryHeaderRead(header, &type);
  }
  if (n < 0) {
    writeLog("Stored waveform header invalid - ignoring");
    f.close();
    return;
  }

  bool ok = true;
  if (type == WaveTypeReference && static_cast<uint32_t>(n) <= MaxWaveSamples) {
    const uint32_t bytes = static_cast<uint32_t>(n) * 2;
    ok = f.read(reinterpret_cast<uint8_t *>(waveSamples), bytes) == static_cast<int>(bytes);
    f.close();
    if (ok) {
      storedType = type;
      storedCount = static_cast<uint32_t>(n);
      waveMode = ModeResident;
    }
  } else if (type == WaveTypeReference) {
    // Beyond PSRAM: stream it. Validate the file is long enough, then prefill.
    ok = f.fileSize() >= WaveBinaryHeaderSize + static_cast<uint64_t>(n) * 2;
    f.close();
    if (ok) {
      storedType = type;
      storedCount = static_cast<uint32_t>(n);
      streamFile = sd.open(WaveformFile, O_RDONLY);
      streamNextSample = 0;
      ring.reset();
      fillStreamBuffer(0);
      fillStreamBuffer(1);
      waveMode = ModeStreaming;
      char buf[64];
      snprintf(buf, sizeof(buf), "Waveform streams from SD: %lu samples",
               static_cast<unsigned long>(storedCount));
      writeLog(buf);
    }
  } else { // sequence
    for (int32_t i = 0; ok && i < n; i++) {
      uint8_t rec[8];
      ok = f.read(rec, sizeof(rec)) == static_cast<int>(sizeof(rec));
      if (ok) {
        segLevelsQ15[i] = static_cast<int16_t>(rec[0] | (rec[1] << 8));
        segMicros[i] = static_cast<uint32_t>(rec[4]) | (static_cast<uint32_t>(rec[5]) << 8) |
                       (static_cast<uint32_t>(rec[6]) << 16) | (static_cast<uint32_t>(rec[7]) << 24);
        ok = segMicros[i] != 0;
      }
    }
    f.close();
    if (ok) {
      storedType = type;
      storedCount = static_cast<uint32_t>(n);
      waveMode = ModeResident;
    }
  }
  if (!ok) {
    writeLog("Stored waveform truncated - ignoring");
    deactivate();
  }
}

void waveformLoadFromSd() {
  activateFromSd();
}

// ---------------------------------------------------------------------------
// Ingest: HTTP body -> [gunzip] -> TEGW binary or teg-wave text -> sink.
// With SD the sink is /waveform.bin (any size up to MaxStreamSamples); without
// SD it is the PSRAM store (MaxWaveSamples, not persisted).
// ---------------------------------------------------------------------------

constexpr uint32_t StagingSamples = 4096;
DMAMEM static int16_t staging[StagingSamples];
DMAMEM static GzipInflater gunzip; // 32KB dictionary lives in OCRAM

namespace ingest {
enum Stage : uint8_t { Detect, Text, BinPayload, BinSeqPayload, Failed };

static Stage stage;
static const char *err;
static bool useSd;
static FsFile out;
static uint32_t sunkSamples;   // samples committed to the sink
static uint8_t pre[WaveBinaryHeaderSize];
static uint32_t preLen;
static uint8_t binType;
static uint32_t binCount;
static uint32_t payloadGot;    // bytes of binary payload consumed
static uint8_t recBuf[8];
static uint32_t recLen;
static WaveParser parser;
static char line[128];
static uint32_t lineLen;
static void (*progressFn)();

static bool fail(const char *message) {
  err = message;
  stage = Failed;
  return false;
}

static bool sinkSamples(const int16_t *data, uint32_t n) {
  if (useSd) {
    if (sunkSamples + n > MaxStreamSamples) {
      return fail("too many points/segments");
    }
    if (out.write(reinterpret_cast<const uint8_t *>(data), n * 2) != static_cast<int>(n * 2)) {
      return fail("SD write failed");
    }
  } else {
    if (sunkSamples + n > MaxWaveSamples) {
      return fail("waveform too long without an SD card");
    }
    memcpy(&waveSamples[sunkSamples], data, n * 2);
  }
  sunkSamples += n;
  if (progressFn != nullptr) {
    progressFn();
  }
  return true;
}

static bool flushParserStaging() {
  if (parser.count > 0) {
    const uint32_t n = parser.count;
    parser.count = 0;
    return sinkSamples(staging, n);
  }
  return true;
}

static bool openSink(bool binaryWithHeader) {
  if (useSd) {
    sd.remove(WaveformFile);
    out = sd.open(WaveformFile, O_RDWR | O_CREAT | O_TRUNC);
    if (!out) {
      return fail("SD open failed");
    }
    // Placeholder header; rewritten with the real type/count at finish
    uint8_t header[WaveBinaryHeaderSize];
    waveBinaryHeaderWrite(header, binaryWithHeader ? binType : WaveTypeReference, 2);
    if (out.write(header, sizeof(header)) != static_cast<int>(sizeof(header))) {
      return fail("SD write failed");
    }
  }
  return true;
}

static bool feedTextByte(uint8_t c) {
  if (c != '\n') {
    if (lineLen < sizeof(line) - 1) {
      line[lineLen++] = static_cast<char>(c);
    }
    return true;
  }
  const int32_t lineErr = waveParseLine(parser, line, line + lineLen);
  lineLen = 0;
  if (lineErr != 0) {
    switch (lineErr) {
      case WaveErrNoType: return fail("first line must be type=reference or type=sequence");
      case WaveErrBadValue: return fail("unparseable level value");
      case WaveErrTooMany: return fail("too many points/segments");
      case WaveErrBadDuration: return fail("bad or zero duration_us");
      default: return fail("parse error");
    }
  }
  if (parser.type == WaveTypeReference && parser.count >= StagingSamples) {
    return flushParserStaging();
  }
  return true;
}

static bool feedLogical(const uint8_t *data, size_t len);

static bool leaveDetect() {
  // Called once enough bytes are buffered to classify the payload
  if (preLen >= 4 && memcmp(pre, "TEGW", 4) == 0) {
    if (preLen < WaveBinaryHeaderSize) {
      return true; // keep collecting header bytes
    }
    uint8_t type;
    const int32_t n = waveBinaryHeaderRead(pre, &type);
    if (n < 0) {
      return fail("bad TEGW binary header");
    }
    binType = type;
    binCount = static_cast<uint32_t>(n);
    if (!useSd && binType == WaveTypeReference && binCount > MaxWaveSamples) {
      return fail("waveform too long without an SD card");
    }
    if (!openSink(true)) {
      return false;
    }
    payloadGot = 0;
    recLen = 0;
    stage = binType == WaveTypeReference ? BinPayload : BinSeqPayload;
    return true;
  }
  // Text: open the sink, then replay the buffered bytes through the parser
  parser = WaveParser{};
  parser.samples = staging;
  parser.maxSamples = StagingSamples;
  parser.segLevelsQ15 = segLevelsQ15;
  parser.segMicros = segMicros;
  parser.maxSegments = MaxWaveSegments;
  lineLen = 0;
  if (!openSink(false)) {
    return false;
  }
  stage = Text;
  uint8_t replay[WaveBinaryHeaderSize];
  const uint32_t replayLen = preLen;
  memcpy(replay, pre, replayLen);
  return feedLogical(replay, replayLen);
}

static bool feedLogical(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    switch (stage) {
      case Detect:
        pre[preLen++] = data[i];
        if ((preLen >= 4 && memcmp(pre, "TEGW", 4) != 0) || preLen == WaveBinaryHeaderSize) {
          if (!leaveDetect()) {
            return false;
          }
        }
        break;
      case Text:
        if (!feedTextByte(data[i])) {
          return false;
        }
        break;
      case BinPayload: {
        recBuf[recLen++] = data[i];
        if (recLen == 2) {
          const int16_t s = static_cast<int16_t>(recBuf[0] | (recBuf[1] << 8));
          recLen = 0;
          staging[(payloadGot / 2) % StagingSamples] = s;
          payloadGot += 2;
          if ((payloadGot / 2) % StagingSamples == 0 || payloadGot == binCount * 2) {
            const uint32_t n = ((payloadGot / 2 - 1) % StagingSamples) + 1;
            if (!sinkSamples(staging, n)) {
              return false;
            }
          }
        }
        break;
      }
      case BinSeqPayload:
        recBuf[recLen++] = data[i];
        if (recLen == 8) {
          recLen = 0;
          const uint32_t idx = payloadGot / 8;
          segLevelsQ15[idx] = static_cast<int16_t>(recBuf[0] | (recBuf[1] << 8));
          segMicros[idx] = static_cast<uint32_t>(recBuf[4]) | (static_cast<uint32_t>(recBuf[5]) << 8) |
                           (static_cast<uint32_t>(recBuf[6]) << 16) |
                           (static_cast<uint32_t>(recBuf[7]) << 24);
          if (segMicros[idx] == 0) {
            return fail("bad or zero duration_us");
          }
          payloadGot += 8;
        }
        break;
      case Failed:
        return false;
    }
  }
  return true;
}

static bool gzEmit(void *, const uint8_t *data, size_t len) {
  return feedLogical(data, len);
}
} // namespace ingest

bool waveformApplyStream(Stream &in, const char **errorOut, void (*progress)()) {
  using namespace ingest;

  deactivate(); // stop streaming playback before the file is replaced

  stage = Detect;
  err = "";
  useSd = sdAvailable;
  sunkSamples = 0;
  preLen = 0;
  payloadGot = 0;
  recLen = 0;
  progressFn = progress;

  bool gz = false;
  bool gzDecided = false;
  gunzip.begin();

  uint8_t chunk[1024];
  uint32_t chunkLen = 0;
  uint32_t idleSpins = 0;
  bool ok = true;

  for (;;) {
    const int c = in.read();
    if (c < 0) {
      if (++idleSpins < 200000) {
        continue;
      }
    } else {
      idleSpins = 0;
      chunk[chunkLen++] = static_cast<uint8_t>(c);
    }
    const bool flushNow = chunkLen == sizeof(chunk) || (c < 0 && chunkLen > 0);
    if (flushNow) {
      if (!gzDecided && chunkLen >= 2) {
        gz = GzipInflater::looksLikeGzip(chunk);
        gzDecided = true;
      }
      ok = gz ? gunzip.feed(chunk, chunkLen, ingest::gzEmit, nullptr)
              : feedLogical(chunk, chunkLen);
      chunkLen = 0;
      if (!ok) {
        break;
      }
      if (progress != nullptr) {
        progress();
      }
    }
    if (c < 0) {
      break;
    }
  }

  if (ok && stage == Detect && preLen > 0 && preLen < WaveBinaryHeaderSize) {
    ok = leaveDetect(); // very short upload: classify what we buffered as text
  }
  if (ok && stage == Text && lineLen > 0) {
    ok = feedTextByte('\n'); // terminate a final unterminated line
  }
  if (ok && gz && !gunzip.finish()) {
    ok = false;
    err = gunzip.error();
  }

  uint8_t finalType = WaveTypeNone;
  uint32_t finalCount = 0;
  if (ok) {
    switch (stage) {
      case Text:
        if (parser.type == WaveTypeReference) {
          ok = flushParserStaging();
          finalType = WaveTypeReference;
          finalCount = sunkSamples;
          if (ok && finalCount < 2) {
            ok = fail("need at least 2 reference points or 1 segment");
          }
        } else if (parser.type == WaveTypeSequence) {
          finalType = WaveTypeSequence;
          finalCount = parser.count;
          if (finalCount < 1) {
            ok = fail("need at least 2 reference points or 1 segment");
          }
        } else {
          ok = fail("first line must be type=reference or type=sequence");
        }
        break;
      case BinPayload:
        finalType = WaveTypeReference;
        finalCount = binCount;
        if (payloadGot != binCount * 2) {
          ok = fail("truncated sample data");
        }
        break;
      case BinSeqPayload:
        finalType = WaveTypeSequence;
        finalCount = binCount;
        if (payloadGot != binCount * 8) {
          ok = fail("truncated segment data");
        }
        break;
      default:
        ok = fail("empty upload");
    }
  }

  if (ok && useSd) {
    if (finalType == WaveTypeSequence) {
      // Sequence records were parsed into RAM; write them after the header
      for (uint32_t i = 0; i < finalCount; i++) {
        uint8_t rec[8] = {static_cast<uint8_t>(segLevelsQ15[i]),
                          static_cast<uint8_t>(segLevelsQ15[i] >> 8), 0, 0,
                          static_cast<uint8_t>(segMicros[i]), static_cast<uint8_t>(segMicros[i] >> 8),
                          static_cast<uint8_t>(segMicros[i] >> 16),
                          static_cast<uint8_t>(segMicros[i] >> 24)};
        out.write(rec, sizeof(rec));
      }
    }
    uint8_t header[WaveBinaryHeaderSize];
    waveBinaryHeaderWrite(header, finalType, finalCount);
    out.seekSet(0);
    out.write(header, sizeof(header));
    out.close();
  } else if (out) {
    out.close();
  }

  if (!ok) {
    *errorOut = err[0] != '\0' ? err : "parse error";
    if (useSd) {
      sd.remove(WaveformFile); // don't leave a half-written file to load at boot
    }
    return false;
  }

  if (useSd) {
    activateFromSd();
  } else {
    storedType = finalType;
    storedCount = finalCount;
    waveMode = ModeResident;
    writeLog("Waveform accepted but not persisted (no SD)");
  }

  char buf[72];
  snprintf(buf, sizeof(buf), "Waveform loaded: %s, %lu %s%s",
           finalType == WaveTypeReference ? "reference" : "sequence",
           static_cast<unsigned long>(finalCount),
           finalType == WaveTypeReference ? "points" : "segments",
           waveformIsStreaming() ? " (SD streaming)" : "");
  writeLog(buf);
  return true;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

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

bool waveformPreview(int16_t *outSamples, uint32_t points) {
  if (storedType != WaveTypeReference || storedCount == 0) {
    return false;
  }
  if (waveMode == ModeResident) {
    for (uint32_t i = 0; i < points; i++) {
      outSamples[i] = waveSamples[(static_cast<uint64_t>(i) * storedCount) / points];
    }
    return true;
  }
  // Streaming: strided reads from the file
  FsFile f = sd.open(WaveformFile, O_RDONLY);
  if (!f) {
    return false;
  }
  for (uint32_t i = 0; i < points; i++) {
    const uint64_t sample = (static_cast<uint64_t>(i) * storedCount) / points;
    f.seekSet(WaveBinaryHeaderSize + sample * 2);
    uint8_t b[2] = {0, 0};
    f.read(b, 2);
    outSamples[i] = static_cast<int16_t>(b[0] | (b[1] << 8));
  }
  f.close();
  return true;
}
