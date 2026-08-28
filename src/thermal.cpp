#include "thermal.h"
#include "thermal_math.h"
#include "config_json.h"
#include "pwm_utils.h"
#include "modulation.h"
#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <InternalTemperature.h>

extern MainConfig config;

// OneWire's bus pin is constructor-only; placement-new lets a settings apply
// move the bus to a different pin without a reboot (OneWire holds no
// resources that need destruction).
alignas(OneWire) static uint8_t oneWireStorage[sizeof(OneWire)];
static OneWire *oneWireBus = nullptr;
static DallasTemperature sensors;
static uint8_t activePin = 255;

static uint16_t derateMilli = 1000;
static int16_t hotDeciC = INT16_MIN;
static int16_t coldDeciC = INT16_MIN;
static int16_t chipDeciC = INT16_MIN;

// Cached ROM addresses. The lexicographically lower ROM is probe 1 (reported
// as "hot" only in the legacy API), the higher ROM is probe 2 (legacy "cold").
// This is deterministic across reset and bus-search ordering but deliberately
// makes no claim about physical connector or thermal role. Safety derating uses
// BOTH probes, so wiring order cannot hide an overtemperature.
// getTempCByIndex() calls getAddress(), which runs a FULL
// bus search every time - reset_search() then search() until it reaches the index -
// and a 1-Wire ROM search reads three bit-slots per ROM bit. Reading two probes by
// index therefore performs three device searches, roughly 576 bit-slots that exist
// only to rediscover addresses which never change.
//
// That matters here beyond the wasted time: OneWire masks interrupts around each
// bit slot, up to 65-70us for a write-0, which is longer than a whole carrier
// period at 20kHz. Every avoidable slot is avoidable modulation disruption. Caching
// the addresses and reading by address removes about 78% of the bus traffic per
// harvest. Measure the remainder with missedIsrCycles.
static DeviceAddress hotAddr;
static DeviceAddress coldAddr;
static bool hotAddrValid = false;
static bool coldAddrValid = false;
static uint32_t harvestMissedCycles = 0; // ISR cycles lost to the last harvest
static elapsedMillis sinceRequest;
static elapsedMillis sinceRescan;
static bool conversionPending = false;
static uint32_t missedAtRequest = 0;
static bool haveValidExternalSample = false;

uint32_t thermalHarvestMissedCycles() {
  return harvestMissedCycles;
}

// Search the bus and cache whatever is present. Also called periodically while a
// probe is missing, so one connected later is still picked up.
static int compareRom(const DeviceAddress a, const DeviceAddress b) {
  for (uint8_t i = 0; i < 8; ++i) {
    if (a[i] < b[i]) return -1;
    if (a[i] > b[i]) return 1;
  }
  return 0;
}

static void cacheProbeAddresses() {
  sensors.begin(); // re-counts devices; getAddress() gates on that count
  DeviceAddress a = {}, b = {};
  const bool haveA = sensors.getAddress(a, 0);
  const bool haveB = sensors.getAddress(b, 1);
  hotAddrValid = haveA;
  coldAddrValid = haveB;
  if (haveA && haveB && compareRom(a, b) > 0) {
    memcpy(hotAddr, b, sizeof(DeviceAddress));
    memcpy(coldAddr, a, sizeof(DeviceAddress));
  } else {
    if (haveA) memcpy(hotAddr, a, sizeof(DeviceAddress));
    if (haveB) memcpy(coldAddr, b, sizeof(DeviceAddress));
  }
}

void thermalConfigure() {
  if (!config.Thermal.Enabled) {
    derateMilli = 1000;
    haveValidExternalSample = true;
    hotDeciC = coldDeciC = chipDeciC = INT16_MIN;
    conversionPending = false;
    return;
  }
  derateMilli = 0;
  haveValidExternalSample = false;
  if (config.Thermal.OneWirePin != activePin) {
    activePin = config.Thermal.OneWirePin;
    oneWireBus = new (oneWireStorage) OneWire(activePin);
    sensors.setOneWire(oneWireBus);
    sensors.setWaitForConversion(false); // never block loop() on a conversion
    hotAddrValid = coldAddrValid = false; // different bus: forget the old ROMs
    cacheProbeAddresses();
  }
}

