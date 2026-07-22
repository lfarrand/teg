#include "web_handlers.h"
#include "config_json.h"
#include "config_serde.h"
#include "pwm_utils.h"
#include "capture.h"
#include "meter.h"
#include "acmp.h"
#include "pll.h"
#include "scope_math.h"
#include "spectrum_math.h"
#include <arm_math.h>
#include "thermal.h"
#include "waveform.h"
#include "waveform_parse.h"
#include "modulation.h"
#include "main.h"
#include "utils.h"
#if __has_include("version.h")
#include "version.h"
#else
#define TEG_GIT_HASH "dev"
#endif
#include "web_assets.h"
#include <ArduinoJson.h>

extern Application app;
extern MainConfig config;
extern EthernetServer server;
extern const char* filename;

// Set by the update handler after applying changes to the hardware; loop()
// persists the config to SD so the HTTP response never waits on the card.
volatile bool configSaveNeeded = false;

// Last measured hardware-apply duration, reported by /api/status
static volatile uint32_t lastApplyMicros = 0;

// aWOT only exposes headers that were registered before processing
static char authPinHeader[sizeof(SecurityConfig{}.WritePin) + 4];

FLASHMEM void configureWebServer() {
  app.header("X-Auth-Pin", authPinHeader, sizeof(authPinHeader));
  app.get("/", &index);
  app.get("/index.html", &index);
  app.get("/stats.html", &serve_stats);
  app.get("/pico.min.css", &serve_pico_css);
  app.get("/api/config", &api_config_get);
  app.post("/api/config", &api_config_post);
  app.get("/api/status", &api_status);
  app.get("/api/capture/raw", &api_capture_raw);
  app.get("/api/capture", &api_capture);
  app.get("/api/scope", &api_scope_get);
  app.post("/api/scope/arm", &api_scope_arm);
  app.post("/api/scope/release", &api_scope_release);
  app.get("/api/waveform", &api_waveform_get);
  app.post("/api/waveform", &api_waveform_post);
  app.post("/api/fault/clear", &api_fault_clear);
  app.get("/api/crash", &api_crash);
  app.get("/api/spectrum", &api_spectrum);
}

void processWebServer() {
  EthernetClient client = server.available();
  if (client) {
    app.process(&client);
    client.stop();
  }
}

FLASHMEM static void sendAsset(Response &res, const char *path, const char *cacheControl) {
  for (unsigned int i = 0; i < WebAssetCount; i++) {
    if (strcmp(WebAssets[i].path, path) == 0) {
      res.set("Content-Type", WebAssets[i].contentType);
      res.set("Content-Encoding", "gzip");
      res.set("Cache-Control", cacheControl);
      res.write(const_cast<uint8_t *>(WebAssets[i].data), WebAssets[i].length);
      return;
    }
  }
  res.sendStatus(404);
}

FLASHMEM void index(Request &req, Response &res) {
  sendAsset(res, "/index.html", "no-cache");
}

FLASHMEM void serve_stats(Request &req, Response &res) {
  sendAsset(res, "/stats.html", "no-cache");
}

FLASHMEM void serve_pico_css(Request &req, Response &res) {
  sendAsset(res, "/pico.min.css", "max-age=86400"); // content-stable, cache a day
}

FLASHMEM void api_config_get(Request &req, Response &res) {
  JsonDocument doc;
  configToJson(config, doc);
  redactSecrets(doc); // the Influx token and write PIN never leave the device
  res.set("Content-Type", "application/json");
  serializeJson(doc, res);
}

// Constant-time comparison over the whole PIN buffer so response timing
// leaks nothing about how many leading characters matched
static bool pinMatches(const char *provided) {
  if (provided == nullptr) {
    return false;
  }
  uint8_t diff = 0;
  bool providedEnded = false;
  for (size_t i = 0; i < sizeof(config.Security.WritePin); i++) {
    const char p = providedEnded ? '\0' : provided[i];
    if (p == '\0') {
      providedEnded = true;
    }
    diff |= static_cast<uint8_t>(p ^ config.Security.WritePin[i]);
  }
  return diff == 0;
}

// When a write PIN is configured, POSTs must carry it in X-Auth-Pin
static bool writeAuthorized(Request &req) {
  if (config.Security.WritePin[0] == '\0') {
    return true;
  }
  return pinMatches(req.get("X-Auth-Pin"));
}

