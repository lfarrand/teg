#include "web_handlers.h"
#include "config_json.h"
#include "config_serde.h"
#include "pwm_utils.h"
#include "capture.h"
#include "meter.h"
#include "acmp.h"
#include "pll.h"
#include "mppt.h"
#include "mqtt.h"
#include "ota.h"
#include "presets.h"
#include "preset_name.h"
#include "event_log_api.h"
#include "mtp_service.h"
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
  // Bound the header phase and feed the watchdog while waiting on a slow client.
  // Without this a byte dribbled just inside the per-byte timeout keeps the header
  // loop alive indefinitely while nothing services the watchdog - an unauthenticated
  // reset of a generating inverter. See lib/aWOT/PATCHES.md.
  // serviceControlTasks rather than a bare kick: the header budget allows up to 4s,
  // and a 4s stall re-syncs the PLL and freezes the meter just as surely as a long
  // download does.
  Request::setServiceCallback(&serviceControlTasks);
  Request::setHeaderBudget(4000);

  // Same treatment for the response side, which was the more dangerous half: a client
  // that finishes its request and then simply stops reading advertises a zero TCP
  // window, and the write loop spun on it for ever with nothing feeding the watchdog.
  // That was an unauthenticated one-request reset of a generating inverter, on any GET.
  Response::setServiceCallback(&serviceControlTasks);
  Response::setWriteBudget(3000);

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
  app.get("/api/log", &api_log);
  app.get("/api/spectrum", &api_spectrum);
  app.get("/api/config/export", &api_config_export);
  app.post("/api/config/import", &api_config_import);
  app.get("/api/presets", &api_presets_get);
  app.post("/api/presets/save", &api_presets_save);
  app.post("/api/presets/load", &api_presets_load);
  app.post("/api/presets/delete", &api_presets_delete);
  app.post("/api/ota/commit", &api_ota_commit);
  app.post("/api/ota/abort", &api_ota_abort);
  app.get("/api/ota", &api_ota_get);
  app.post("/api/ota", &api_ota_post);
}

// Accepted connections that have not yet said anything.
//
// server.available() only ever returns a client that HAS data. A client that opens a
// socket and then stays silent was therefore never accepted, never serviced and never
// stopped - it simply held an lwIP TCP PCB indefinitely. lwipopts.h sets
// MEMP_NUM_TCP_PCB to 8, and MQTT and InfluxDB draw their outbound sockets from that
// same pool, so eight idle connections permanently disabled the web server, the broker
// link and metrics together. Opening eight sockets and saying nothing is not an attack
// that requires any skill.
//
// server.accept() takes ownership whether or not the client has spoken, so connections
// can be held here with an arrival time and reaped when they go quiet for too long.
constexpr uint8_t MaxPendingClients = 4; // leaves PCBs for MQTT, InfluxDB and the listener
constexpr uint32_t IdleClientTimeoutMs = 5000;

static EthernetClient pendingClient[MaxPendingClients];
static uint32_t pendingSince[MaxPendingClients];
static bool pendingUsed[MaxPendingClients];

static void releasePending(uint8_t i) {
  pendingClient[i].stop();
  pendingClient[i] = EthernetClient();
  pendingUsed[i] = false;
}

