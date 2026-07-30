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

// Cached ROM addresses. getTempCByIndex() calls getAddress(), which runs a FULL
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

uint32_t thermalHarvestMissedCycles() {
  return harvestMissedCycles;
}

// Search the bus and cache whatever is present. Also called periodically while a
// probe is missing, so one connected later is still picked up.
static void cacheProbeAddresses() {
  sensors.begin(); // re-counts devices; getAddress() gates on that count
  if (!hotAddrValid) {
    hotAddrValid = sensors.getAddress(hotAddr, 0);
  }
  if (!coldAddrValid) {
    coldAddrValid = sensors.getAddress(coldAddr, 1);
  }
}

void thermalConfigure() {
  if (!config.Thermal.Enabled) {
    derateMilli = 1000;
    hotDeciC = coldDeciC = chipDeciC = INT16_MIN;
    return;
  }
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

  // Non-blocking cadence: request conversions, harvest them 800ms later
  // (a 12-bit DS18B20 conversion takes 750ms)
  static elapsedMillis sinceRequest;
  static bool conversionPending = false;

  if (!conversionPending && sinceRequest >= 2000) {
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
  static elapsedMillis sinceRescan;
  if ((!hotAddrValid || !coldAddrValid) && sinceRescan >= 30000) {
    sinceRescan = 0;
    cacheProbeAddresses();
  }

  // Attribute the ISR disruption this harvest causes, so the OneWire cost is
  // measurable rather than inferred (see missedIsrCycles in /api/status).
  const uint32_t missedBefore = vMissedIsrCycles;

  // By address, never by index: getTempCByIndex() would re-search the bus.
  hotDeciC = hotAddrValid ? toDeciC(sensors.getTempC(hotAddr)) : INT16_MIN;
  coldDeciC = coldAddrValid ? toDeciC(sensors.getTempC(coldAddr)) : INT16_MIN;
  chipDeciC = toDeciC(InternalTemperature.readTemperatureC());

  harvestMissedCycles = vMissedIsrCycles - missedBefore;

  // Derate on the worst of hot-side probe and die temperature
  float worstC = -1000.0f;
  if (hotDeciC != INT16_MIN) {
    worstC = hotDeciC / 10.0f;
  }
  if (chipDeciC != INT16_MIN && chipDeciC / 10.0f > worstC) {
    worstC = chipDeciC / 10.0f;
  }
  derateMilli = worstC > -999.0f
                  ? thermalDerateMilli(worstC, config.Thermal.DerateStartC, config.Thermal.DerateEndC)
                  : 1000;

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