FLASHMEM void api_config_post(Request &req, Response &res) {
  if (!writeAuthorized(req)) {
    res.sendStatus(401);
    return;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, req);
  if (err) {
    res.sendStatus(400);
    return;
  }

  const bool spwmWasEnabled = spwmActive();
  if (spwmWasEnabled) {
    disablePwmInterrupts();
  }

  MainConfig previous;
  memcpy(&previous, &config, sizeof(previous));

  configFromJson(doc, config);
  preserveSecrets(config, previous); // empty secret in the POST = keep current
  if (validateConfig(config)) {
    writeLog("Invalid values in config; corrected");
  }

  // Apply to hardware first: register writes are buffered and take effect at
  // the next PWM reload, i.e. within one PWM period
  const uint32_t applyStart = ARM_DWT_CYCCNT;
  applyPwmConfig(previous);
  // Not while a trip is latched (a refused clear): the modulation ISR would
  // drive duty updates into fault-masked submodules
  if (spwmActive() && !vFaultTripped) {
    attachModule2PwmInterruptVectors();
    enablePwmInterrupts();
  }
  lastApplyMicros = (ARM_DWT_CYCCNT - applyStart) / (F_CPU_ACTUAL / 1000000);

  res.set("Content-Type", "application/json");
  JsonDocument out;
  out["applyMicros"] = lastApplyMicros;
  serializeJson(out, res);

  // Persist from loop() so the response doesn't wait on the SD card
  configSaveNeeded = true;
}

FLASHMEM void api_waveform_post(Request &req, Response &res) {
  if (!writeAuthorized(req)) {
    res.sendStatus(401);
    return;
  }

  const char *err = "";
  // Streams text or TEGW binary straight into the PSRAM store; multi-MB
  // uploads take seconds, so the watchdog is serviced during the transfer
  if (!waveformApplyStream(req, &err, &kickWatchdog)) {
    res.status(400);
    res.set("Content-Type", "application/json");
    JsonDocument out;
    out["error"] = err;
    serializeJson(out, res);
    return;
  }

  // If the running modulation uses the uploaded waveform, rebuild it live
  const uint8_t wave = config.Pwm.Tm2.ReferenceWaveform;
  if (spwmActive() && (wave == RefWaveCustom || wave == RefWaveSequence)) {
    disablePwmInterrupts();
    buildSpwmLut();
    enablePwmInterrupts();
  }

  res.set("Content-Type", "application/json");
  JsonDocument out;
  out["type"] = waveformType() == WaveTypeReference ? "reference" : "sequence";
  out["count"] = waveformCount();
  serializeJson(out, res);
}

FLASHMEM void api_waveform_get(Request &req, Response &res) {
  JsonDocument doc;
  const uint8_t t = waveformType();
  doc["type"] = t == WaveTypeReference ? "reference" : t == WaveTypeSequence ? "sequence" : "none";
  doc["count"] = waveformCount();
  doc["streaming"] = waveformIsStreaming();
  doc["underruns"] = waveformStreamUnderruns();
  if (t == WaveTypeReference) {
    const uint32_t points = waveformCount() < 128 ? waveformCount() : 128;
    static int16_t previewBuf[128];
    if (waveformPreview(previewBuf, points)) {
      JsonArray preview = doc["preview"].to<JsonArray>();
      for (uint32_t i = 0; i < points; i++) {
        preview.add(previewBuf[i]);
      }
    }
  } else if (t == WaveTypeSequence) {
    const int16_t *levels;
    const uint32_t *micros;
    const uint32_t n = waveformSegments(&levels, &micros);
    JsonArray lv = doc["levels"].to<JsonArray>();
    JsonArray us = doc["micros"].to<JsonArray>();
    for (uint32_t i = 0; i < n; i++) {
      lv.add(levels[i]);
      us.add(micros[i]);
    }
  }
  res.set("Content-Type", "application/json");
  serializeJson(doc, res);
}

// FFT working buffers in OCRAM: 4096-point re/im floats + sample copy
DMAMEM static float fftRe[SpectrumMaxPoints];
DMAMEM static float fftIm[SpectrumMaxPoints];
DMAMEM static int16_t fftSamples[SpectrumMaxPoints];

// CMSIS real-FFT instance, re-initialised when the point count changes
static arm_rfft_fast_instance_f32 rfftInstance;
static uint32_t rfftInstancePoints = 0;