void processWebServer() {
  // Take ownership of anything new, so nothing can sit unaccounted for in the PCB pool.
  EthernetClient incoming = server.accept();
  if (incoming) {
    bool placed = false;
    for (uint8_t i = 0; i < MaxPendingClients; i++) {
      if (!pendingUsed[i]) {
        pendingClient[i] = incoming;
        pendingSince[i] = millis();
        pendingUsed[i] = true;
        placed = true;
        break;
      }
    }
    if (!placed) {
      // Pool full: refuse this one immediately rather than leak it. Better to drop a
      // connection than to lose the ability to serve any.
      incoming.stop();
    }
  }

  // Service at most one request per pass - app.process() runs the whole request-response
  // cycle inline, so taking two would double the worst-case time this holds the loop.
  bool served = false;
  for (uint8_t i = 0; i < MaxPendingClients; i++) {
    if (!pendingUsed[i]) {
      continue;
    }
    if (pendingClient[i].available()) {
      if (!served) {
        app.process(&pendingClient[i]);
        releasePending(i);
        served = true;
      }
      continue;
    }
    // Signed comparison so the millis() wrap cannot extend an idle client's welcome.
    const bool idleTooLong =
        static_cast<int32_t>(millis() - (pendingSince[i] + IdleClientTimeoutMs)) >= 0;
    if (!pendingClient[i].connected() || idleTooLong) {
      releasePending(i);
    }
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
  if (otaInProgress()) {
    res.sendStatus(409); // config applies could re-enable outputs mid-flash
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
  if (otaInProgress()) {
    res.sendStatus(409);
    return;
  }

  const char *err = "";
  // Streams text or TEGW binary straight into the PSRAM store; multi-MB
  // uploads take seconds, so the watchdog is serviced during the transfer.
  //
  // Deliberately still a bare kick, not serviceControlTasks(): that would run
  // waveformStreamTask(), which reads the waveform store for playback, while this
  // call is rewriting it. Servicing the control loops here needs the upload-versus-
  // playback interaction settled first - see docs/SECURITY.md.
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

// CMSIS real-FFT instance, re-initialised when the point count changes.
//
// Compiled out by default. Referencing arm_rfft_fast_f32 pulls twiddleCoef_2048 and
// twiddleCoef_rfft_4096 out of the CMSIS archive, and on a Teensy 4 plain const data
// is copied into DTCM rather than left in flash - 32KB of it, for a fast path that is
// only reachable by adding ?engine=cmsis to one diagnostic endpoint. That is 1.8x the
// entire remaining stack (18432 bytes with USB MTP compiled in), so the default trade
// is wrong: it spends the scarcest resource on the least-used feature.
//
// The portable radix-2 engine stays available, is the natively-tested one, and already
// serves every request. Define TEG_ENABLE_CMSIS_FFT to link the fast path back in;
// with it undefined, ?engine=cmsis falls back and the response reports "portable", the
// same contract already used for an unsupported point count.
#ifdef TEG_ENABLE_CMSIS_FFT
static arm_rfft_fast_instance_f32 rfftInstance;
static uint32_t rfftInstancePoints = 0;
#endif

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
  // (ARM's mixed-radix real FFT, ~5x faster). The CMSIS path is compiled out unless
  // TEG_ENABLE_CMSIS_FFT is defined - see rfftInstance above for why - and the
  // request then falls back rather than failing.
  bool useCmsis = false;
#ifdef TEG_ENABLE_CMSIS_FFT
  if (req.query("engine", qbuf, sizeof(qbuf))) {
    useCmsis = strcmp(qbuf, "cmsis") == 0;
  }
#endif

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
#ifdef TEG_ENABLE_CMSIS_FFT
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
  } else
#endif
  {
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

// Download the running configuration as a file. Secrets are redacted, so
// the export is safe to store or share; importing it back keeps whatever
// credentials are currently on the device.
FLASHMEM void api_config_export(Request &req, Response &res) {
  JsonDocument doc;
  configToJson(config, doc);
  redactSecrets(doc);
  doc["ExportedBy"] = TEG_GIT_HASH; // which firmware wrote this file
  res.set("Content-Type", "application/json");
  res.set("Content-Disposition", "attachment; filename=\"teg-config.json\"");
  serializeJson(doc, res);
}

FLASHMEM void api_presets_get(Request &req, Response &res) {
  JsonDocument doc;
  const bool ok = presetList(doc);
  doc["available"] = ok;
  res.set("Content-Type", "application/json");
  serializeJson(doc, res);
}

// Body {"name": "..."} for all three mutating preset endpoints
static bool presetNameFromBody(Request &req, Response &res, char *out, size_t size) {
  JsonDocument doc;
  if (deserializeJson(doc, req)) {
    res.sendStatus(400);
    return false;
  }
  const char *name = doc["name"] | "";
  if (!presetNameValid(name)) {
    res.status(400);
    res.set("Content-Type", "application/json");
    res.print(F("{\"error\":\"invalid preset name\"}"));
    return false;
  }
  snprintf(out, size, "%s", name);
  return true;
}

static void presetResult(Response &res, bool ok, const char *err) {
  res.status(ok ? 200 : 400);
  res.set("Content-Type", "application/json");
  JsonDocument out;
  out["ok"] = ok;
  out["error"] = ok ? "" : err;
  serializeJson(out, res);
}

FLASHMEM void api_presets_save(Request &req, Response &res) {
  if (!writeAuthorized(req)) {
    res.sendStatus(401);
    return;
  }
  if (otaInProgress()) {
    res.sendStatus(409);
    return;
  }
  char name[PresetNameMax + 1];
  if (!presetNameFromBody(req, res, name, sizeof(name))) {
    return;
  }
  const char *err = "";
  presetResult(res, presetSave(name, &err), err);
}

FLASHMEM void api_presets_load(Request &req, Response &res) {
  if (!writeAuthorized(req)) {
    res.sendStatus(401);
    return;
  }
  if (otaInProgress()) {
    res.sendStatus(409); // loading a preset reapplies PWM settings
    return;
  }
  char name[PresetNameMax + 1];
  if (!presetNameFromBody(req, res, name, sizeof(name))) {
    return;
  }
  const char *err = "";
  presetResult(res, presetLoad(name, &err), err);
}

// Import a settings file. Deliberately NOT the plain config POST: an
// imported file is not operator-authored, so secrets are always taken from
// the device and an incomplete document is rejected instead of defaulting
// safety sections off.
FLASHMEM void api_config_import(Request &req, Response &res) {
  if (!writeAuthorized(req)) {
    res.sendStatus(401);
    return;
  }
  if (otaInProgress()) {
    res.sendStatus(409);
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, req)) {
    res.sendStatus(400);
    return;
  }
  const char *err = "";
  const bool ok = configApplyDocument(doc, &err);
  presetResult(res, ok, err);
}

FLASHMEM void api_presets_delete(Request &req, Response &res) {
  if (!writeAuthorized(req)) {
    res.sendStatus(401);
    return;
  }
  if (otaInProgress()) {
    res.sendStatus(409); // uniform with the other mutating preset endpoints
    return;
  }
  char name[PresetNameMax + 1];
  if (!presetNameFromBody(req, res, name, sizeof(name))) {
    return;
  }
  const char *err = "";
  presetResult(res, presetDelete(name, &err), err);
}

FLASHMEM void api_ota_get(Request &req, Response &res) {
  JsonDocument doc;
  doc["inProgress"] = otaInProgress();
  doc["valid"] = otaImageVerified();
  doc["received"] = otaReceivedBytes();
  doc["size"] = otaImageSize();
  doc["error"] = otaLastError();
  doc["version"] = TEG_GIT_HASH;
  res.set("Content-Type", "application/json");
  serializeJson(doc, res);
}

void api_ota_post(Request &req, Response &res) {
  if (!writeAuthorized(req)) {
    // Drain the (multi-MB) body before answering: replying mid-upload makes
    // the browser see a connection reset instead of the 401, so the PIN
    // prompt would never appear
    while (req.available()) {
      req.read();
      serviceControlTasks(); // a rejected upload can still be multi-MB to drain
    }
    res.sendStatus(401);
    return;
  }
  const char *err = "";
  const bool ok = otaIngestStream(req, &err, &kickWatchdog);
  res.status(ok ? 200 : 400);
  res.set("Content-Type", "application/json");
  JsonDocument out;
  out["received"] = otaReceivedBytes();
  out["lines"] = otaLineCount();
  out["size"] = otaImageSize();
  out["valid"] = ok;
  out["error"] = err;
  serializeJson(out, res);
}

FLASHMEM void api_ota_commit(Request &req, Response &res) {
  if (!writeAuthorized(req)) {
    res.sendStatus(401);
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, req)) {
    res.sendStatus(400);
    return;
  }
  // The UI must echo the verified size back - a stale or mismatched confirm
  // cannot trigger a flash
  const uint32_t size = doc["size"] | 0U;
  if (!otaRequestCommit(size)) {
    res.sendStatus(409);
    return;
  }
  res.set("Content-Type", "application/json");
  res.print(F("{\"committing\":true}"));
  // The copy-down + reboot runs from loop() after this response flushes
}

FLASHMEM void api_ota_abort(Request &req, Response &res) {
  if (!writeAuthorized(req)) {
    res.sendStatus(401);
    return;
  }
  otaRequestAbort();
  res.set("Content-Type", "application/json");
  res.print(F("{\"aborted\":true,\"rebooting\":true}"));
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
  if (otaInProgress()) {
    res.sendStatus(409);
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
  if (otaInProgress()) {
    res.sendStatus(409); // uniform policy: no state changes during an OTA
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
    // A full 2MB ring takes seconds to stream, during which loop() does not run.
    // Kicking the watchdog alone would keep the device alive with every control loop
    // frozen - the PLL re-syncs after 250ms, the meter stops draining, thermal derate
    // stops updating - and nothing would report it.
    serviceControlTasks();
  }
}

FLASHMEM void api_fault_clear(Request &req, Response &res) {
  if (!writeAuthorized(req)) {
    res.sendStatus(401);
    return;
  }
  if (otaInProgress()) {
    res.sendStatus(409); // clearing would re-enable outputs mid-flash
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

// Event log. ?since=N returns everything newer than sequence N, so a client
// polls with the last seq it saw and never repeats or misses an entry;
// "gap" tells it entries were evicted before it caught up.
FLASHMEM void api_log(Request &req, Response &res) {
  char qbuf[16];
  uint32_t since = 0;
  if (req.query("since", qbuf, sizeof(qbuf))) {
    since = strtoul(qbuf, nullptr, 10);
  }
  const EventLog &log = eventLogRef();
  const uint32_t newest = eventLogNewestSeq(log);
  constexpr uint32_t MaxPerResponse = 64; // bound the response size
  uint32_t n = eventLogCountSince(log, since);
  bool skipped = eventLogGapSince(log, since);
  if (n > MaxPerResponse) {
    // Return the NEWEST entries, not the oldest: a first call (since=0) must
    // show what just happened, and a client that fell behind cares about now
    since = newest - MaxPerResponse;
    n = MaxPerResponse;
    skipped = true;
  }

  JsonDocument doc;
  doc["clockValid"] = eventClockValid();
  doc["ntpSynced"] = eventClockNtpSynced();
  doc["now"] = eventNowEpoch();
  doc["bootId"] = eventBootId(); // changes on reset: clients reset their cursor
  doc["newest"] = newest;
  doc["gap"] = skipped;
  JsonArray arr = doc["events"].to<JsonArray>();
  char iso[24];
  for (uint32_t i = 0; i < n; i++) {
    const EventEntry *e = eventLogAt(log, since, i);
    if (e == nullptr) {
      break;
    }
    JsonObject o = arr.add<JsonObject>();
    o["seq"] = e->seq;
    o["uptimeMs"] = e->uptimeMs;
    o["level"] = eventLevelName(e->level);
    // Points into the ring rather than copying: safe because nothing writes
    // the log between here and serialization (writeLog is never called from
    // an interrupt, and this handler runs to completion inside loop())
    o["text"] = e->text;
    if (formatIso8601(iso, sizeof(iso), e->epoch) && iso[0] != '\0') {
      o["time"] = iso;
    }
  }
  res.set("Content-Type", "application/json");
  serializeJson(doc, res);
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
  doc["ota"] = otaInProgress();
  doc["mtp"] = mtpEnabled();
  if (mtpEnabled()) {
    doc["mtpPaused"] = mtpPaused();
  }
  doc["ocLimit"] = config.CurrentLimit.Enabled;
  if (config.CurrentLimit.Enabled) {
    doc["ocMode"] = config.CurrentLimit.CycleByCycle ? "cbc" : "latched";
    doc["ocThresholdMv"] = acmpActualThresholdMv(); // DAC-quantized, as programmed
    doc["ocActive"] = acmpFaultPinActive();
    doc["ocTrips"] = acmpCbcTripCount();
    doc["ocTripsPerSec"] = acmpCbcTripsPerSec();
  }
  doc["isrCycles"] = vIsrCycles;
  doc["missedIsrCycles"] = vMissedIsrCycles; // carrier cycles the ISR failed to serve
  doc["thermalMissedCycles"] = thermalHarvestMissedCycles(); // of those, the last OneWire harvest
  doc["applyMicros"] = lastApplyMicros;
  doc["modMilliHz"] = modulationActualMilliHz();
  doc["indexMilli"] = modulationIndexNowMilli();
  doc["targetMilli"] = modulationIndexTargetMilli();
  doc["dtcmFree"] = getFreeMemory();
  doc["stackLowWater"] = getStackLowWater(); // the figure that reveals an overflow
  doc["ocramFree"] = freeram();
  doc["captureActive"] = captureActive();
  doc["captureFrozen"] = captureIsFrozen();
  doc["captureSamples"] = captureSampleCount();
  doc["mqttConnected"] = mqttConnected();
  doc["mqttPublishFailures"] = mqttPublishFailures();
  doc["mpptEnabled"] = config.Mppt.Enabled;
  if (config.Mppt.Enabled) {
    doc["mpptActive"] = mpptActive();
    doc["mpptIndexMilli"] = mpptIndexMilli();
    doc["mpptStepMilli"] = mpptStepMilli();
    doc["mpptDir"] = mpptDirection();
    doc["mpptDeltaMw"] = mpptLastDeltaMw();
  }
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
      (static_cast<uint32_t>(analogRead(config.Feedback.AnalogPin)) * config.Feedback.FullScaleMillivolts) /
      AdcCountFullScale; // 12-bit, same as capture - see capture.h
  }
  doc["streamUnderruns"] = waveformStreamUnderruns();
  doc["derateMilli"] = thermalDerateMilliNow();
  doc["hotDeciC"] = thermalHotDeciC();   // INT16_MIN = unavailable
  doc["coldDeciC"] = thermalColdDeciC();
  doc["chipDeciC"] = thermalChipDeciC();
  res.set("Content-Type", "application/json");
  serializeJson(doc, res);
}
