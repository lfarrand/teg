#include "capture.h"
#include "capture_math.h"
#include "meter_math.h"
#include "scope_math.h"
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

// Scope trigger: vScopeEnabled gates the ISR's access to the machine so the
// web handler can reconfigure it without racing (worst case the ISR skips or
// double-checks one sample either side of an arm - harmless)
static ScopeMachine scopeM;
static volatile bool vScopeEnabled = false;
static volatile bool vScopeSourceCurrent = false;
static volatile uint32_t vScopeTrigSample = 0;

void captureConfigure() {
  vEnabled = false; // stop the ISR touching the module while we reconfigure
  vScopeEnabled = false; // reconfigure invalidates an armed trigger
  scopeDisarm(scopeM);

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

    if (vScopeEnabled && !vScopeSourceCurrent && scopeM.state != ScopeComplete) {
      const uint8_t before = scopeM.state;
      const bool freeze = scopeStep(scopeM, v);
      if (before == ScopeArmed && scopeM.state != ScopeArmed) {
        vScopeTrigSample = vTotal; // this sample's 1-based index
      }
      if (freeze) {
        vFrozen = true;
      }
    }

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

      if (vScopeEnabled && vScopeSourceCurrent && scopeM.state != ScopeComplete) {
        const uint8_t before = scopeM.state;
        const bool freeze = scopeStep(scopeM, iRaw);
        if (before == ScopeArmed && scopeM.state != ScopeArmed) {
          vScopeTrigSample = vTotal;
        }
        if (freeze) {
          vFrozen = true;
        }
      }

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

uint32_t captureReadSince(uint32_t *cursorTotal, uint16_t *dst, uint32_t maxN) {
  const uint32_t total = vTotal;
  // Modular lag so the counter's 2^32 wrap (~6h at 200kHz) is seamless; a
  // "negative" lag (cursor ahead) means captureConfigure reset the counter
  uint32_t lag = total - *cursorTotal;
  if (lag > 0x80000000u) {
    *cursorTotal = total; // reconfigure reset the counter; resynchronize
    return 0;
  }
  // Keep a margin from the write head's overwrite frontier: samples close to
  // the oldest edge of a full ring may be rewritten mid-copy by the ISR
  constexpr uint32_t OverrunMargin = 4096;
  if (lag > CaptureRingSamples - OverrunMargin) {
    *cursorTotal = total - (CaptureRingSamples - OverrunMargin);
    lag = CaptureRingSamples - OverrunMargin;
  }
  const uint32_t n = lag < maxN ? lag : maxN;
  if (n == 0) {
    return 0;
  }
  uint32_t idx = *cursorTotal & (CaptureRingSamples - 1);
  for (uint32_t i = 0; i < n; i++) {
    dst[i] = captureRing[idx];
    idx = (idx + 1) & (CaptureRingSamples - 1);
  }
  *cursorTotal += n;
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

bool captureScopeArm(bool currentSource, uint16_t levelCounts, bool fallingEdge,
                     uint32_t postSamples) {
  if (!vEnabled || (currentSource && !vMeterEnabled)) {
    return false;
  }
  vScopeEnabled = false; // take the machine away from the ISR while we set up
  vScopeSourceCurrent = currentSource;
  vScopeTrigSample = 0;
  scopeArm(scopeM, levelCounts, fallingEdge, postSamples);
  vFrozen = false; // arming releases a previous scope/fault freeze
  vScopeEnabled = true;
  return true;
}

void captureScopeRelease() {
  vScopeEnabled = false;
  scopeDisarm(scopeM);
  vFrozen = false; // resume rolling capture (a live fault re-freezes next tick)
}

uint8_t captureScopeState() {
  return vScopeEnabled ? scopeM.state : static_cast<uint8_t>(ScopeIdle);
}

bool captureScopeSourceIsCurrent() {
  return vScopeSourceCurrent;
}

uint16_t captureScopeLevelCounts() {
  return scopeM.level;
}

bool captureScopeFalling() {
  return scopeM.fallingEdge;
}

uint32_t captureScopePostSamples() {
  return scopeM.postTotal;
}

uint32_t captureScopeTrigSample() {
  return vScopeTrigSample;
}

uint32_t captureRawAvailable(bool currentChannel) {
  if (currentChannel && !vMeterEnabled) {
    return 0;
  }
  const uint32_t ring = currentChannel ? CurrentRingSamples : CaptureRingSamples;
  return vTotal < ring ? vTotal : ring;
}

void captureRawCopy(bool currentChannel, uint32_t windowN, uint32_t offset,
                    uint16_t *dst, uint32_t n) {
  const uint16_t *ring = currentChannel ? currentRing : captureRing;
  const uint32_t size = currentChannel ? CurrentRingSamples : CaptureRingSamples;
  const uint32_t head = currentChannel ? vCurrentHead : vHead;
  // Window start = head - windowN (mod size); copy [offset, offset+n) of it
  uint32_t idx = (head + size - (windowN % size) + offset) & (size - 1);
  for (uint32_t i = 0; i < n; i++) {
    dst[i] = ring[idx];
    idx = (idx + 1) & (size - 1);
  }
}
