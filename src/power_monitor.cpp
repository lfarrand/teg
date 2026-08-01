#include "power_monitor.h"
#include "power_monitor_math.h"
#include "config_json.h"
#include "capture.h"
#include "meter_math.h" // energyStepMwh
#include "utils.h"
#include <Arduino.h>
#include <Wire.h>

extern MainConfig config;

// The telemetry loom gets its own bus (see power_monitor.h). 100 kHz, not
// 400: the loom leaves the enclosure and runs beside a switching converter;
// at 3 register reads per interval the whole transaction is ~1.5 ms per
// 100 ms - timing margin is worth more than bus speed here.
static TwoWire &bus = Wire2;
constexpr uint32_t PowerMonBusHz = 100000;

// INA226 register map
namespace {
enum : uint8_t {
  RegConfig = 0x00,
  RegShunt = 0x01,
  RegBus = 0x02,
  RegPower = 0x03,
  RegCurrent = 0x04,
  RegCal = 0x05,
  RegMaskEn = 0x06,
  RegAlertLim = 0x07,
  RegMfgId = 0xFE,
};
constexpr uint16_t MfgIdTi = 0x5449; // 'TI'
constexpr uint32_t ProbeRetryMs = 5000;
} // namespace

static bool inaOnline = false;
static bool enabled = false;
static PowerMonReadings lastReadings;
static double energyMwh = 0.0;
static uint32_t lastEnergyMs = 0;
static int32_t peakMa = 0;
static elapsedMillis sinceRead;
static elapsedMillis sinceProbe = ProbeRetryMs; // first probe immediately

static bool regWrite(uint8_t reg, uint16_t value) {
  bus.beginTransmission(config.PowerMon.Address);
  bus.write(reg);
  bus.write(static_cast<uint8_t>(value >> 8));
  bus.write(static_cast<uint8_t>(value & 0xFF));
  return bus.endTransmission() == 0;
}

static bool regRead(uint8_t reg, uint16_t &value) {
  bus.beginTransmission(config.PowerMon.Address);
  bus.write(reg);
  if (bus.endTransmission(false) != 0) {
    return false;
  }
  if (bus.requestFrom(static_cast<int>(config.PowerMon.Address), 2) != 2) {
    return false;
  }
  value = static_cast<uint16_t>(bus.read()) << 8;
  value |= static_cast<uint16_t>(bus.read());
  return true;
}

// Probe + program the INA226. Returns false when it does not answer (loom
// unplugged, board unpowered) - the task retries, degraded-mode style.
static bool inaProgram() {
  uint16_t id = 0;
  if (!regRead(RegMfgId, id) || id != MfgIdTi) {
    return false;
  }
  const uint32_t cal =
    ina226CalRegister(config.PowerMon.ShuntMicroOhm, config.PowerMon.CurrentLsbMicroAmp);
  if (cal == 0 || cal > 0xFFFF) {
    return false; // validateConfig should have prevented this
  }
  bool ok = regWrite(RegConfig, 0x8000); // reset
  delayMicroseconds(50);
  ok = ok && regWrite(RegConfig, Ina226ConfigValue);
  ok = ok && regWrite(RegCal, static_cast<uint16_t>(cal));
  if (config.PowerMon.AlertMilliAmp != 0) {
    ok = ok && regWrite(RegAlertLim, ina226AlertLimitCounts(config.PowerMon.AlertMilliAmp,
                                                            config.PowerMon.ShuntMicroOhm));
    ok = ok && regWrite(RegMaskEn, Ina226MaskSolLatched);
  }
  return ok;
}

static void pinInput(uint8_t pin) {
  if (pin != 255) {
    pinMode(pin, INPUT);
  }
}

void powerMonitorConfigure() {
  enabled = config.PowerMon.Enabled;
  inaOnline = false;
  lastReadings = PowerMonReadings{};
  if (!enabled) {
    return;
  }
  bus.begin();
  bus.setClock(PowerMonBusHz);
  pinInput(config.PowerMon.PgEfusePin);  // driver board provides 10k pull-ups
  pinInput(config.PowerMon.PgBuckPin);
  pinInput(config.PowerMon.AlertPin);    // open-drain; pull-up at this end
  inaOnline = inaProgram();
  writeLogLevel(inaOnline ? EventInfo : EventWarn,
                inaOnline ? "Aux monitor: INA226 online" : "Aux monitor: INA226 not answering");
  sinceProbe = 0;
  sinceRead = config.PowerMon.IntervalMs; // first read immediately
  lastEnergyMs = 0;
}

