#include "web_handlers.h"
#include "config_json.h"
#include "config_serde.h"
#include "pwm_utils.h"
#include "utils.h"
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
  app.get("/pico.min.css", &serve_pico_css);
  app.get("/api/config", &api_config_get);
  app.post("/api/config", &api_config_post);
  app.get("/api/status", &api_status);
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

// When a write PIN is configured, POSTs must carry it in X-Auth-Pin
static bool writeAuthorized(Request &req) {
  if (config.Security.WritePin[0] == '\0') {
    return true;
  }
  const char *provided = req.get("X-Auth-Pin");
  return provided != nullptr && strcmp(provided, config.Security.WritePin) == 0;
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
  if (spwmActive()) {
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

void api_status(Request &req, Response &res) {
  JsonDocument doc;
  doc["uptimeMs"] = millis();
  doc["active"] = spwmActive();
  doc["fault"] = vFaultTripped;
  doc["isrCycles"] = vIsrCycles;
  doc["applyMicros"] = lastApplyMicros;
  doc["modMilliHz"] = modulationActualMilliHz();
  doc["indexMilli"] = modulationIndexNowMilli();
  doc["targetMilli"] = modulationIndexTargetMilli();
  doc["dtcmFree"] = getFreeMemory();
  doc["ocramFree"] = freeram();
  res.set("Content-Type", "application/json");
  serializeJson(doc, res);
}
