#include "capture.h"
#include "capture_math.h"
#include "meter_math.h"
#include "config_json.h"
#include "pwm_utils.h" // vFaultTripped
#include "utils.h"
#include <Arduino.h>
#include <ADC.h>

extern MainConfig config;

// 1M samples x 16 bit = 2MB of the 8MB PSRAM: ~52s of history at 20kHz
constexpr uint32_t CaptureRingSamples = 1UL << 20;
EXTMEM static uint16_t captureRing[CaptureRingSamples];

// Current channel: 512K samples (1MB), ~26s at 20kHz
constexpr uint32_t CurrentRingSamples = 1UL << 19;
EXTMEM static uint16_t currentRing[CurrentRingSamples];

static ADC adcController;
static ADC_Module *adcModule = nullptr;
static uint8_t activePin = 255;

static ADC_Module *currentModule = nullptr;
static uint8_t activeCurrentPin = 255;

static volatile uint32_t vHead = 0;
static volatile uint32_t vTotal = 0;
static volatile bool vEnabled = false;
static volatile bool vFrozen = false;
static volatile uint16_t vLatest = 0;

static volatile bool vMeterEnabled = false;
static volatile uint32_t vCurrentHead = 0;
static int32_t zeroV = 2048; // sensor bias points in raw counts (config-derived)
static int32_t zeroI = 2048;
static MeterBank meterBanks[2];
static volatile uint8_t vMeterBank = 0;

void captureConfigure() {
  vEnabled = false; // stop the ISR touching the module while we reconfigure

  if (!config.Capture.Enabled) {
    return;
  }

  const uint8_t pin = config.Feedback.AnalogPin;
  if (pin != activePin || adcModule == nullptr) {
    // Pick whichever ADC module can read this pin (startSingleRead validates)
    adcModule = adcController.adc0;
    adcModule->setResolution(12);
    adcModule->setAveraging(1);
    adcModule->setConversionSpeed(ADC_CONVERSION_SPEED::HIGH_SPEED);
    adcModule->setSamplingSpeed(ADC_SAMPLING_SPEED::HIGH_SPEED);
    if (!adcModule->startSingleRead(pin)) {
      adcModule = adcController.adc1;
      adcModule->setResolution(12);
      adcModule->setAveraging(1);
      adcModule->setConversionSpeed(ADC_CONVERSION_SPEED::HIGH_SPEED);
      adcModule->setSamplingSpeed(ADC_SAMPLING_SPEED::HIGH_SPEED);
      if (!adcModule->startSingleRead(pin)) {
        adcModule = nullptr;
        Serial.println(F("Capture: pin not readable by either ADC"));
        return;
      }
    }
    activePin = pin;
  } else {
    adcModule->startSingleRead(activePin);
  }

  // Current channel: must live on the OTHER ADC module for simultaneous
  // sampling; if only the voltage module can read the pin, metering is off
  vMeterEnabled = false;
  if (config.Meter.Enabled) {
    ADC_Module *other = adcModule == adcController.adc0 ? adcController.adc1 : adcController.adc0;
    other->setResolution(12);
    other->setAveraging(1);
    other->setConversionSpeed(ADC_CONVERSION_SPEED::HIGH_SPEED);
    other->setSamplingSpeed(ADC_SAMPLING_SPEED::HIGH_SPEED);
    if (other->startSingleRead(config.Meter.CurrentPin)) {
      currentModule = other;
      activeCurrentPin = config.Meter.CurrentPin;
      zeroV = meterZeroCounts(config.Meter.VoltageZeroMillivolts);
      zeroI = meterZeroCounts(config.Meter.CurrentZeroMillivolts);
      meterBanks[0] = MeterBank{};
      meterBanks[1] = MeterBank{};
      vCurrentHead = 0;
      vMeterEnabled = true;
    } else {
      currentModule = nullptr;
      writeLog("Meter: current pin not readable by the other ADC module");
    }
  }

  vHead = 0;
  vTotal = 0;
  vFrozen = false;
  vEnabled = true;
}

