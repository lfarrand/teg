#ifndef MQTT_DISCOVERY_H
#define MQTT_DISCOVERY_H

// MQTT topic construction, Home Assistant discovery payloads and the state
// document: pure ArduinoJson/string building, no network dependencies,
// unit-tested natively. The connection lifecycle lives in mqtt.cpp.
//
// Layout: one retained discovery config per entity under
//   <prefix>/<component>/<nodeId>/<object>/config
// a single JSON state topic all sensors share via value_template
//   <base>/<nodeId>/state
// and a retained availability topic wired as the connection's last will
//   <base>/<nodeId>/availability   ("online"/"offline")
//
// Keys omitted from the state JSON (meter off, probe absent) render as the
// 'None' sentinel via the template default filter - the documented value
// that sets a numeric MQTT sensor to the 'unknown' state (a literal
// 'unknown' string would be rejected by numeric sensors with error spam).

#include <stdint.h>
#include <stdio.h>
#include <ArduinoJson.h>

struct MqttSensorDef {
  const char *object;      // topic object id + unique_id suffix
  const char *name;
  const char *unit;        // nullptr = unitless
  const char *deviceClass; // nullptr = none
  const char *stateClass;  // nullptr = none
  const char *stateKey;    // key in the state JSON
};

// The published entity set. Energy uses device_class=energy,
// state_class=total_increasing, unit Wh.
constexpr MqttSensorDef MqttSensors[] = {
  {"power", "Real power", "W", "power", "measurement", "power_w"},
  {"vrms", "Output voltage", "V", "voltage", "measurement", "vrms_v"},
  {"irms", "Output current", "A", "current", "measurement", "irms_a"},
  {"pf", "Power factor", nullptr, "power_factor", "measurement", "pf"},
  {"energy", "Energy", "Wh", "energy", "total_increasing", "energy_wh"},
  {"frequency", "Output frequency", "Hz", "frequency", "measurement", "freq_hz"},
  {"index", "Modulation index", nullptr, nullptr, "measurement", "index"},
  {"hot", "Temperature probe 1", "\xc2\xb0""C", "temperature", "measurement", "hot_c"},
  {"cold", "Temperature probe 2", "\xc2\xb0""C", "temperature", "measurement", "cold_c"},
  {"die", "MCU die", "\xc2\xb0""C", "temperature", "measurement", "die_c"},
  {"derate", "Thermal derate", "%", nullptr, "measurement", "derate_pct"},
  {"pll", "PLL state", nullptr, nullptr, nullptr, "pll"},
  // Aux power monitor: the MOSFET driver board's own supply, measured by its
  // on-board INA226 (total draw at the 14-26 V DC input, ahead of the eFuse)
  {"aux_power", "Driver board power", "W", "power", "measurement", "aux_power_w"},
  {"aux_voltage", "Driver board voltage", "V", "voltage", "measurement", "aux_voltage_v"},
  {"aux_current", "Driver board current", "A", "current", "measurement", "aux_current_a"},
  {"aux_energy", "Driver board energy", "Wh", "energy", "total_increasing", "aux_energy_wh"},
};
constexpr unsigned MqttSensorCount = sizeof(MqttSensors) / sizeof(MqttSensors[0]);

inline void mqttStateTopic(char *out, unsigned size, const char *base, const char *nodeId) {
  snprintf(out, size, "%s/%s/state", base, nodeId);
}

inline void mqttAvailabilityTopic(char *out, unsigned size, const char *base, const char *nodeId) {
  snprintf(out, size, "%s/%s/availability", base, nodeId);
}

inline void mqttDiscoveryTopic(char *out, unsigned size, const char *prefix,
                               const char *component, const char *nodeId, const char *object) {
  snprintf(out, size, "%s/%s/%s/%s/config", prefix, component, nodeId, object);
}

// Shared device block so HA groups every entity under one device, plus the
// recommended origin block identifying this firmware as the publisher
inline void mqttDeviceBlock(JsonDocument &doc, const char *nodeId, const char *fwVersion) {
  JsonObject dev = doc["device"].to<JsonObject>();
  dev["identifiers"][0] = nodeId;
  dev["name"] = "TEG Inverter";
  dev["model"] = "Teensy 4.1 PWM inverter";
  dev["sw_version"] = fwVersion;
  JsonObject origin = doc["origin"].to<JsonObject>();
  origin["name"] = "teg";
  origin["sw_version"] = fwVersion;
}

