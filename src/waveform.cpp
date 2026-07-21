#include "waveform.h"
#include "waveform_parse.h"
#include "spwm_math.h"
#include "utils.h"
#include <Arduino.h>
#include <SdFat.h>

extern SdFs sd;
extern bool sdAvailable;

static const char *const WaveformFile = "/waveform.txt";

// Large scratch/storage in OCRAM, not DTCM
DMAMEM static char textBuffer[32768];
DMAMEM static float parsePoints[MaxWavePoints];

static int16_t referenceLut[SpwmLutSize];
static int16_t segLevelsQ15[MaxWaveSegments];
static uint32_t segMicros[MaxWaveSegments];
static uint8_t storedType = WaveTypeNone;
static uint32_t storedCount = 0;

char *waveformTextBuffer(size_t *capacity) {
  *capacity = sizeof(textBuffer);
  return textBuffer;
}

static const char *errorText(int32_t code) {
  switch (code) {
    case WaveErrNoType: return "first line must be type=reference or type=sequence";
    case WaveErrBadValue: return "unparseable level value";
    case WaveErrTooMany: return "too many points/segments";
    case WaveErrTooFew: return "need at least 2 reference points or 1 segment";
    case WaveErrBadDuration: return "bad or zero duration_us";
    default: return "parse error";
  }
}

static bool parseInto(const char *text, const char **errorOut) {
  static int16_t tmpLevels[MaxWaveSegments];
  static uint32_t tmpMicros[MaxWaveSegments];
  uint8_t type = WaveTypeNone;
  const int32_t n = parseWaveform(text, parsePoints, MaxWavePoints, tmpLevels, tmpMicros,
                                  MaxWaveSegments, &type);
  if (n < 0) {
    *errorOut = errorText(n);
    return false;
  }
  if (type == WaveTypeReference) {
    resampleReference(parsePoints, static_cast<uint32_t>(n), referenceLut, SpwmLutSize);
  } else {
    memcpy(segLevelsQ15, tmpLevels, n * sizeof(int16_t));
    memcpy(segMicros, tmpMicros, n * sizeof(uint32_t));
  }
  storedType = type;
  storedCount = static_cast<uint32_t>(n);
  return true;
}

bool waveformApplyText(const char *text, const char **errorOut) {
  if (!parseInto(text, errorOut)) {
    return false;
  }

  if (sdAvailable) {
    sd.remove(WaveformFile);
    FsFile f = sd.open(WaveformFile, O_RDWR | O_CREAT | O_TRUNC);
    if (f) {
      f.write(text, strlen(text));
      f.close();
    } else {
      writeLog("Waveform accepted but not persisted (SD write failed)");
    }
  } else {
    writeLog("Waveform accepted but not persisted (no SD)");
  }

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
  const int n = f.read(textBuffer, sizeof(textBuffer) - 1);
  f.close();
  if (n <= 0) {
    return;
  }
  textBuffer[n] = '\0';
  const char *err;
  if (!parseInto(textBuffer, &err)) {
    writeLog("Stored waveform failed to parse - ignoring");
  }
}

uint8_t waveformType() {
  return storedType;
}

uint32_t waveformCount() {
  return storedCount;
}

const int16_t *waveformReferenceLut() {
  return referenceLut;
}

uint32_t waveformSegments(const int16_t **levelsQ15, const uint32_t **micros) {
  *levelsQ15 = segLevelsQ15;
  *micros = segMicros;
  return storedType == WaveTypeSequence ? storedCount : 0;
}