FLASHMEM void api_spectrum(Request &req, Response &res) {
  char qbuf[12];
  uint32_t points = SpectrumMaxPoints;
  if (req.query("points", qbuf, sizeof(qbuf))) {
    points = strtoul(qbuf, nullptr, 10);
  }
  if (points < 256 || points > SpectrumMaxPoints || (points & (points - 1)) != 0) {
    points = SpectrumMaxPoints;
  }
  // Engine: "portable" (the natively-tested radix-2, default) or "cmsis"
  // (ARM's mixed-radix real FFT, ~5x faster)
  bool useCmsis = false;
  if (req.query("engine", qbuf, sizeof(qbuf))) {
    useCmsis = strcmp(qbuf, "cmsis") == 0;
  }

  res.set("Content-Type", "application/json");
  JsonDocument doc;
  const uint32_t sampleHz = config.Pwm.Tm2.SpwmCarrierFrequency;
  doc["sampleHz"] = sampleHz;
  doc["points"] = points;

  if (!captureActive() || captureCopyRecent(fftSamples, points) == 0) {
    doc["available"] = false;
    serializeJson(doc, res);
    return;
  }

  const uint32_t computeStart = ARM_DWT_CYCCNT;
  const uint32_t halfN = points / 2;
  float *mag;
  if (useCmsis) {
    if (rfftInstancePoints != points) {
      if (arm_rfft_fast_init_f32(&rfftInstance, points) != ARM_MATH_SUCCESS) {
        useCmsis = false; // unsupported length: fall back
      } else {
        rfftInstancePoints = points;
      }
    }
  }
  if (useCmsis) {
    prepareSpectrumInputReal(fftSamples, points, fftRe);
    arm_rfft_fast_f32(&rfftInstance, fftRe, fftIm, 0); // consumes fftRe
    mag = fftRe;
    spectrumMagnitudesPacked(fftIm, mag, halfN);
  } else {
    prepareSpectrumInput(fftSamples, points, fftRe, fftIm);
    fftRadix2(fftRe, fftIm, points);
    // In-place: mag[i] depends only on re[i]/im[i], so re[] can hold the result
    mag = fftRe;
    spectrumMagnitudes(fftRe, fftIm, mag, halfN);
  }
  doc["engine"] = useCmsis ? "cmsis" : "portable";
  doc["computeMicros"] = (ARM_DWT_CYCCNT - computeStart) / (F_CPU_ACTUAL / 1000000);

  const uint32_t fundBin = findFundamentalBin(mag, halfN);
  const float fund = harmonicPeak(mag, halfN, fundBin);
  doc["available"] = true;
  doc["binHz"] = static_cast<float>(sampleHz) / points;
  doc["fundamentalHz"] = (static_cast<float>(sampleHz) / points) * fundBin;
  doc["thdPercent"] = thdPercent(mag, halfN, fundBin);

  // First 512 bins, normalized to the fundamental, rounded to 4 decimals
  const uint32_t outBins = halfN < 512 ? halfN : 512;
  JsonArray arr = doc["mag"].to<JsonArray>();
  for (uint32_t i = 0; i < outBins; i++) {
    arr.add(fund > 0.0f ? roundf((mag[i] / fund) * 10000.0f) / 10000.0f : 0.0f);
  }
  serializeJson(doc, res);
}

constexpr uint32_t MaxCaptureBins = 600;

void api_capture(Request &req, Response &res) {
  char qbuf[16];
  uint32_t count = 20000, bins = MaxCaptureBins;
  if (req.query("count", qbuf, sizeof(qbuf))) {
    count = strtoul(qbuf, nullptr, 10);
  }
  if (req.query("bins", qbuf, sizeof(qbuf))) {
    bins = strtoul(qbuf, nullptr, 10);
  }
  if (bins == 0) bins = 1;
  if (bins > MaxCaptureBins) bins = MaxCaptureBins;

  bool currentChannel = false;
  if (req.query("channel", qbuf, sizeof(qbuf))) {
    currentChannel = qbuf[0] == 'i';
  }

  static uint16_t binMin[MaxCaptureBins];
  static uint16_t binMax[MaxCaptureBins];
  const uint32_t used = currentChannel ? captureDecimateCurrent(count, bins, binMin, binMax)
                                       : captureDecimate(count, bins, binMin, binMax);

  JsonDocument doc;
  doc["channel"] = currentChannel ? "i" : "v";
  doc["sampleHz"] = config.Pwm.Tm2.SpwmCarrierFrequency;
  doc["count"] = used;
  doc["frozen"] = captureIsFrozen();
  JsonArray mn = doc["min"].to<JsonArray>();
  JsonArray mx = doc["max"].to<JsonArray>();
  if (used > 0) {
    for (uint32_t i = 0; i < bins; i++) {
      mn.add(binMin[i]);
      mx.add(binMax[i]);
    }
  }
  res.set("Content-Type", "application/json");
  serializeJson(doc, res);
}

