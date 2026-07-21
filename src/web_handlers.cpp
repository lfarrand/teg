#include "web_handlers.h"
#include "config_json.h"
#include "config_fields.h"
#include "config_serde.h"
#include "pwm_utils.h"
#include "utils.h"
#include "web_assets.h"
#include <ArduinoJson.h>

extern Application app;
extern MainConfig config;
extern EthernetServer server;
extern const char* filename;

// Set by the update handlers after applying changes to the hardware; loop()
// persists the config to SD so the HTTP response never waits on the card.
volatile bool configSaveNeeded = false;

// Last measured hardware-apply duration, reported by /api/status
static volatile uint32_t lastApplyMicros = 0;

FLASHMEM void configureWebServer() {
  app.get("/", &index);
  app.get("/index.html", &index);
  app.get("/pico.min.css", &serve_pico_css);
  app.get("/api/config", &api_config_get);
  app.post("/api/config", &api_config_post);
  app.get("/api/status", &api_status);
  // Legacy server-rendered pages, kept during the SPA transition
  app.get("/settings/pwm", &settings_pwm);
  app.post("/settings/pwm/update", &settings_pwm_update);
  app.get("/settings/pwm-timer", &settings_pwm_timer);
  app.post("/settings/pwm-timer/update", &settings_pwm_timer_update);
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

FLASHMEM void serve_pico_css(Request &req, Response &res) {
  sendAsset(res, "/pico.min.css", "max-age=86400"); // content-stable, cache a day
}

FLASHMEM void api_config_get(Request &req, Response &res) {
  JsonDocument doc;
  configToJson(config, doc);
  res.set("Content-Type", "application/json");
  serializeJson(doc, res);
}

FLASHMEM void api_config_post(Request &req, Response &res) {
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, req);
  if (err) {
    res.set("Content-Type", "application/json");
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

void processWebServer() {
  EthernetClient client = server.available();
  if (client) {
    app.process(&client);
    client.stop();
  }
}

FLASHMEM void index(Request &req, Response &res) {
  sendAsset(res, "/index.html", "no-cache");
}

FLASHMEM void settings_pwm(Request &req, Response &res) {
  char buf[32768];
  snprintf(buf, sizeof(buf), PwmSettingsPageTemplate,
           config.Pwm.Tm1.Sm13.ChannelA.OnPeriodMicroseconds,
           config.Pwm.Tm1.Sm13.ChannelB.OnPeriodMicroseconds,
           config.Pwm.Tm1.Sm13.PwmFrequency,
           config.Pwm.Tm2.Sm20.PwmFrequency,
           config.Pwm.Tm2.Sm21.PwmFrequency,
           config.Pwm.Tm2.Sm22.PwmFrequency,
           config.Pwm.Tm2.Sm23.PwmFrequency,
           config.Pwm.Tm3.Sm31.PwmFrequency,
           config.Pwm.Tm4.Sm40.PwmFrequency,
           config.Pwm.Tm4.Sm41.PwmFrequency,
           config.Pwm.Tm4.Sm42.PwmFrequency,
           config.Pwm.Tm1.Sm13.DeadTime,
           config.Pwm.Tm2.Sm20.DeadTime,
           config.Pwm.Tm2.Sm21.DeadTime,
           config.Pwm.Tm2.Sm22.DeadTime,
           config.Pwm.Tm2.Sm23.DeadTime,
           config.Pwm.Tm3.Sm31.DeadTime,
           config.Pwm.Tm4.Sm40.DeadTime,
           config.Pwm.Tm4.Sm41.DeadTime,
           config.Pwm.Tm4.Sm42.DeadTime,
           config.Pwm.Tm1.Sm13.ChannelA.DutyCycle,
           config.Pwm.Tm1.Sm13.ChannelB.DutyCycle,
           config.Pwm.Tm2.Sm20.ChannelA.DutyCycle,
           config.Pwm.Tm2.Sm20.ChannelB.DutyCycle,
           config.Pwm.Tm2.Sm21.ChannelA.DutyCycle,
           config.Pwm.Tm2.Sm22.ChannelA.DutyCycle,
           config.Pwm.Tm2.Sm22.ChannelB.DutyCycle,
           config.Pwm.Tm2.Sm23.ChannelA.DutyCycle,
           config.Pwm.Tm2.Sm23.ChannelB.DutyCycle,
           config.Pwm.Tm3.Sm31.ChannelA.DutyCycle,
           config.Pwm.Tm3.Sm31.ChannelB.DutyCycle,
           config.Pwm.Tm4.Sm40.ChannelA.DutyCycle,
           config.Pwm.Tm4.Sm41.ChannelA.DutyCycle,
           config.Pwm.Tm4.Sm42.ChannelA.DutyCycle,
           config.Pwm.Tm4.Sm42.ChannelB.DutyCycle,
           config.Pwm.Tm2.Sm21.ChannelA.PhaseShift,
           config.Pwm.Tm2.Sm22.ChannelA.PhaseShift,
           config.Pwm.Tm2.Sm22.ChannelB.PhaseShift,
           config.Pwm.Tm2.Sm23.ChannelA.PhaseShift,
           config.Pwm.Tm2.Sm23.ChannelB.PhaseShift,
           config.Pwm.Tm4.Sm41.ChannelA.PhaseShift,
           config.Pwm.Tm4.Sm42.ChannelA.PhaseShift,
           config.Pwm.Tm4.Sm42.ChannelB.PhaseShift,
           config.Pwm.PrintRegs ? "Yes" : "No",
           config.Pwm.SyncPwm ? "Yes" : "No",
           config.Pwm.Tm2.UseSpwm ? "Yes" : "No",
           config.Pwm.Tm2.SpwmCarrierFrequency,
           config.Pwm.Tm2.SpwmModulationFrequency,
           config.Pwm.Tm2.ModulationScheme,
           config.Pwm.Tm2.ModulationIndexMilli,
           config.Pwm.Tm2.ModulationCells,
           config.Pwm.Tm2.CarrierDisposition,
           config.Pwm.Tm2.ReferenceWaveform,
           config.Pwm.Tm2.DpwmVariant,
           config.Pwm.Tm2.DpwmClampAngleDeg,
           config.Pwm.Tm2.CarrierDitherMode,
           config.Pwm.Tm2.CarrierDitherPercent,
           config.Pwm.Tm2.NearestLevelModulation ? "Yes" : "No",
           config.Pwm.Tm2.DeadTimeCompensation ? "Yes" : "No",
           config.Pwm.Tm2.SoftStartMs,
           config.Feedback.Enabled ? "Yes" : "No",
           config.Feedback.SetpointMillivolts,
           config.Feedback.KpMilli,
           config.Feedback.KiMilli,
           config.FaultProtection.Enabled ? "Yes" : "No",
           config.FaultProtection.Pin,
           config.FaultProtection.ActiveHigh ? "Yes" : "No",
           config.AsymmetricInduction.IsEnabled ? "Yes" : "No",
           config.Pwm.Tm4.Sm42.PwmFrequency,
           config.Pwm.Tm4.Sm42.ChannelA.DutyCycle,
           config.AsymmetricInduction.PreShiftNanos,
           config.AsymmetricInduction.PostShiftNanos);
  res.set("Content-Type", "text/html");
  res.print(buf);
}

FLASHMEM void settings_pwm_update(Request &req, Response &res) {
  if (req.method() != Request::MethodType::POST) {
    res.set("Content-Type", "text/plain");
    res.printP("Method Not Allowed");
    res.sendStatus(405);
    res.flush();
    res.end();
  }
  else {
    const bool spwmWasEnabled = spwmActive();
    if (spwmWasEnabled) {
      disablePwmInterrupts();
    }

    // Snapshot (memcpy so padding compares equal) to detect which timers changed
    MainConfig previous;
    memcpy(&previous, &config, sizeof(previous));

    while (req.left()) {
      char value[100];
      char name[50];
      if (!req.form(name, 50, value, 100)) {
        if (spwmWasEnabled) {
          enablePwmInterrupts();
        }
        res.sendStatus(400);
        res.flush();
        res.end();
        return;
      }

      applyConfigFormField(config, name, value);
    }

    // Apply to hardware first: the register writes are buffered and take
    // effect at the next PWM reload, i.e. within one PWM period.
    const uint32_t applyStart = ARM_DWT_CYCCNT;

    applyPwmConfig(previous);

    if (spwmActive()) {
      attachModule2PwmInterruptVectors(); // required if SPWM was off at boot
      enablePwmInterrupts();
    }

    const uint32_t applyMicros = (ARM_DWT_CYCCNT - applyStart) / (F_CPU_ACTUAL / 1000000);
    lastApplyMicros = applyMicros;

    // Redirect
    res.set("Location", "/settings/pwm");
    res.sendStatus(302);
    res.flush();
    res.end();

    char strBuf[48];
    snprintf(strBuf, sizeof(strBuf), "Settings applied in %luus", applyMicros);
    writeLog(strBuf);

    // Persist from loop() so the response doesn't wait on the SD card
    configSaveNeeded = true;
  }
}

FLASHMEM void settings_pwm_timer(Request &req, Response &res) {
  char buf[8192]; // template is ~3.2 KB; formatted page comfortably fits
  snprintf(buf, sizeof(buf),
         PwmTimerSettingsPageTemplate,
         config.Pwm.Tm1.Sm13.ChannelA.OnPeriodMicroseconds,
         config.Pwm.Tm1.Sm13.ChannelA.Enabled ? " checked" : "",
         config.Pwm.Tm1.Sm13.ChannelB.OnPeriodMicroseconds,
         config.Pwm.Tm1.Sm13.ChannelB.Enabled ? " checked" : "");
  res.set("Content-Type", "text/html");
  res.print(buf);
  res.flush();
  res.end();
}

FLASHMEM void settings_pwm_timer_update(Request &req, Response &res) {
  if (req.method() != Request::MethodType::POST) {
    res.set("Content-Type", "text/plain");
    res.printP("Method Not Allowed");
    res.sendStatus(405);
    res.flush();
    res.end();
  } else {

    config.Pwm.Tm1.Sm13.ChannelA.Enabled = false;
    config.Pwm.Tm1.Sm13.ChannelB.Enabled = false;

    while (req.left()) {
      char value[100];
      char name[50];
      if (!req.form(name, 50, value, 100)) {
        return res.sendStatus(400);
      }

      applyTimerFormField(config, name, value);
    }

    // Redirect
    res.set("Location", "/settings/pwm-timer");
    res.sendStatus(302);
    res.flush();
    res.end();

    // Persist from loop() so the response doesn't wait on the SD card
    configSaveNeeded = true;
  }
}

void DumpText(EthernetClient &client) {
}