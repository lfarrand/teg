#include "meter.h"
#include "capture.h"
#include "config_json.h"
#include <Arduino.h>

extern MainConfig config;

static MeterReadings lastReadings;
static double energyMwh = 0.0;
static uint32_t lastEnergyMs = 0;
static elapsedMillis cadence;
static bool flipped = false;

void meterTask() {
  if (!captureMeterActive()) {
    lastReadings = MeterReadings{};
    lastEnergyMs = 0;
    cadence = 0;
    flipped = false;
    return;
  }

  // Bank-flip protocol: flip, give the ISR a moment to move to the new bank,
  // then drain the idle one - no locking against the 20kHz ISR needed
  if (!flipped && cadence >= 1000) {
    captureMeterFlip();
    flipped = true;
    return;
  }
  if (!(flipped && cadence >= 1002)) {
    return; // >= 2ms after the flip: several carrier periods of margin
  }
  cadence = 0;
  flipped = false;

  const MeterBank bank = captureMeterTake(captureMeterIdleBank());
  lastReadings = computeMeterReadings(
    bank.sumP, bank.sumVsq, bank.sumIsq, bank.n,
    meterMvPerCount(config.Meter.VoltageRatioMilli),
    meterMaPerCount(config.Meter.CurrentMilliampPerVolt));

  const uint32_t now = millis();
  if (lastReadings.valid) {
    if (lastEnergyMs != 0) {
      energyMwh += energyStepMwh(lastReadings.powerMw, now - lastEnergyMs);
    }
    lastEnergyMs = now;
  } else {
    lastEnergyMs = 0;
  }
}

MeterReadings meterReadings() {
  return lastReadings;
}

uint64_t meterEnergyMwh() {
  return energyMwhToCounter(energyMwh);
}