static const char *scopeStateName(uint8_t s) {
  switch (s) {
    case ScopeArmed: return "armed";
    case ScopeTriggered: return "triggered";
    case ScopeComplete: return "complete";
    default: return "idle";
  }
}

FLASHMEM void api_scope_get(Request &req, Response &res) {
  JsonDocument doc;
  doc["state"] = scopeStateName(captureScopeState());
  doc["source"] = captureScopeSourceIsCurrent() ? "i" : "v";
  doc["edge"] = captureScopeFalling() ? "falling" : "rising";
  doc["levelMv"] = (static_cast<uint32_t>(captureScopeLevelCounts()) * 3300 + 2047) / 4095;
  doc["postSamples"] = captureScopePostSamples();
  doc["trigSample"] = captureScopeTrigSample();
  doc["totalSamples"] = captureSampleCount();
  doc["frozen"] = captureIsFrozen();
  res.set("Content-Type", "application/json");
  serializeJson(doc, res);
}

FLASHMEM void api_scope_arm(Request &req, Response &res) {
  if (!writeAuthorized(req)) {
    res.sendStatus(401);
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, req)) {
    res.sendStatus(400);
    return;
  }
  const char *source = doc["source"] | "v";
  const char *edge = doc["edge"] | "rising";
  const uint32_t levelMv = doc["levelMv"] | 1650;
  uint32_t postSamples = doc["postSamples"] | 4096;
  if (postSamples > captureRingSamples()) {
    postSamples = captureRingSamples();
  }
  if (!captureScopeArm(source[0] == 'i', scopeLevelCounts(levelMv), edge[0] == 'f', postSamples)) {
    res.status(409);
    res.set("Content-Type", "application/json");
    res.print(F("{\"error\":\"channel not sampling (enable capture/meter and save)\"}"));
    return;
  }
  api_scope_get(req, res);
}

FLASHMEM void api_scope_release(Request &req, Response &res) {
  if (!writeAuthorized(req)) {
    res.sendStatus(401);
    return;
  }
  captureScopeRelease();
  api_scope_get(req, res);
}

// Raw ring download: 20-byte TEGC header + little-endian uint16 samples.
//   0  "TEGC"   4 magic
//   4  u8       version (1)
//   5  u8       channel (0 = voltage, 1 = current)
//   6  u16 LE   flags: bit0 frozen, bit1 trigger offset valid
//   8  u32 LE   sample rate Hz
//   12 u32 LE   sample count
//   16 u32 LE   trigger sample offset within the data (0xFFFFFFFF = none)
// For a guaranteed-consistent record download while frozen (trigger or
// fault); a rolling capture may overwrite the oldest samples mid-transfer.
DMAMEM static uint16_t rawChunk[2048];

void api_capture_raw(Request &req, Response &res) {
  char qbuf[16];
  bool currentChannel = false;
  if (req.query("channel", qbuf, sizeof(qbuf))) {
    currentChannel = qbuf[0] == 'i';
  }
  const uint32_t available = captureRawAvailable(currentChannel);
  uint32_t count = available;
  if (req.query("count", qbuf, sizeof(qbuf))) {
    count = strtoul(qbuf, nullptr, 10);
    if (count > available) {
      count = available;
    }
  }
  if (count == 0) {
    res.sendStatus(404);
    return;
  }

  const bool trigOnThisChannel = captureScopeState() == ScopeComplete &&
                                 captureScopeSourceIsCurrent() == currentChannel;
  const uint32_t trigOffset = scopeTrigOffset(trigOnThisChannel, captureScopePostSamples(), count);

  uint8_t hdr[20];
  memcpy(hdr, "TEGC", 4);
  hdr[4] = 1;
  hdr[5] = currentChannel ? 1 : 0;
  const uint16_t flags = (captureIsFrozen() ? 1 : 0) | (trigOffset != 0xFFFFFFFFu ? 2 : 0);
  memcpy(hdr + 6, &flags, 2);
  const uint32_t rate = config.Pwm.Tm2.SpwmCarrierFrequency;
  memcpy(hdr + 8, &rate, 4);
  memcpy(hdr + 12, &count, 4);
  memcpy(hdr + 16, &trigOffset, 4);

  res.set("Content-Type", "application/octet-stream");
  res.set("Content-Disposition", currentChannel ? "attachment; filename=\"capture_i.tegc\""
                                                : "attachment; filename=\"capture_v.tegc\"");
  res.write(hdr, sizeof(hdr));

  constexpr uint32_t ChunkSamples = sizeof(rawChunk) / sizeof(rawChunk[0]);
  for (uint32_t offset = 0; offset < count; offset += ChunkSamples) {
    const uint32_t n = count - offset < ChunkSamples ? count - offset : ChunkSamples;
    captureRawCopy(currentChannel, count, offset, rawChunk, n);
    res.write(reinterpret_cast<uint8_t *>(rawChunk), n * 2);
    kickWatchdog(); // a full 2MB ring takes seconds to stream
  }
}

