// Tests for the MQTT topic/payload building (mqtt_discovery.h): Home
// Assistant discovery documents, the shared state JSON, and topic layout.

#include <unity.h>
#include <string.h>
#include <mqtt_discovery.h>

void setUp() {}
void tearDown() {}

void test_topic_layout() {
  char buf[96];
  mqttStateTopic(buf, sizeof(buf), "teg", "teg-12345");
  TEST_ASSERT_EQUAL_STRING("teg/teg-12345/state", buf);
  mqttAvailabilityTopic(buf, sizeof(buf), "teg", "teg-12345");
  TEST_ASSERT_EQUAL_STRING("teg/teg-12345/availability", buf);
  mqttDiscoveryTopic(buf, sizeof(buf), "homeassistant", "sensor", "teg-12345", "power");
  TEST_ASSERT_EQUAL_STRING("homeassistant/sensor/teg-12345/power/config", buf);
  mqttDiscoveryTopic(buf, sizeof(buf), "homeassistant", "binary_sensor", "teg-12345", "fault");
  TEST_ASSERT_EQUAL_STRING("homeassistant/binary_sensor/teg-12345/fault/config", buf);
}

void test_power_sensor_discovery() {
  JsonDocument doc;
  mqttBuildSensorDiscovery(doc, MqttSensors[0], "teg", "teg-12345", "abc1234");
  TEST_ASSERT_EQUAL_STRING("Real power", doc["name"]);
  TEST_ASSERT_EQUAL_STRING("teg-12345_power", doc["unique_id"]);
  TEST_ASSERT_EQUAL_STRING("teg/teg-12345/state", doc["state_topic"]);
  TEST_ASSERT_EQUAL_STRING("teg/teg-12345/availability", doc["availability_topic"]);
  TEST_ASSERT_EQUAL_STRING("W", doc["unit_of_measurement"]);
  TEST_ASSERT_EQUAL_STRING("power", doc["device_class"]);
  TEST_ASSERT_EQUAL_STRING("measurement", doc["state_class"]);
  // 'None' is HA's documented sentinel that sets a numeric sensor to the
  // 'unknown' STATE; a literal 'unknown' string would be rejected by
  // numeric device classes with an error logged per publish
  TEST_ASSERT_EQUAL_STRING("{{ value_json.power_w | default('None') }}",
                           doc["value_template"]);
  TEST_ASSERT_EQUAL_STRING("teg-12345", doc["device"]["identifiers"][0]);
  TEST_ASSERT_EQUAL_STRING("abc1234", doc["device"]["sw_version"]);
  TEST_ASSERT_EQUAL_STRING("teg", doc["origin"]["name"]);
  TEST_ASSERT_EQUAL_STRING("abc1234", doc["origin"]["sw_version"]);
}

void test_energy_sensor_payload_shape() {
  // Energy discovery payload shape: device_class energy, state_class
  // total_increasing, unit Wh.
  const MqttSensorDef *energy = nullptr;
  for (unsigned i = 0; i < MqttSensorCount; i++) {
    if (strcmp(MqttSensors[i].object, "energy") == 0) {
      energy = &MqttSensors[i];
    }
  }
  TEST_ASSERT_NOT_NULL(energy);
  JsonDocument doc;
  mqttBuildSensorDiscovery(doc, *energy, "teg", "n", "v");
  TEST_ASSERT_EQUAL_STRING("energy", doc["device_class"]);
  TEST_ASSERT_EQUAL_STRING("total_increasing", doc["state_class"]);
  TEST_ASSERT_EQUAL_STRING("Wh", doc["unit_of_measurement"]);
}

void test_fault_binary_sensor() {
  JsonDocument doc;
  mqttBuildFaultDiscovery(doc, "teg", "teg-9", "v1");
  TEST_ASSERT_EQUAL_STRING("problem", doc["device_class"]);
  TEST_ASSERT_EQUAL_STRING("teg-9_fault", doc["unique_id"]);
  TEST_ASSERT_EQUAL_STRING("{{ 'ON' if value_json.fault else 'OFF' }}",
                           doc["value_template"]);
  TEST_ASSERT_EQUAL_STRING("teg/teg-9/state", doc["state_topic"]);
}

void test_unique_ids_are_unique() {
  for (unsigned i = 0; i < MqttSensorCount; i++) {
    for (unsigned j = i + 1; j < MqttSensorCount; j++) {
      TEST_ASSERT_TRUE(strcmp(MqttSensors[i].object, MqttSensors[j].object) != 0);
      TEST_ASSERT_TRUE(strcmp(MqttSensors[i].stateKey, MqttSensors[j].stateKey) != 0);
    }
  }
}

void test_discovery_fits_publish_buffer() {
  // mqtt.cpp serializes into 640 bytes and publishes through a 1024-byte
  // client buffer (which also carries the topic + MQTT overhead)
  JsonDocument doc;
  char payload[640];
  for (unsigned i = 0; i < MqttSensorCount; i++) {
    mqttBuildSensorDiscovery(doc, MqttSensors[i], "a-long-base-topic-here",
                             "teg-4294967295", "0123456789abcdef");
    const size_t n = serializeJson(doc, payload, sizeof(payload));
    TEST_ASSERT_LESS_THAN_UINT32(sizeof(payload) - 1, n);
  }
}

void test_state_document_full() {
  MqttStateValues v;
  v.meterValid = true;
  v.powerMw = 12345;
  v.vrmsMv = 230500;
  v.irmsMa = 1500;
  v.pfMilli = 987;
  v.energyMwh = 4500123;
  v.modMilliHz = 50123;
  v.indexMilli = 850;
  v.hotDeciC = 755;
  v.coldDeciC = 251;
  v.dieDeciC = 402;
  v.derateMilli = 900;
  v.fault = true;
  v.pllState = "locked";
  JsonDocument doc;
  mqttBuildState(doc, v);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.345f, doc["power_w"].as<float>());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 230.5f, doc["vrms_v"].as<float>());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, doc["irms_a"].as<float>());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.987f, doc["pf"].as<float>());
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 4500.123f, doc["energy_wh"].as<float>());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.123f, doc["freq_hz"].as<float>());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.85f, doc["index"].as<float>());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 75.5f, doc["hot_c"].as<float>());
  TEST_ASSERT_EQUAL_INT(90, doc["derate_pct"].as<int>());
  TEST_ASSERT_TRUE(doc["fault"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("locked", doc["pll"]);
}

void test_state_document_omits_missing() {
  // Meter off and no probes: those keys must be ABSENT (the discovery
  // templates turn absent into 'unknown'; null would raise template errors)
  MqttStateValues v; // defaults: meterValid false, temps INT16_MIN
  JsonDocument doc;
  mqttBuildState(doc, v);
  TEST_ASSERT_FALSE(doc["power_w"].is<float>());
  TEST_ASSERT_FALSE(doc["energy_wh"].is<float>());
  TEST_ASSERT_FALSE(doc["hot_c"].is<float>());
  TEST_ASSERT_TRUE(doc["freq_hz"].is<float>()); // always present
  TEST_ASSERT_TRUE(doc["fault"].is<bool>());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_topic_layout);
  RUN_TEST(test_power_sensor_discovery);
  RUN_TEST(test_energy_sensor_payload_shape);
  RUN_TEST(test_fault_binary_sensor);
  RUN_TEST(test_unique_ids_are_unique);
  RUN_TEST(test_discovery_fits_publish_buffer);
  RUN_TEST(test_state_document_full);
  RUN_TEST(test_state_document_omits_missing);
  return UNITY_END();
}