static int16_t toDeciC(float c) {
  // DallasTemperature reports DEVICE_DISCONNECTED_C (-127) for missing probes
  if (c < -100.0f || c > 300.0f) {
    return INT16_MIN;
  }
  return static_cast<int16_t>(c * 10.0f);
}

void thermalTask() {
  if (!config.Thermal.Enabled) {
    return;
  }
  // OneWire masks IRQs 65–70 µs per write-0. Do not run bit slots while OUTEN
  // is live; last-good derate holds until the next inhibited harvest.
  if (!pwmOutputInhibited()) {
    return;
  }

  // Non-blocking cadence: request conversions, harvest them 800ms later
  // (a 12-bit DS18B20 conversion takes 750ms)
  const uint32_t requestMs = config.Pwm.Tm2.SpwmCarrierFrequency >= 10000U ? 4000U : 2000U;
  if (!conversionPending && sinceRequest >= requestMs) {
    // Start the measurement before the request itself: OneWire disables
    // interrupts during bit slots, so counting only the later temperature read
    // concealed most of the disturbance we are trying to expose.
    missedAtRequest = vMissedIsrCycles;
    sensors.requestTemperatures();
    conversionPending = true;
    sinceRequest = 0;
    return;
  }
  if (!(conversionPending && sinceRequest >= 800)) {
    return;
  }
  conversionPending = false;

  // Re-scan only while something is missing, and not often. The search is the
  // expensive part of a 1-Wire transaction, so paying it every harvest to detect a
  // probe that is almost never hot-plugged is the wrong trade.
  if ((!hotAddrValid || !coldAddrValid) && sinceRescan >= 30000) {
    sinceRescan = 0;
    cacheProbeAddresses();
  }

  // Attribute the ISR disruption this harvest causes, so the OneWire cost is
  // measurable rather than inferred (see missedIsrCycles in /api/status).
  // By address, never by index: getTempCByIndex() would re-search the bus.
  hotDeciC = hotAddrValid ? toDeciC(sensors.getTempC(hotAddr)) : INT16_MIN;
  coldDeciC = coldAddrValid ? toDeciC(sensors.getTempC(coldAddr)) : INT16_MIN;
  if (hotDeciC == INT16_MIN) hotAddrValid = false;
  if (coldDeciC == INT16_MIN) coldAddrValid = false;
  chipDeciC = toDeciC(InternalTemperature.readTemperatureC());

  harvestMissedCycles = vMissedIsrCycles - missedAtRequest;

  // Derate on the worst of BOTH external probes and die temperature. Probe
  // role assignment affects labels only, never thermal protection.
  float worstC = -1000.0f;
  if (hotDeciC != INT16_MIN) {
    worstC = hotDeciC / 10.0f;
  }
  if (coldDeciC != INT16_MIN && coldDeciC / 10.0f > worstC) {
    worstC = coldDeciC / 10.0f;
  }
  if (chipDeciC != INT16_MIN && chipDeciC / 10.0f > worstC) {
    worstC = chipDeciC / 10.0f;
  }
  haveValidExternalSample = (hotDeciC != INT16_MIN) || (coldDeciC != INT16_MIN);
  derateMilli = haveValidExternalSample
                    ? thermalDerateMilli(worstC, config.Thermal.DerateStartC, config.Thermal.DerateEndC)
                    : 0;

  setThermalDerateMilli(derateMilli);

  // Open-loop: re-push the configured index so a recovering derate cap
  // restores the output. Skipped when another controller owns the index
  // target (closed-loop feedback and MPPT both re-push their own targets
  // every cycle - overwriting here would fight the tracker).
  if (!config.Feedback.Enabled && !config.Mppt.Enabled && spwmActive()) {
    uint16_t indexMilli = config.Pwm.Tm2.ModulationIndexMilli;
    if (indexMilli > MaxModulationIndexMilli) {
      indexMilli = MaxModulationIndexMilli;
    }
    setModulationIndexTargetQ15(indexMilliToQ15(indexMilli));
  }
}

bool thermalAllowsPwmRelease() {
  return !config.Thermal.Enabled || haveValidExternalSample;
}

uint16_t thermalDerateMilliNow() {
  return derateMilli;
}

int16_t thermalHotDeciC() {
  return hotDeciC;
}

int16_t thermalColdDeciC() {
  return coldDeciC;
}

int16_t thermalChipDeciC() {
  return chipDeciC;
}
