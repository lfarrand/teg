#include "meter.h"
#include "capture.h"
#include "config_json.h"
#include <Arduino.h>

extern MainConfig config;

static MeterReadings lastReadings;
static double energyMwh = 0.0;
static uint32_t lastEnergyMs = 0;

void meterTask() {
  if (!captureMeterActive()) {
    lastReadings = MeterReadings{};
    return;
  }

  // Bank-flip protocol: flip, give the ISR a moment to move to the new bank,
  // then drain the idle one - no locking against the 20kHz ISR needed
  static elapsedMillis cadence;
  static bool flipped = false;

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
  if (lastReadings.valid && lastEnergyMs != 0) {
    energyMwh += energyStepMwh(lastReadings.powerMw, now - lastEnergyMs);
  }
  lastEnergyMs = now;
}

MeterReadings meterReadings() {
  return lastReadings;
}

uint64_t meterEnergyMwh() {
  return energyMwh >= 0.0 ? static_cast<uint64_t>(energyMwh) : 0;
}