// PG/alert pins are cheap digital reads: sampled every pass so state edges
// are timestamped at loop resolution, not at the I2C interval
static void samplePins() {
  const bool pg1 = config.PowerMon.PgEfusePin != 255 &&
                   digitalRead(config.PowerMon.PgEfusePin) == HIGH;
  const bool pg2 = config.PowerMon.PgBuckPin != 255 &&
                   digitalRead(config.PowerMon.PgBuckPin) == HIGH;
  if (config.PowerMon.PgEfusePin != 255 && pg1 != lastReadings.pgEfuse) {
    writeLogLevel(pg1 ? EventInfo : EventWarn,
                  pg1 ? "Aux monitor: eFuse PG asserted" : "Aux monitor: eFuse PG lost");
  }
  if (config.PowerMon.PgBuckPin != 255 && pg2 != lastReadings.pgBuck) {
    writeLogLevel(pg2 ? EventInfo : EventWarn,
                  pg2 ? "Aux monitor: buck PG asserted" : "Aux monitor: buck PG lost");
  }
  lastReadings.pgEfuse = pg1;
  lastReadings.pgBuck = pg2;
}

void powerMonitorTask() {
  if (!enabled) {
    return;
  }

  samplePins();

  if (!inaOnline) {
    if (sinceProbe >= ProbeRetryMs) {
      sinceProbe = 0;
      inaOnline = inaProgram();
      if (inaOnline) {
        writeLog("Aux monitor: INA226 online");
      }
    }
    lastReadings.valid = false;
    return;
  }

  if (sinceRead < config.PowerMon.IntervalMs) {
    return;
  }
  sinceRead = 0;

  uint16_t rawBus = 0, rawCurrent = 0, rawPower = 0;
  bool ok = regRead(RegBus, rawBus);
  ok = ok && regRead(RegCurrent, rawCurrent);
  ok = ok && regRead(RegPower, rawPower);
  if (!ok) {
    inaOnline = false;
    lastReadings.valid = false;
    writeLogLevel(EventWarn, "Aux monitor: INA226 comms lost");
    sinceProbe = 0;
    return;
  }

  lastReadings.valid = true;
  lastReadings.busMv = ina226BusMv(rawBus);
  lastReadings.currentMa =
    ina226CurrentMa(static_cast<int16_t>(rawCurrent), config.PowerMon.CurrentLsbMicroAmp);
  lastReadings.powerMw = ina226PowerMw(rawPower, config.PowerMon.CurrentLsbMicroAmp);
  if (lastReadings.currentMa > peakMa) {
    peakMa = lastReadings.currentMa;
  }

  // Latched shunt-overcurrent alert: reading Mask/Enable returns AFF and
  // clears the latch, so a trip between polls is never missed
  if (config.PowerMon.AlertMilliAmp != 0) {
    uint16_t maskEn = 0;
    if (regRead(RegMaskEn, maskEn)) {
      const bool alert = (maskEn & Ina226MaskAffBit) != 0;
      if (alert && !lastReadings.alert) {
        writeLogLevel(EventError, "Aux monitor: overcurrent alert (shunt limit exceeded)");
      }
      lastReadings.alert = alert;
    }
  }

  // IMON (optional): the eFuse's analog current mirror into R_IMON. Read
  // with the core's analogRead ONLY while the capture ISR does not own the
  // ADC modules - captureTick() runs startSingleRead per carrier cycle on
  // both modules, and a loop-context conversion would race it (same rule as
  // the feedback fallback in web_handlers.cpp). When capture is active the
  // INA226 remains the current channel and imonMa reads -1.
  lastReadings.imonMa = -1;
  if (config.PowerMon.ImonPin != 255 && !captureActive()) {
    const uint32_t raw = analogRead(config.PowerMon.ImonPin);
    lastReadings.imonMa = imonMilliAmp(raw, AdcCountFullScale, 3300,
                                       config.PowerMon.ImonRimonOhm,
                                       Tps25983GimonMicroAmpPerAmp);
    if (lastReadings.imonMa > peakMa) {
      peakMa = lastReadings.imonMa;
    }
  }

  // Energy over wall time between valid readings (meter.cpp pattern)
  const uint32_t now = millis();
  if (lastEnergyMs != 0) {
    energyMwh += energyStepMwh(static_cast<int32_t>(lastReadings.powerMw), now - lastEnergyMs);
  }
  lastEnergyMs = now;
}

PowerMonReadings powerMonReadings() {
  return lastReadings;
}

uint64_t powerMonEnergyMwh() {
  return energyMwh >= 0.0 ? static_cast<uint64_t>(energyMwh) : 0;
}

int32_t powerMonPeakMa() {
  return peakMa;
}

bool powerMonAvailable() {
  return enabled && inaOnline;
}
