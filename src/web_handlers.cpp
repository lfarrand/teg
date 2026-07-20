#include "web_handlers.h"
#include "config_json.h"
#include "config_fields.h"
#include "pwm_utils.h"
#include "utils.h"

extern Application app;
extern MainConfig config;
extern EthernetServer server;
extern const char* filename;

// Set by the update handlers after applying changes to the hardware; loop()
// persists the config to SD so the HTTP response never waits on the card.
volatile bool configSaveNeeded = false;

FLASHMEM void configureWebServer() {
  app.get("/", &index);
  app.get("/settings/pwm", &settings_pwm);
  app.post("/settings/pwm/update", &settings_pwm_update);
  app.get("/settings/pwm-timer", &settings_pwm_timer);
  app.post("/settings/pwm-timer/update", &settings_pwm_timer_update);
}

void processWebServer() {
  EthernetClient client = server.available();
  if (client) {
    app.process(&client);
    client.stop();
  }
}

FLASHMEM void index(Request &req, Response &res) {
  res.print("Welcome to PWM Control System");
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
    const bool spwmWasEnabled = config.Pwm.Tm2.UseSpwm;
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
    applyPwmConfig(previous);

    if (config.Pwm.Tm2.UseSpwm) {
      attachModule2PwmInterruptVectors(); // required if SPWM was off at boot
      enablePwmInterrupts();
    }

    // Redirect
    res.set("Location", "/settings/pwm");
    res.sendStatus(302);
    res.flush();
    res.end();

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