FLASHMEM void api_fault_clear(Request &req, Response &res) {
  if (!writeAuthorized(req)) {
    res.sendStatus(401);
    return;
  }
  clearFaultTrip();
  res.set("Content-Type", "application/json");
  JsonDocument out;
  out["fault"] = vFaultTripped;
  // Why a clear may not stick: the overcurrent comparator is still asserting
  out["ocStillActive"] = acmpFaultPinActive();
  serializeJson(out, res);
}

FLASHMEM void api_crash(Request &req, Response &res) {
  res.set("Content-Type", "text/plain");
  const char *text = crashReportText();
  res.print(text[0] != '\0' ? text : "none");
}

void api_status(Request &req, Response &res) {
  JsonDocument doc;
  doc["uptimeMs"] = millis();
  doc["version"] = TEG_GIT_HASH;
  doc["resetCause"] = resetCauseString();
  doc["crash"] = crashReportText()[0] != '\0';
  doc["active"] = spwmActive();
  doc["fault"] = vFaultTripped;
  doc["ocLimit"] = config.CurrentLimit.Enabled;
  if (config.CurrentLimit.Enabled) {
    doc["ocMode"] = config.CurrentLimit.CycleByCycle ? "cbc" : "latched";
    doc["ocThresholdMv"] = acmpActualThresholdMv(); // DAC-quantized, as programmed
    doc["ocActive"] = acmpFaultPinActive();
    doc["ocTrips"] = acmpCbcTripCount();
    doc["ocTripsPerSec"] = acmpCbcTripsPerSec();
  }
  doc["isrCycles"] = vIsrCycles;
  doc["applyMicros"] = lastApplyMicros;
  doc["modMilliHz"] = modulationActualMilliHz();
  doc["indexMilli"] = modulationIndexNowMilli();
  doc["targetMilli"] = modulationIndexTargetMilli();
  doc["dtcmFree"] = getFreeMemory();
  doc["ocramFree"] = freeram();
  doc["captureActive"] = captureActive();
  doc["captureFrozen"] = captureIsFrozen();
  doc["captureSamples"] = captureSampleCount();
  doc["pllEnabled"] = config.Pll.Enabled;
  if (config.Pll.Enabled) {
    doc["pllState"] = pllStateStr();
    doc["pllLocked"] = pllLocked();
    doc["pllRefMilliHz"] = pllRefMilliHz();
    doc["pllPhaseErrCentiDeg"] = pllPhaseErrCentiDeg();
    doc["pllRefMv"] = pllRefMillivolts();
    doc["pllResyncs"] = pllResyncCount();
  }
  const MeterReadings meter = meterReadings();
  doc["meterActive"] = meter.valid;
  if (meter.valid) {
    doc["powerMw"] = meter.powerMw;
    doc["vrmsMv"] = meter.vrmsMv;
    doc["irmsMa"] = meter.irmsMa;
    doc["pfMilli"] = meter.pfMilli;
    doc["energyMwh"] = meterEnergyMwh();
  }
  // Measured feedback voltage: synchronous capture mean when available,
  // otherwise a direct (10-bit) read of the configured pin
  if (captureActive() && !captureIsFrozen()) {
    doc["feedbackMv"] = (captureMeanRaw(64) * config.Feedback.FullScaleMillivolts) / 4095U;
  } else {
    doc["feedbackMv"] =
      (static_cast<uint32_t>(analogRead(config.Feedback.AnalogPin)) * config.Feedback.FullScaleMillivolts) / 1023U;
  }
  doc["streamUnderruns"] = waveformStreamUnderruns();
  doc["derateMilli"] = thermalDerateMilliNow();
  doc["hotDeciC"] = thermalHotDeciC();   // INT16_MIN = unavailable
  doc["coldDeciC"] = thermalColdDeciC();
  doc["chipDeciC"] = thermalChipDeciC();
  res.set("Content-Type", "application/json");
  serializeJson(doc, res);
}
