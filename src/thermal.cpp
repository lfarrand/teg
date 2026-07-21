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
    sensors.begin();
    sensors.setWaitForConversion(false); // never block loop() on a conversion
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

  hotDeciC = toDeciC(sensors.getTempCByIndex(0));
  coldDeciC = toDeciC(sensors.getTempCByIndex(1));
  chipDeciC = toDeciC(InternalTemperature.readTemperatureC());

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
  // restores the output (closed-loop pushes its own target every cycle)
  if (!config.Feedback.Enabled && spwmActive()) {
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
