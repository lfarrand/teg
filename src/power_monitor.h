#ifndef POWER_MONITOR_H
#define POWER_MONITOR_H

// Aux power monitor: reads the MOSFET driver board's built-in telemetry
// (INA226 over I2C, TPS25983 IMON/PG signals) and publishes total board
// input voltage/current/power/energy to the web UI, MQTT and InfluxDB.
//
// Bus: Wire2 (pins 24 SCL2 / 25 SDA2) - deliberately NOT the OLED's Wire
// bus, so a fault on the off-board telemetry loom cannot take the display
// down with it. The loom carries no pull-ups on the driver board; fit
// 2.2 kOhm to 3V3 at this end. See docs in the driver-board repo
// (POWER_MONITORING_DESIGN_2026-08-01.md) for the full wiring table.
//
// powerMonitorConfigure(): (re)apply config; safe on every settings apply.
// powerMonitorTask(): call from loop(). Interval-gated I2C reads; PG/alert
// pins are sampled every pass and edges land in the event log. The INA226
// is hot-pluggable: a probe that fails at configure retries in the task.

#include <stdint.h>
#include "power_monitor_math.h"

struct PowerMonReadings {
  bool valid = false;      // INA226 online and Enabled
  uint32_t busMv = 0;      // at the driver board's DC input (14-26 V nominal)
  int32_t currentMa = 0;   // total board draw through R24
  uint32_t powerMw = 0;
  int32_t imonMa = -1;     // eFuse IMON channel; -1 = not available
  bool pgEfuse = false;    // TPS25983 power good (pin configured and high)
  bool pgBuck = false;     // TPSM84338 power good
  bool alert = false;      // INA226 shunt-overcurrent alert (latched)
  uint32_t alertCount = 0; // ALERT edges retained even across I2C recovery
  uint32_t pgEdgeCount = 0;
  uint32_t commsErrors = 0;
};

void powerMonitorConfigure();
void powerMonitorTask();

PowerMonReadings powerMonReadings();
uint64_t powerMonEnergyMwh();   // accumulated since boot
int32_t powerMonPeakMa();       // highest current seen since boot (INA or IMON)
bool powerMonAvailable();       // Enabled and the INA226 answered last time

#endif
