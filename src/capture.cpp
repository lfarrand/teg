#include "capture.h"
#include "capture_math.h"
#include "config_json.h"
#include "pwm_utils.h" // vFaultTripped
#include <Arduino.h>
#include <ADC.h>

extern MainConfig config;

// 1M samples x 16 bit = 2MB of the 8MB PSRAM: ~52s of history at 20kHz
constexpr uint32_t CaptureRingSamples = 1UL << 20;
EXTMEM static uint16_t captureRing[CaptureRingSamples];

static ADC adcController;
static ADC_Module *adcModule = nullptr;
static uint8_t activePin = 255;

static volatile uint32_t vHead = 0;
static volatile uint32_t vTotal = 0;
static volatile bool vEnabled = false;
static volatile bool vFrozen = false;
static volatile uint16_t vLatest = 0;

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
