#ifndef PIN_OWNERSHIP_H
#define PIN_OWNERSHIP_H

#include <stddef.h>
#include <stdint.h>
#include "config_json.h"

// Teensy 4.1 header pins are 0..41. Pin 42 is deliberately excluded: the
// Teensyduino 1.59 analog lookup table has no valid entry for it.
constexpr uint8_t Teensy41LastPin = 41;

enum PinRole : uint16_t {
  PinRoleNone         = 0,
  PinRolePwm          = 1u << 0,
  PinRoleTrigger      = 1u << 1,
  PinRoleWire         = 1u << 2,
  PinRoleWire2        = 1u << 3,
  PinRoleFeedbackAdc  = 1u << 4,
  PinRoleMeterCurrent = 1u << 5,
  PinRoleCurrentLimit = 1u << 6,
  PinRoleGpioFault    = 1u << 7,
  PinRoleOneWire      = 1u << 8,
  PinRolePowerMonitor = 1u << 9,
  PinRoleImonAdc      = 1u << 10
};

struct PinValidationResult {
  bool valid = true;
  uint8_t pin = 255;
  uint16_t existing = PinRoleNone;
  uint16_t requested = PinRoleNone;
};

inline bool teensy41AnalogPin(uint8_t pin) {
  return (pin >= 14 && pin <= 27) || (pin >= 38 && pin <= 41);
}

inline const char *pinRoleName(uint16_t role) {
  switch (role) {
    case PinRolePwm: return "PWM output";
    case PinRoleTrigger: return "trigger/LED";
    case PinRoleWire: return "OLED I2C";
    case PinRoleWire2: return "power-monitor I2C";
    case PinRoleFeedbackAdc: return "feedback/capture ADC";
    case PinRoleMeterCurrent: return "meter current ADC";
    case PinRoleCurrentLimit: return "hardware current limit";
    case PinRoleGpioFault: return "GPIO fault";
    case PinRoleOneWire: return "OneWire temperature";
    case PinRolePowerMonitor: return "power-monitor digital signal";
    case PinRoleImonAdc: return "eFuse IMON ADC";
    default: return "unknown";
  }
}

inline bool pinRolesCompatible(uint16_t existing, uint16_t requested) {
  // The current sensor pad is intentionally shared by the ADC and the ACMP.
  const uint16_t currentSense = PinRoleMeterCurrent | PinRoleCurrentLimit;
  return ((existing | requested) & ~currentSense) == 0;
}

inline bool validatePinOwnership(const MainConfig &cfg, PinValidationResult *result = nullptr) {
  PinValidationResult local;
  uint16_t claims[Teensy41LastPin + 1]{};

  const auto fail = [&local](uint8_t pin, uint16_t existing, uint16_t requested) {
    local.valid = false;
    local.pin = pin;
    local.existing = existing;
    local.requested = requested;
    return false;
  };
  const auto claim = [&claims, &fail](uint8_t pin, uint16_t role, bool analog) {
    if (pin > Teensy41LastPin || (analog && !teensy41AnalogPin(pin))) {
      return fail(pin, PinRoleNone, role);
    }
    if (claims[pin] != PinRoleNone && !pinRolesCompatible(claims[pin], role)) {
      return fail(pin, claims[pin], role);
    }
    claims[pin] |= role;
    return true;
  };

  // Every PWM object is configured at boot, even when SPWM is disabled.
  constexpr uint8_t pwmPins[] = {2, 3, 4, 5, 6, 7, 8, 9, 22, 23, 28, 29, 33, 36, 37};
  for (uint8_t pin : pwmPins) {
    if (!claim(pin, PinRolePwm, false)) goto done;
  }
  if (!claim(13, PinRoleTrigger, false) ||
      !claim(18, PinRoleWire, false) || !claim(19, PinRoleWire, false)) goto done;

  if ((cfg.Feedback.Enabled || cfg.Capture.Enabled || cfg.Pll.Enabled) &&
      !claim(cfg.Feedback.AnalogPin, PinRoleFeedbackAdc, true)) goto done;
  if (cfg.Meter.Enabled &&
      !claim(cfg.Meter.CurrentPin, PinRoleMeterCurrent, true)) goto done;
  if (cfg.CurrentLimit.Enabled &&
      !claim(cfg.CurrentLimit.Pin, PinRoleCurrentLimit, true)) goto done;
  if (cfg.FaultProtection.Enabled &&
      !claim(cfg.FaultProtection.Pin, PinRoleGpioFault, false)) goto done;
  if (cfg.Thermal.Enabled &&
      !claim(cfg.Thermal.OneWirePin, PinRoleOneWire, false)) goto done;

  if (cfg.PowerMon.Enabled) {
    if (!claim(24, PinRoleWire2, false) || !claim(25, PinRoleWire2, false)) goto done;
    const uint8_t digitalPins[] = {cfg.PowerMon.PgEfusePin, cfg.PowerMon.PgBuckPin,
                                   cfg.PowerMon.AlertPin};
    for (uint8_t pin : digitalPins) {
      if (pin != 255 && !claim(pin, PinRolePowerMonitor, false)) goto done;
    }
    if (cfg.PowerMon.ImonPin != 255 &&
        !claim(cfg.PowerMon.ImonPin, PinRoleImonAdc, true)) goto done;
  }

done:
  if (result != nullptr) {
    *result = local;
  }
  return local.valid;
}

#endif
