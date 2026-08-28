#include "web_handlers.h"
#include "config_json.h"
#include "config_serde.h"
#include "pin_ownership.h"
#include "pwm_utils.h"
#include "capture.h"
#include "meter.h"
#include "power_monitor.h"
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
#include "spectrum_wire.h"
#ifdef TEG_ENABLE_CMSIS_FFT
#include <arm_math.h>
#endif
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
static Router apiRouter;

// Set by the update handler after applying changes to the hardware; loop()
// persists the config to SD so the HTTP response never waits on the card.
volatile bool configSaveNeeded = false;

// Last measured hardware-apply duration, reported by /api/status
static volatile uint32_t lastApplyMicros = 0;

// aWOT only exposes headers that were registered before processing
static char authPinHeader[sizeof(SecurityConfig{}.WritePin) + 4];
static char hostHeader[64];
static char originHeader[96];
static bool writeAuthorized(Request &req);
static void requireApiAuthorization(Request &req, Response &res);

FLASHMEM void configureWebServer() {
  app.header("X-Auth-Pin", authPinHeader, sizeof(authPinHeader));
  app.header("Host", hostHeader, sizeof(hostHeader));
  app.header("Origin", originHeader, sizeof(originHeader));
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
  // Two budgets, and both are needed. setWriteBudget bounds time with the peer
  // accepting NOTHING, and resets on every byte so an honest slow client is never
  // truncated. On its own it bounds nothing: a client that takes one byte every 2.9s
  // resets it for ever. setWriteTotalBudget is the absolute ceiling on one response,
  // and is what actually caps the damage. serviceControlTasks keeps the PWM, PLL,
  // meter and watchdog alive throughout, but everything else in loop() - MQTT, NTP,
  // metrics, the deferred OTA commit, the config persist, the OLED - is stalled for
  // the duration, so the ceiling is what stops one GET from parking those for days.
  Response::setWriteBudget(3000);
  Response::setWriteTotalBudget(30000);

  app.get("/", &index);
  app.get("/index.html", &index);
  app.get("/stats.html", &serve_stats);
  app.get("/pico.min.css", &serve_pico_css);
  // Mount a dedicated API router so one middleware protects every current and
  // future diagnostic GET. Static assets stay public, but status, logs, crash
  // reports, captures and configuration no longer disclose operational data or
  // credentials-by-context to any host that can reach the LAN.
  apiRouter.use(&requireApiAuthorization);
  apiRouter.get("/config", &api_config_get);
  apiRouter.post("/config", &api_config_post);
  apiRouter.get("/status", &api_status);
  apiRouter.get("/capture/raw", &api_capture_raw);
  apiRouter.get("/capture", &api_capture);
  apiRouter.get("/scope", &api_scope_get);
  apiRouter.post("/scope/arm", &api_scope_arm);
  apiRouter.post("/scope/release", &api_scope_release);
  apiRouter.get("/waveform", &api_waveform_get);
  apiRouter.post("/waveform", &api_waveform_post);
  apiRouter.post("/fault/clear", &api_fault_clear);
  apiRouter.get("/crash", &api_crash);
  apiRouter.get("/log", &api_log);
  apiRouter.get("/spectrum", &api_spectrum);
  apiRouter.get("/config/export", &api_config_export);
  apiRouter.post("/config/import", &api_config_import);
  apiRouter.get("/presets", &api_presets_get);
  apiRouter.post("/presets/save", &api_presets_save);
  apiRouter.post("/presets/load", &api_presets_load);
  apiRouter.post("/presets/delete", &api_presets_delete);
#ifdef TEG_ENABLE_UNSAFE_LAB_OTA
  apiRouter.post("/ota/commit", &api_ota_commit);
  apiRouter.post("/ota/abort", &api_ota_abort);
  apiRouter.get("/ota", &api_ota_get);
  apiRouter.post("/ota", &api_ota_post);
#endif
  app.use("/api", &apiRouter);
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

FLASHMEM void index(Request &, Response &res) {
  sendAsset(res, "/index.html", "no-cache");
}

FLASHMEM void serve_stats(Request &, Response &res) {
  sendAsset(res, "/stats.html", "no-cache");
}

FLASHMEM void serve_pico_css(Request &, Response &res) {
  sendAsset(res, "/pico.min.css", "max-age=86400"); // content-stable, cache a day
}

FLASHMEM static void writeConfigJson(Response &res, bool attach) {
  JsonDocument doc;
  configToJson(config, doc);
  redactSecrets(doc); // the Influx token and write PIN never leave the device
  if (attach) {
    doc["ExportedBy"] = TEG_GIT_HASH; // which firmware wrote this file
    res.set("Content-Disposition", "attachment; filename=\"teg-config.json\"");
  }
  res.set("Content-Type", "application/json");
  serializeJson(doc, res);
}

FLASHMEM void api_config_get(Request &req, Response &res) {
  char qbuf[8];
  const bool attach = req.query("download", qbuf, sizeof(qbuf)) && qbuf[0] == '1';
  writeConfigJson(res, attach);
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

namespace {
struct AuthPeer {
  uint32_t ip = 0;
  uint32_t windowStart = 0;
  uint32_t blockedUntil = 0;
  uint8_t failures = 0;
  bool used = false;
};
AuthPeer authPeers[4];
constexpr uint32_t AuthWindowMs = 60000;
constexpr uint32_t AuthBlockMs = 60000;
constexpr uint8_t AuthFailuresBeforeBlock = 5;

static uint32_t requestPeerIp(Request &req) {
  EthernetClient *client = static_cast<EthernetClient *>(req.stream());
  const IPAddress ip = client->remoteIP();
  return (static_cast<uint32_t>(ip[0]) << 24) | (static_cast<uint32_t>(ip[1]) << 16) |
         (static_cast<uint32_t>(ip[2]) << 8) | ip[3];
}

static uint32_t packIp(const IPAddress &ip) {
  return (static_cast<uint32_t>(ip[0]) << 24) | (static_cast<uint32_t>(ip[1]) << 16) |
         (static_cast<uint32_t>(ip[2]) << 8) | ip[3];
}

static bool requestPeerIsLocal(Request &req) {
  const uint32_t mask = packIp(Ethernet.subnetMask());
  if (mask == 0) return false;
  return (requestPeerIp(req) & mask) == (packIp(Ethernet.localIP()) & mask);
}

static AuthPeer &authPeerFor(uint32_t ip, uint32_t now) {
  AuthPeer *oldest = &authPeers[0];
  for (AuthPeer &peer : authPeers) {
    if (peer.used && peer.ip == ip) return peer;
    if (!peer.used) {
      peer.used = true;
      peer.ip = ip;
      peer.windowStart = now;
      return peer;
    }
    if (now - peer.windowStart > now - oldest->windowStart) oldest = &peer;
  }
  *oldest = AuthPeer{};
  oldest->used = true;
  oldest->ip = ip;
  oldest->windowStart = now;
  return *oldest;
}

static bool authPeerBlocked(Request &req) {
  const uint32_t now = millis();
  AuthPeer &peer = authPeerFor(requestPeerIp(req), now);
  return peer.blockedUntil != 0 && static_cast<int32_t>(now - peer.blockedUntil) < 0;
}

static bool authorityAllowed() {
  if (hostHeader[0] == '\0') return false; // HTTP/1.1 requires Host
  char localIp[20];
  snprintf(localIp, sizeof(localIp), "%u.%u.%u.%u", Ethernet.localIP()[0],
           Ethernet.localIP()[1], Ethernet.localIP()[2], Ethernet.localIP()[3]);
  char localName[40];
  snprintf(localName, sizeof(localName), "%s.local", networkHostname());

  char authority[sizeof(hostHeader)];
  copyConfigString(authority, sizeof(authority), hostHeader);
  char *port = strchr(authority, ':');
  if (port != nullptr) *port = '\0';
  const bool hostOk = strcmp(authority, localIp) == 0 ||
                      strcasecmp(authority, networkHostname()) == 0 ||
                      strcasecmp(authority, localName) == 0;
  if (!hostOk) return false;
  if (originHeader[0] == '\0') return true; // curl/native client
  constexpr char prefix[] = "http://";
  if (strncmp(originHeader, prefix, sizeof(prefix) - 1) != 0) return false;
  // A browser Origin contains only scheme + authority. Requiring it to match
  // Host blocks cross-site and DNS-rebinding writes without pretending HTTP
  // provides confidentiality.
  return strcmp(originHeader + sizeof(prefix) - 1, hostHeader) == 0;
}
} // namespace

// Fail closed. An empty PIN means entropy/persistence provisioning failed; it
// must never turn a missing credential into unauthenticated control.
static bool writeAuthorized(Request &req) {
  if (config.Security.WritePin[0] == '\0') {
    return false;
  }
  const uint32_t now = millis();
  const uint32_t ip = requestPeerIp(req);
  AuthPeer &peer = authPeerFor(ip, now);
  if (peer.blockedUntil != 0 && static_cast<int32_t>(now - peer.blockedUntil) < 0) {
    return false;
  }
  if (pinMatches(req.get("X-Auth-Pin"))) {
    peer.failures = 0;
    peer.blockedUntil = 0;
    peer.windowStart = now;
    return true;
  }
  if (now - peer.windowStart >= AuthWindowMs) {
    peer.windowStart = now;
    peer.failures = 0;
  }
  ++peer.failures;
  if (peer.failures == 1 || peer.failures >= AuthFailuresBeforeBlock) {
    char message[96];
    snprintf(message, sizeof(message), "Web auth failed from %u.%u.%u.%u%s",
             static_cast<unsigned>(ip >> 24), static_cast<unsigned>((ip >> 16) & 0xFF),
             static_cast<unsigned>((ip >> 8) & 0xFF), static_cast<unsigned>(ip & 0xFF),
             peer.failures >= AuthFailuresBeforeBlock ? "; blocked 60s" : "");
    writeLogLevel(EventWarn, message);
  }
  if (peer.failures >= AuthFailuresBeforeBlock) {
    peer.blockedUntil = now + AuthBlockMs;
  }
  return false;
}

static void requireApiAuthorization(Request &req, Response &res) {
  if (!requestPeerIsLocal(req) || !authorityAllowed()) {
    res.sendStatus(403);
    return;
  }
  if (!writeAuthorized(req)) {
    res.sendStatus(authPeerBlocked(req) ? 429 : 401);
  }
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
  if (!configDocComplete(doc)) {
    res.status(400);
    res.set("Content-Type", "application/json");
    res.print(F("{\"error\":\"incomplete or incompatible configuration document\"}"));
    return;
  }

  MainConfig previous;
  memcpy(&previous, &config, sizeof(previous));

  MainConfig candidate;
  configFromJson(doc, candidate);
  preserveSecrets(candidate, previous); // empty secret in the POST = keep current
  if (const char *reason = configApiRejectReason(candidate)) {
    res.status(422);
    res.set("Content-Type", "application/json");
    JsonDocument out;
    out["error"] = reason;
    serializeJson(out, res);
    return;
  }
  if (validateConfig(candidate)) {
    writeLog("Invalid values in config; corrected");
  }
  PinValidationResult pinResult;
  if (!validatePinOwnership(candidate, &pinResult)) {
    res.status(422);
    res.set("Content-Type", "application/json");
    JsonDocument out;
    out["error"] = "pin ownership conflict or invalid pin";
    out["pin"] = pinResult.pin;
    out["existing"] = pinRoleName(pinResult.existing);
    out["requested"] = pinRoleName(pinResult.requested);
    serializeJson(out, res);
    return;
  }

  disablePwmInterrupts();
  memcpy(&config, &candidate, sizeof(config));

  // Apply to hardware first: register writes are buffered and take effect at
  // the next PWM reload, i.e. within one PWM period
  const uint32_t applyStart = ARM_DWT_CYCCNT;
  applyPwmConfig(previous);
  // Not while a trip is latched (a refused clear): the modulation ISR would
  // drive duty updates into fault-masked submodules
  if (pwmInterruptRequired() && !vFaultTripped) {
    attachModule2PwmInterruptVectors();
    enablePwmInterrupts();
  }
  lastApplyMicros = (ARM_DWT_CYCCNT - applyStart) / (F_CPU_ACTUAL / 1000000);

  res.set("Content-Type", "application/json");
  JsonDocument out;
  out["applyMicros"] = lastApplyMicros;
  out["persistPending"] = true;
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

  const uint8_t activeWave = config.Pwm.Tm2.ReferenceWaveform;
  if (spwmActive() && (activeWave == RefWaveCustom || activeWave == RefWaveSequence)) {
    res.status(409);
    res.set("Content-Type", "application/json");
    res.print(F("{\"error\":\"disable custom/sequence modulation before replacing its waveform\"}"));
    return;
  }

  const char *err = "";
  // Streams text or TEGW binary straight into the PSRAM store; multi-MB
  // uploads take seconds, so the watchdog is serviced during the transfer.
  //
  // Upload goes to a separate file and a currently generated custom waveform is
  // rejected above, so supervisory tasks can continue throughout the transfer.
  const int bodyBytes = req.left();
  if (bodyBytes <= 0 ||
      !waveformApplyStream(req, static_cast<uint32_t>(bodyBytes), &err,
                           &serviceControlTasks)) {
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

FLASHMEM void api_waveform_get(Request &, Response &res) {
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
  constexpr uint32_t SpectrumDefaultPoints = 1024;
  char qbuf[12];
  uint32_t points = SpectrumDefaultPoints;
  if (req.query("points", qbuf, sizeof(qbuf))) {
    points = strtoul(qbuf, nullptr, 10);
  }
  if (points < 256 || points > SpectrumMaxPoints || (points & (points - 1)) != 0) {
    points = SpectrumDefaultPoints;
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
  const bool wantBin = req.query("format", qbuf, sizeof(qbuf)) && strcmp(qbuf, "bin") == 0;

  const uint32_t sampleHz = config.Pwm.Tm2.SpwmCarrierFrequency;
  const bool available = captureActive() && captureCopyRecent(fftSamples, points) != 0;
  const float binHz = static_cast<float>(sampleHz) / points;

  float *mag = nullptr;
  uint32_t computeMicros = 0;
  float fund = 0.0f;
  float fundamentalHz = 0.0f;
  float thd = 0.0f;
  if (available) {
    const uint32_t computeStart = ARM_DWT_CYCCNT;
    const uint32_t halfN = points / 2;
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
    computeMicros = (ARM_DWT_CYCCNT - computeStart) / (F_CPU_ACTUAL / 1000000);
    const uint32_t fundBin = findFundamentalBin(mag, halfN);
    fund = harmonicPeak(mag, halfN, fundBin);
    fundamentalHz = binHz * fundBin;
    thd = thdPercent(mag, halfN, fundBin);
  }

  if (wantBin) {
    uint8_t buf[SpectrumWireMaxBody];
    const SpectrumWireFields f = {available, sampleHz, points, computeMicros,
                                  binHz, fundamentalHz, thd};
    const size_t n = spectrumWirePack(buf, sizeof(buf), f, mag, fund);
    res.set("Content-Type", "application/octet-stream");
    res.write(buf, n);
    return;
  }

  res.set("Content-Type", "application/json");
  JsonDocument doc;
  doc["sampleHz"] = sampleHz;
  doc["points"] = points;
  if (!available) {
    doc["available"] = false;
    serializeJson(doc, res);
    return;
  }
  doc["engine"] = useCmsis ? "cmsis" : "portable";
  doc["computeMicros"] = computeMicros;
  doc["available"] = true;
  doc["binHz"] = binHz;
  doc["fundamentalHz"] = fundamentalHz;
  doc["thdPercent"] = thd;

  const uint16_t outBins = spectrumWireBinCount(points, true);
  JsonArray arr = doc["mag"].to<JsonArray>();
  for (uint16_t i = 0; i < outBins; i++) {
    arr.add(spectrumWireQuantize(mag[i], fund) / SpectrumWireScale);
  }
  serializeJson(doc, res);
}

constexpr uint32_t MaxCaptureBins = 600;
constexpr uint32_t MaxCaptureCount = 32768;

void api_capture(Request &req, Response &res) {
  char qbuf[16];
  uint32_t count = 20000, bins = MaxCaptureBins;
  if (req.query("count", qbuf, sizeof(qbuf))) {
    count = strtoul(qbuf, nullptr, 10);
  }
  if (count > MaxCaptureCount) {
    count = MaxCaptureCount;
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
FLASHMEM void api_config_export(Request &, Response &res) {
  writeConfigJson(res, true);
}

FLASHMEM void api_presets_get(Request &, Response &res) {
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

#ifdef TEG_ENABLE_UNSAFE_LAB_OTA
FLASHMEM void api_ota_get(Request &, Response &res) {
  JsonDocument doc;
  doc["enabled"] = otaReleaseEnabled();
  if (!otaReleaseEnabled()) {
    doc["reason"] = "disabled: updater is unsigned and single-slot; use USB bootloader";
  }
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
  if (!otaReleaseEnabled()) {
    res.status(501);
    res.set("Content-Type", "application/json");
    res.print(F("{\"error\":\"OTA disabled in production; use the Teensy USB bootloader\"}"));
    return;
  }
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
  const int bodyBytes = req.left();
  const bool ok = bodyBytes > 0 &&
                  otaIngestStream(req, static_cast<uint32_t>(bodyBytes), &err,
                                  &serviceControlTasks);
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
  if (!otaReleaseEnabled()) {
    res.sendStatus(501);
    return;
  }
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
  if (!otaReleaseEnabled()) {
    res.sendStatus(501);
    return;
  }
  if (!writeAuthorized(req)) {
    res.sendStatus(401);
    return;
  }
  otaRequestAbort();
  res.set("Content-Type", "application/json");
  res.print(F("{\"aborted\":true,\"rebooting\":true}"));
}
#endif

static const char *scopeStateName(uint8_t s) {
  switch (s) {
    case ScopeArmed: return "armed";
    case ScopeTriggered: return "triggered";
    case ScopeComplete: return "complete";
    default: return "idle";
  }
}

FLASHMEM void api_scope_get(Request &, Response &res) {
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
  const bool released = clearFaultTrip(true);
  res.set("Content-Type", "application/json");
  JsonDocument out;
  out["fault"] = vFaultTripped;
  out["restartInhibit"] = pwmRestartInhibited();
  out["hardwareInhibit"] = pwmHardwareInhibited();
  out["provisioningInhibit"] = pwmProvisioningInhibited();
  out["outputsInhibited"] = pwmOutputInhibited();
  out["released"] = released;
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

FLASHMEM void api_crash(Request &, Response &res) {
  res.set("Content-Type", "text/plain");
  const char *text = crashReportText();
  res.print(text[0] != '\0' ? text : "none");
}

FLASHMEM void api_status(Request &req, Response &res) {
  char qbuf[8];
  const bool lite = req.query("lite", qbuf, sizeof(qbuf)) && qbuf[0] == '1';

  JsonDocument doc;
  doc["uptimeMs"] = millis();
  if (!lite) {
    doc["version"] = TEG_GIT_HASH;
    doc["resetCause"] = resetCauseString();
    doc["hostname"] = networkHostname();
    doc["crash"] = crashReportText()[0] != '\0';
  }
  doc["active"] = spwmActive();
  doc["fault"] = vFaultTripped;
  doc["restartInhibit"] = pwmRestartInhibited();
  doc["hardwareInhibit"] = pwmHardwareInhibited();
  doc["provisioningInhibit"] = pwmProvisioningInhibited();
  if (!lite) {
    doc["outputsInhibited"] = pwmOutputInhibited();
  }
  doc["configPersistPending"] = configSaveNeeded;
  if (!lite) {
    doc["pwmConfigValid"] = pwmConfigurationValid();
    doc["ota"] = otaInProgress();
  }
  doc["otaEnabled"] = otaReleaseEnabled();
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
  if (!lite) {
    doc["missedIsrCycles"] = vMissedIsrCycles; // carrier cycles the ISR failed to serve
    doc["thermalMissedCycles"] = thermalHarvestMissedCycles(); // of those, the last OneWire harvest
  }
  doc["applyMicros"] = lastApplyMicros;
  doc["modMilliHz"] = modulationActualMilliHz();
  doc["indexMilli"] = modulationIndexNowMilli();
  doc["targetMilli"] = modulationIndexTargetMilli();
  doc["dtcmFree"] = getFreeMemory();
  if (!lite) {
    doc["stackLowWater"] = getStackLowWater(); // the figure that reveals an overflow
  }
  doc["ocramFree"] = freeram();
  if (!lite) {
    doc["captureActive"] = captureActive();
    doc["captureFrozen"] = captureIsFrozen();
    doc["captureSamples"] = captureSampleCount();
    doc["captureVoltageMisses"] = captureVoltageMissCount();
    doc["captureCurrentMisses"] = captureCurrentMissCount();
  }
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
  if (!lite) {
    const MeterReadings meter = meterReadings();
    doc["meterActive"] = meter.valid;
    if (meter.valid) {
      doc["powerMw"] = meter.powerMw;
      doc["vrmsMv"] = meter.vrmsMv;
      doc["irmsMa"] = meter.irmsMa;
      doc["pfMilli"] = meter.pfMilli;
      doc["energyMwh"] = meterEnergyMwh();
    }
    // Aux power monitor: the driver board's own supply telemetry (INA226 over
    // Wire2 plus eFuse PG/IMON taps)
    doc["auxMonEnabled"] = config.PowerMon.Enabled;
    if (config.PowerMon.Enabled) {
      const PowerMonReadings aux = powerMonReadings();
      doc["auxMonOnline"] = aux.valid;
      if (aux.valid) {
        doc["auxBusMv"] = aux.busMv;
        doc["auxCurrentMa"] = aux.currentMa;
        doc["auxPowerMw"] = aux.powerMw;
        doc["auxEnergyMwh"] = powerMonEnergyMwh();
        doc["auxPeakMa"] = powerMonPeakMa();
        if (aux.imonMa >= 0) {
          doc["auxImonMa"] = aux.imonMa;
        }
      }
      if (config.PowerMon.PgEfusePin != 255) {
        doc["auxPgEfuse"] = aux.pgEfuse;
      }
      if (config.PowerMon.PgBuckPin != 255) {
        doc["auxPgBuck"] = aux.pgBuck;
      }
      doc["auxAlert"] = aux.alert;
      doc["auxAlertCount"] = aux.alertCount;
      doc["auxPgEdgeCount"] = aux.pgEdgeCount;
      doc["auxCommsErrors"] = aux.commsErrors;
    }
    // Measured feedback voltage: synchronous capture mean when available,
    // otherwise a direct 12-bit read of the configured pin
    if (captureActive() && !captureIsFrozen()) {
      doc["feedbackMv"] = (captureMeanRaw(64) * config.Feedback.FullScaleMillivolts) / AdcCountFullScale;
    } else {
      doc["feedbackMv"] =
        (static_cast<uint32_t>(analogRead(config.Feedback.AnalogPin)) * config.Feedback.FullScaleMillivolts) /
        AdcCountFullScale; // 12-bit, same as capture - see capture.h
    }
    doc["streamUnderruns"] = waveformStreamUnderruns();
  }
  doc["derateMilli"] = thermalDerateMilliNow();
  doc["hotDeciC"] = thermalHotDeciC();   // INT16_MIN = unavailable
  doc["coldDeciC"] = thermalColdDeciC();
  doc["chipDeciC"] = thermalChipDeciC();
  res.set("Content-Type", "application/json");
  serializeJson(doc, res);
}