FASTRUN void captureTick() {
  if (!vEnabled || adcModule == nullptr) {
    return;
  }
  if (vFaultTripped) {
    vFrozen = true; // freeze the ring: pre-fault history becomes the flight record
  }
  if (vFrozen) {
    return;
  }
  if (adcModule->isComplete()) {
    const uint16_t v = static_cast<uint16_t>(adcModule->readSingle());
    vLatest = v;
    const uint32_t h = vHead;
    captureRing[h] = v;
    vHead = (h + 1) & (CaptureRingSamples - 1);
    vTotal = vTotal + 1;

    // Paired current sample: both conversions started in the same tick, so
    // both are ready together; accumulate zero-corrected V*I / V^2 / I^2
    if (vMeterEnabled && currentModule->isComplete()) {
      const uint16_t iRaw = static_cast<uint16_t>(currentModule->readSingle());
      const uint32_t ch = vCurrentHead;
      currentRing[ch] = iRaw;
      vCurrentHead = (ch + 1) & (CurrentRingSamples - 1);

      const int32_t vs = static_cast<int32_t>(v) - zeroV;
      const int32_t is = static_cast<int32_t>(iRaw) - zeroI;
      MeterBank &bank = meterBanks[vMeterBank];
      bank.sumP += static_cast<int64_t>(vs) * is;
      bank.sumVsq += static_cast<uint64_t>(static_cast<int64_t>(vs) * vs);
      bank.sumIsq += static_cast<uint64_t>(static_cast<int64_t>(is) * is);
      bank.n++;
      currentModule->startSingleRead(activeCurrentPin);
    }

    adcModule->startSingleRead(activePin);
  }
}

bool captureActive() {
  return vEnabled;
}

bool captureIsFrozen() {
  return vFrozen;
}

uint32_t captureSampleCount() {
  return vTotal;
}

uint16_t captureLatestRaw() {
  return vLatest;
}

uint32_t captureMeanRaw(uint32_t n) {
  const uint32_t available = vTotal < CaptureRingSamples ? vTotal : CaptureRingSamples;
  if (available == 0) {
    return 0;
  }
  return ringMean(captureRing, CaptureRingSamples, vHead, n < available ? n : available);
}

uint32_t captureRingSamples() {
  return CaptureRingSamples;
}

uint32_t captureCopyRecent(int16_t *out, uint32_t n) {
  const uint32_t available = vTotal < CaptureRingSamples ? vTotal : CaptureRingSamples;
  if (n > available) {
    return 0;
  }
  uint32_t idx = (vHead + CaptureRingSamples - (n % CaptureRingSamples)) & (CaptureRingSamples - 1);
  for (uint32_t i = 0; i < n; i++) {
    out[i] = static_cast<int16_t>(captureRing[idx]);
    idx = (idx + 1) & (CaptureRingSamples - 1);
  }
  return n;
}

uint32_t captureDecimate(uint32_t count, uint32_t bins, uint16_t *outMin, uint16_t *outMax) {
  const uint32_t available = vTotal < CaptureRingSamples ? vTotal : CaptureRingSamples;
  if (count > available) {
    count = available;
  }
  if (count == 0) {
    return 0;
  }
  ringDecimate(captureRing, CaptureRingSamples, vHead, count, bins, outMin, outMax);
  return count;
}

uint32_t captureDecimateCurrent(uint32_t count, uint32_t bins, uint16_t *outMin, uint16_t *outMax) {
  if (!vMeterEnabled) {
    return 0;
  }
  const uint32_t available = vTotal < CurrentRingSamples ? vTotal : CurrentRingSamples;
  if (count > available) {
    count = available;
  }
  if (count == 0) {
    return 0;
  }
  ringDecimate(currentRing, CurrentRingSamples, vCurrentHead, count, bins, outMin, outMax);
  return count;
}

bool captureMeterActive() {
  return vEnabled && vMeterEnabled && !vFrozen;
}

void captureMeterFlip() {
  vMeterBank ^= 1;
}

uint8_t captureMeterIdleBank() {
  return vMeterBank ^ 1;
}

MeterBank captureMeterTake(uint8_t bank) {
  const MeterBank out = meterBanks[bank];
  meterBanks[bank] = MeterBank{};
  return out;
}