inline void mqttBuildSensorDiscovery(JsonDocument &doc, const MqttSensorDef &s,
                                     const char *base, const char *nodeId,
                                     const char *fwVersion) {
  doc.clear();
  doc["name"] = s.name;
  char buf[96];
  snprintf(buf, sizeof(buf), "%s_%s", nodeId, s.object);
  doc["unique_id"] = buf;
  mqttStateTopic(buf, sizeof(buf), base, nodeId);
  doc["state_topic"] = buf;
  mqttAvailabilityTopic(buf, sizeof(buf), base, nodeId);
  doc["availability_topic"] = buf;
  if (s.unit != nullptr) {
    doc["unit_of_measurement"] = s.unit;
  }
  if (s.deviceClass != nullptr) {
    doc["device_class"] = s.deviceClass;
  }
  if (s.stateClass != nullptr) {
    doc["state_class"] = s.stateClass;
  }
  snprintf(buf, sizeof(buf), "{{ value_json.%s | default('None') }}", s.stateKey);
  doc["value_template"] = buf;
  mqttDeviceBlock(doc, nodeId, fwVersion);
}

// The fault flag is a binary_sensor (problem class shows as an alert in HA)
inline void mqttBuildFaultDiscovery(JsonDocument &doc, const char *base, const char *nodeId,
                                    const char *fwVersion) {
  doc.clear();
  doc["name"] = "Fault";
  char buf[96];
  snprintf(buf, sizeof(buf), "%s_fault", nodeId);
  doc["unique_id"] = buf;
  mqttStateTopic(buf, sizeof(buf), base, nodeId);
  doc["state_topic"] = buf;
  mqttAvailabilityTopic(buf, sizeof(buf), base, nodeId);
  doc["availability_topic"] = buf;
  doc["device_class"] = "problem";
  doc["value_template"] = "{{ 'ON' if value_json.fault else 'OFF' }}";
  mqttDeviceBlock(doc, nodeId, fwVersion);
}

// Everything the state document needs, gathered by the task so this stays
// hardware-free and testable
struct MqttStateValues {
  bool meterValid = false;
  int32_t powerMw = 0;
  uint32_t vrmsMv = 0;
  uint32_t irmsMa = 0;
  int32_t pfMilli = 0;
  uint64_t energyMwh = 0;
  uint64_t modMilliHz = 0;
  uint32_t indexMilli = 0;
  int16_t hotDeciC = INT16_MIN; // INT16_MIN = probe absent
  int16_t coldDeciC = INT16_MIN;
  int16_t dieDeciC = INT16_MIN;
  uint16_t derateMilli = 1000;
  bool fault = false;
  const char *pllState = "off";
  bool auxValid = false;   // INA226 on the driver board answered
  uint32_t auxPowerMw = 0;
  uint32_t auxBusMv = 0;
  int32_t auxCurrentMa = 0;
  uint64_t auxEnergyMwh = 0;
  bool auxAlert = false;
};

inline void mqttBuildState(JsonDocument &doc, const MqttStateValues &v) {
  doc.clear();
  if (v.meterValid) {
    doc["power_w"] = static_cast<float>(v.powerMw) / 1000.0f;
    doc["vrms_v"] = static_cast<float>(v.vrmsMv) / 1000.0f;
    doc["irms_a"] = static_cast<float>(v.irmsMa) / 1000.0f;
    doc["pf"] = static_cast<float>(v.pfMilli) / 1000.0f;
    doc["energy_wh"] = static_cast<float>(static_cast<double>(v.energyMwh) / 1000.0);
  }
  doc["freq_hz"] = static_cast<float>(static_cast<double>(v.modMilliHz) / 1000.0);
  doc["index"] = static_cast<float>(v.indexMilli) / 1000.0f;
  if (v.hotDeciC != INT16_MIN) {
    doc["hot_c"] = static_cast<float>(v.hotDeciC) / 10.0f;
  }
  if (v.coldDeciC != INT16_MIN) {
    doc["cold_c"] = static_cast<float>(v.coldDeciC) / 10.0f;
  }
  if (v.dieDeciC != INT16_MIN) {
    doc["die_c"] = static_cast<float>(v.dieDeciC) / 10.0f;
  }
  doc["derate_pct"] = v.derateMilli / 10;
  doc["fault"] = v.fault;
  doc["pll"] = v.pllState;
  if (v.auxValid) {
    doc["aux_power_w"] = static_cast<float>(v.auxPowerMw) / 1000.0f;
    doc["aux_voltage_v"] = static_cast<float>(v.auxBusMv) / 1000.0f;
    doc["aux_current_a"] = static_cast<float>(v.auxCurrentMa) / 1000.0f;
    doc["aux_energy_wh"] = static_cast<float>(static_cast<double>(v.auxEnergyMwh) / 1000.0);
    doc["aux_alert"] = v.auxAlert;
  }
}

#endif
