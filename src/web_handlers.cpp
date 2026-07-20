#include "web_handlers.h"
#include "config_json.h"
#include "pwm_utils.h"
#include "utils.h"

extern Application app;
extern MainConfig config;
extern EthernetServer server;
extern const char* filename;

void configureWebServer() {
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

void index(Request &req, Response &res) {
  res.print("Welcome to PWM Control System");
}

void settings_pwm(Request &req, Response &res) {
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

void settings_pwm_update(Request &req, Response &res) {
  if (req.method() != Request::MethodType::POST) {
    res.set("Content-Type", "text/plain");
    res.printP("Method Not Allowed");
    res.sendStatus(405);
    res.flush();
    res.end();
  }
  else {
    disablePwmInterrupts();

    while (req.left()) {
      char value[100];
      char name[50];
      if (!req.form(name, 50, value, 100)) {
        res.sendStatus(400);
        res.flush();
        res.end();
        return;
      }

      res.print(name);
      res.print(": ");
      res.println(value);

      if (strcmp(name, "period-13a") == 0) {
        config.Pwm.Tm1.Sm13.ChannelA.OnPeriodMicroseconds = strtol(value, nullptr, 10);
      } else if (strcmp(name, "period-13b") == 0) {
        config.Pwm.Tm1.Sm13.ChannelB.OnPeriodMicroseconds = strtol(value, nullptr, 10);
      } else if (strcmp(name, "pwm-frequency-13") == 0) {
        config.Pwm.Tm1.Sm13.PwmFrequency = strtol(value, nullptr, 10);
      } else if (strcmp(name, "pwm-frequency-20") == 0) {
        config.Pwm.Tm2.Sm20.PwmFrequency = strtol(value, nullptr, 10);
      } else if (strcmp(name, "pwm-frequency-21") == 0) {
        config.Pwm.Tm2.Sm21.PwmFrequency = strtol(value, nullptr, 10);
      } else if (strcmp(name, "pwm-frequency-22") == 0) {
        config.Pwm.Tm2.Sm22.PwmFrequency = strtol(value, nullptr, 10);
      } else if (strcmp(name, "pwm-frequency-23") == 0) {
        config.Pwm.Tm2.Sm23.PwmFrequency = strtol(value, nullptr, 10);
      } else if (strcmp(name, "pwm-frequency-31") == 0) {
        config.Pwm.Tm3.Sm31.PwmFrequency = strtol(value, nullptr, 10);
      } else if (strcmp(name, "pwm-frequency-40") == 0) {
        config.Pwm.Tm4.Sm40.PwmFrequency = strtol(value, nullptr, 10);
      } else if (strcmp(name, "pwm-frequency-41") == 0) {
        config.Pwm.Tm4.Sm41.PwmFrequency = strtol(value, nullptr, 10);
      } else if (strcmp(name, "pwm-frequency-42") == 0) {
        config.Pwm.Tm4.Sm42.PwmFrequency = strtol(value, nullptr, 10);
      } else if (strcmp(name, "dead-time-13") == 0) {
        config.Pwm.Tm1.Sm13.DeadTime = strtol(value, nullptr, 10);
      } else if (strcmp(name, "dead-time-20") == 0) {
        config.Pwm.Tm2.Sm20.DeadTime = strtol(value, nullptr, 10);
      } else if (strcmp(name, "dead-time-21") == 0) {
        config.Pwm.Tm2.Sm21.DeadTime = strtol(value, nullptr, 10);
      } else if (strcmp(name, "dead-time-22") == 0) {
        config.Pwm.Tm2.Sm22.DeadTime = strtol(value, nullptr, 10);
      } else if (strcmp(name, "dead-time-23") == 0) {
        config.Pwm.Tm2.Sm23.DeadTime = strtol(value, nullptr, 10);
      } else if (strcmp(name, "dead-time-31") == 0) {
        config.Pwm.Tm3.Sm31.DeadTime = strtol(value, nullptr, 10);
      } else if (strcmp(name, "dead-time-40") == 0) {
        config.Pwm.Tm4.Sm40.DeadTime = strtol(value, nullptr, 10);
      } else if (strcmp(name, "dead-time-41") == 0) {
        config.Pwm.Tm4.Sm41.DeadTime = strtol(value, nullptr, 10);
      } else if (strcmp(name, "dead-time-42") == 0) {
        config.Pwm.Tm4.Sm42.DeadTime = strtol(value, nullptr, 10);
      } else if (strcmp(name, "duty-cycle-13a") == 0) {
        config.Pwm.Tm1.Sm13.ChannelA.DutyCycle = strtol(value, nullptr, 10);
      } else if (strcmp(name, "duty-cycle-13b") == 0) {
        config.Pwm.Tm1.Sm13.ChannelB.DutyCycle = strtol(value, nullptr, 10);
      } else if (strcmp(name, "duty-cycle-20a") == 0) {
        config.Pwm.Tm2.Sm20.ChannelA.DutyCycle = strtol(value, nullptr, 10);
      } else if (strcmp(name, "duty-cycle-20b") == 0) {
        config.Pwm.Tm2.Sm20.ChannelB.DutyCycle = strtol(value, nullptr, 10);
      } else if (strcmp(name, "duty-cycle-21a") == 0) {
        config.Pwm.Tm2.Sm21.ChannelA.DutyCycle = strtol(value, nullptr, 10);
      } else if (strcmp(name, "duty-cycle-22a") == 0) {
        config.Pwm.Tm2.Sm22.ChannelA.DutyCycle = strtol(value, nullptr, 10);
      } else if (strcmp(name, "duty-cycle-22b") == 0) {
        config.Pwm.Tm2.Sm22.ChannelB.DutyCycle = strtol(value, nullptr, 10);
      } else if (strcmp(name, "duty-cycle-23a") == 0) {
        config.Pwm.Tm2.Sm23.ChannelA.DutyCycle = strtol(value, nullptr, 10);
      } else if (strcmp(name, "duty-cycle-23b") == 0) {
        config.Pwm.Tm2.Sm23.ChannelB.DutyCycle = strtol(value, nullptr, 10);
      } else if (strcmp(name, "duty-cycle-31a") == 0) {
        config.Pwm.Tm3.Sm31.ChannelA.DutyCycle = strtol(value, nullptr, 10);
      } else if (strcmp(name, "duty-cycle-31b") == 0) {
        config.Pwm.Tm3.Sm31.ChannelB.DutyCycle = strtol(value, nullptr, 10);
      } else if (strcmp(name, "duty-cycle-40a") == 0) {
        config.Pwm.Tm4.Sm40.ChannelA.DutyCycle = strtol(value, nullptr, 10);
      } else if (strcmp(name, "duty-cycle-41a") == 0) {
        config.Pwm.Tm4.Sm41.ChannelA.DutyCycle = strtol(value, nullptr, 10);
      } else if (strcmp(name, "duty-cycle-42a") == 0) {
        config.Pwm.Tm4.Sm42.ChannelA.DutyCycle = strtol(value, nullptr, 10);
      } else if (strcmp(name, "duty-cycle-42b") == 0) {
        config.Pwm.Tm4.Sm42.ChannelB.DutyCycle = strtol(value, nullptr, 10);
      } else if (strcmp(name, "phase-shift-21a") == 0) {
        config.Pwm.Tm2.Sm21.ChannelA.PhaseShift = strtol(value, nullptr, 10);
      } else if (strcmp(name, "phase-shift-22a") == 0) {
        config.Pwm.Tm2.Sm22.ChannelA.PhaseShift = strtol(value, nullptr, 10);
      } else if (strcmp(name, "phase-shift-22b") == 0) {
        config.Pwm.Tm2.Sm22.ChannelB.PhaseShift = strtol(value, nullptr, 10);
      } else if (strcmp(name, "phase-shift-23a") == 0) {
        config.Pwm.Tm2.Sm23.ChannelA.PhaseShift = strtol(value, nullptr, 10);
      } else if (strcmp(name, "phase-shift-23b") == 0) {
        config.Pwm.Tm2.Sm23.ChannelB.PhaseShift = strtol(value, nullptr, 10);
      } else if (strcmp(name, "phase-shift-41a") == 0) {
        config.Pwm.Tm4.Sm41.ChannelA.PhaseShift = strtol(value, nullptr, 10);
      } else if (strcmp(name, "phase-shift-42a") == 0) {
        config.Pwm.Tm4.Sm42.ChannelA.PhaseShift = strtol(value, nullptr, 10);
      } else if (strcmp(name, "phase-shift-42b") == 0) {
        config.Pwm.Tm4.Sm42.ChannelB.PhaseShift = strtol(value, nullptr, 10);
      } else if (strcmp(name, "print-regs") == 0) {
        if (strcmp(value, "Yes") == 0) {
          config.Pwm.PrintRegs = true;
        } else {
          config.Pwm.PrintRegs = false;
        }
      } else if (strcmp(name, "sync-pwm") == 0) {
        if (strcmp(value, "Yes") == 0) {
          config.Pwm.SyncPwm = true;
        } else {
          config.Pwm.SyncPwm = false;
        }
      } else if (strcmp(name, "use-spwm") == 0) {
        if (strcmp(value, "Yes") == 0) {
          config.Pwm.Tm2.UseSpwm = true;
        } else {
          config.Pwm.Tm2.UseSpwm = false;
        }
      } else if (strcmp(name, "spwm-carrier-signal-frequency") == 0) {
        config.Pwm.Tm2.SpwmCarrierFrequency = strtol(value, nullptr, 10);
      } else if (strcmp(name, "spwm-modulation-frequency") == 0) {
        config.Pwm.Tm2.SpwmModulationFrequency = strtol(value, nullptr, 10);
      } else if (strcmp(name, "enable-asymmetric-induction") == 0) {
        if (strcmp(value, "Yes") == 0) {
          config.AsymmetricInduction.IsEnabled = true;
        } else {
          config.AsymmetricInduction.IsEnabled = false;
        }
      } else if (strcmp(name, "pwm-frequency-42") == 0) {
        config.Pwm.Tm4.Sm42.PwmFrequency = strtoul(value, nullptr, 10);
      } else if (strcmp(name, "duty-cycle-42a") == 0) {
        config.Pwm.Tm4.Sm42.ChannelA.DutyCycle = static_cast<unsigned short>(strtoul(value, nullptr, 10));
      } else if (strcmp(name, "asymmetric-induction-preshiftnanos") == 0) {
        config.AsymmetricInduction.PreShiftNanos = strtol(value, nullptr, 10);
      } else if (strcmp(name, "asymmetric-induction-postshiftnanos") == 0) {
        config.AsymmetricInduction.PostShiftNanos = strtol(value, nullptr, 10);
      }
    }

    Serial.println(F("Saving configuration..."));
    saveConfiguration(filename);

    Serial.println(F("Print config file..."));
    printFile(filename);

    delay(1000);

    configurePwm();

    if (config.Pwm.Tm2.UseSpwm) {
      enablePwmInterrupts();
    }

    // Redirect
    res.set("Location", "/settings/pwm");
    res.sendStatus(302);
    res.flush();
    res.end();
  }
}

void settings_pwm_timer(Request &req, Response &res) {
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

void settings_pwm_timer_update(Request &req, Response &res) {
  if (req.method() != Request::MethodType::POST) {
    res.set("Content-Type", "text/plain");
    res.printP("Method Not Allowed");
    res.sendStatus(405);
    res.flush();
    res.end();
  } else {

    writeLog(F("Disabling SM13 channel A"));
    writeLog(F("Disabling SM13 channel B"));
    config.Pwm.Tm1.Sm13.ChannelA.Enabled = false;
    config.Pwm.Tm1.Sm13.ChannelB.Enabled = false;

    while (req.left()) {
      char value[100];
      char name[50];
      if (!req.form(name, 50, value, 100)) {
        return res.sendStatus(400);
      }

      res.print(name);
      res.print(": ");
      res.println(value);

      if (strcmp(name, "period-13a") == 0) {
        config.Pwm.Tm1.Sm13.ChannelA.OnPeriodMicroseconds = strtol(value, nullptr, 10);
      } else if (strcmp(name, "toggle-13a") == 0) {
        if (strcmp(value, "on") == 0) {
          writeLog(F("Enabling SM13 channel A"));
          config.Pwm.Tm1.Sm13.ChannelA.Enabled = true;
        }
      } else if (strcmp(name, "period-13b") == 0) {
        config.Pwm.Tm1.Sm13.ChannelB.OnPeriodMicroseconds = strtol(value, nullptr, 10);
      } else if (strcmp(name, "toggle-13b") == 0) {
        if (strcmp(value, "on") == 0) {
          writeLog(F("Enabling SM13 channel B"));
          config.Pwm.Tm1.Sm13.ChannelB.Enabled = true;
        }
      }
    }

    Serial.println(F("Saving configuration..."));
    saveConfiguration(filename);

    Serial.println(F("Print config file..."));
    printFile(filename);

    // Redirect
    res.set("Location", "/settings/pwm-timer");
    res.sendStatus(302);
    res.flush();
    res.end();
  }
}

void DumpText(EthernetClient &client) {
}