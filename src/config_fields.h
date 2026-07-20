#ifndef CONFIG_FIELDS_H
#define CONFIG_FIELDS_H

// Web-form field name -> MainConfig mapping: no hardware dependencies,
// unit-tested natively. Used by the /settings/pwm/update and
// /settings/pwm-timer/update handlers.

#include <string.h>
#include <stdlib.h>
#include "config_json.h"

// Applies one form field from the PWM settings page. Returns false if the
// field name is not recognised (the caller ignores unknown fields).
inline bool applyConfigFormField(MainConfig &config, const char *name, const char *value) {
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
    config.Pwm.PrintRegs = (strcmp(value, "Yes") == 0);
  } else if (strcmp(name, "sync-pwm") == 0) {
    config.Pwm.SyncPwm = (strcmp(value, "Yes") == 0);
  } else if (strcmp(name, "use-spwm") == 0) {
    config.Pwm.Tm2.UseSpwm = (strcmp(value, "Yes") == 0);
  } else if (strcmp(name, "spwm-carrier-signal-frequency") == 0) {
    config.Pwm.Tm2.SpwmCarrierFrequency = strtol(value, nullptr, 10);
  } else if (strcmp(name, "spwm-modulation-frequency") == 0) {
    config.Pwm.Tm2.SpwmModulationFrequency = strtol(value, nullptr, 10);
  } else if (strcmp(name, "modulation-scheme") == 0) {
    config.Pwm.Tm2.ModulationScheme = strtol(value, nullptr, 10);
  } else if (strcmp(name, "modulation-index") == 0) {
    config.Pwm.Tm2.ModulationIndexMilli = strtol(value, nullptr, 10);
  } else if (strcmp(name, "modulation-cells") == 0) {
    config.Pwm.Tm2.ModulationCells = strtol(value, nullptr, 10);
  } else if (strcmp(name, "carrier-disposition") == 0) {
    config.Pwm.Tm2.CarrierDisposition = strtol(value, nullptr, 10);
  } else if (strcmp(name, "deadtime-compensation") == 0) {
    config.Pwm.Tm2.DeadTimeCompensation = (strcmp(value, "Yes") == 0);
  } else if (strcmp(name, "soft-start-ms") == 0) {
    config.Pwm.Tm2.SoftStartMs = strtol(value, nullptr, 10);
  } else if (strcmp(name, "feedback-enabled") == 0) {
    config.Feedback.Enabled = (strcmp(value, "Yes") == 0);
  } else if (strcmp(name, "feedback-setpoint-mv") == 0) {
    config.Feedback.SetpointMillivolts = strtol(value, nullptr, 10);
  } else if (strcmp(name, "feedback-kp") == 0) {
    config.Feedback.KpMilli = strtol(value, nullptr, 10);
  } else if (strcmp(name, "feedback-ki") == 0) {
    config.Feedback.KiMilli = strtol(value, nullptr, 10);
  } else if (strcmp(name, "fault-enabled") == 0) {
    config.FaultProtection.Enabled = (strcmp(value, "Yes") == 0);
  } else if (strcmp(name, "fault-pin") == 0) {
    config.FaultProtection.Pin = strtol(value, nullptr, 10);
  } else if (strcmp(name, "fault-active-high") == 0) {
    config.FaultProtection.ActiveHigh = (strcmp(value, "Yes") == 0);
  } else if (strcmp(name, "enable-asymmetric-induction") == 0) {
    config.AsymmetricInduction.IsEnabled = (strcmp(value, "Yes") == 0);
  } else if (strcmp(name, "asymmetric-induction-preshiftnanos") == 0) {
    config.AsymmetricInduction.PreShiftNanos = strtol(value, nullptr, 10);
  } else if (strcmp(name, "asymmetric-induction-postshiftnanos") == 0) {
    config.AsymmetricInduction.PostShiftNanos = strtol(value, nullptr, 10);
  } else {
    return false;
  }
  return true;
}

// Applies one form field from the PWM timer settings page. Checkbox semantics:
// the caller must reset the Enabled flags to false before the form is parsed
// (unchecked boxes are simply absent from the POST body).
inline bool applyTimerFormField(MainConfig &config, const char *name, const char *value) {
  if (strcmp(name, "period-13a") == 0) {
    config.Pwm.Tm1.Sm13.ChannelA.OnPeriodMicroseconds = strtol(value, nullptr, 10);
  } else if (strcmp(name, "toggle-13a") == 0) {
    if (strcmp(value, "on") == 0) {
      config.Pwm.Tm1.Sm13.ChannelA.Enabled = true;
    }
  } else if (strcmp(name, "period-13b") == 0) {
    config.Pwm.Tm1.Sm13.ChannelB.OnPeriodMicroseconds = strtol(value, nullptr, 10);
  } else if (strcmp(name, "toggle-13b") == 0) {
    if (strcmp(value, "on") == 0) {
      config.Pwm.Tm1.Sm13.ChannelB.Enabled = true;
    }
  } else {
    return false;
  }
  return true;
}

#endif
