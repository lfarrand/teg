// MQTT / Home Assistant (item: MQTT). Pure payload building lives in
// mqtt_discovery.h; this file owns the PubSubClient connection lifecycle.
//
// Behavior: connect with a last-will "offline" on the availability topic;
// on each (re)connect publish the retained discovery configs ONE PER TASK
// PASS (a back-to-back burst of all 17 exceeds lwIP's TCP send buffer and
// corrupts the MQTT stream mid-frame), then a state immediately once
// discovery completes, then every IntervalSeconds.
//
// Blocking budget: TCP connect capped at 500ms and the MQTT CONNACK wait at
// 2s (PubSubClient's default is 15s - longer than the 8s watchdog, so a TCP
// endpoint that accepts but never answers, e.g. a TLS-only listener, would
// otherwise hard-reset the inverter). A hostname broker with a dead DNS
// server can still stall ~5s inside QNEthernet's resolver, so failed
// attempts back off 10s -> 60s and the watchdog is kicked around the
// attempt. Control-loop note: the PLL treats any loop() stall > 250ms as a
// resync (drop backlog, hold frequency) by design.

#include "mqtt.h"
#include "mqtt_discovery.h"
#include "config_json.h"
#include "pwm_utils.h"
#include "thermal.h"
#include "meter.h"
#include "power_monitor.h"
#include "pll.h"
#include "utils.h"
#include "main.h"
#if __has_include("version.h")
#include "version.h"
#else
#define TEG_GIT_HASH "dev"
#endif
#include <Arduino.h>
#include <TeensyID.h>
#include <PubSubClient.h>
#include <QNEthernet.h>

extern MainConfig config;

static qindesign::network::EthernetClient mqttNet;
static PubSubClient mq(mqttNet);
static char nodeId[24] = "";
static bool discoveryPublished = false;
static unsigned discoveryIdx = 0;
static elapsedMillis sinceAttempt = 60000; // first attempt immediately
static elapsedMillis sincePublish;
static uint32_t backoffMs = 10000;
static uint32_t publishFailures = 0;
static uint64_t energyHighWaterMwh = 0;
static uint64_t auxEnergyHighWaterMwh = 0;
static IPAddress mqttAddr;
static bool mqttAddrValid = false;

FLASHMEM static void ensureNodeId() {
  if (nodeId[0] == '\0') {
    snprintf(nodeId, sizeof(nodeId), "teg-%lu", static_cast<unsigned long>(teensyUsbSN()));
  }
}

FLASHMEM void mqttConfigure() {
  ensureNodeId();
  if (mq.connected()) {
    // Publish the will manually before a clean disconnect: PubSubClient's
    // DISCONNECT packet suppresses the broker-side last will
    char availTopic[64];
    mqttAvailabilityTopic(availTopic, sizeof(availTopic), config.Mqtt.BaseTopic, nodeId);
    mq.publish(availTopic, "offline", true);
    mq.disconnect();
  }
  discoveryPublished = false;
  mqttAddrValid = false;
  sinceAttempt = 60000; // reconnect with the new settings on the next task pass
}

FLASHMEM static bool mqttConnect() {
  if (!mqttAddrValid) {
    mqttAddrValid = mqttAddr.fromString(config.Mqtt.Host);
    if (!mqttAddrValid) {
      // Hostname resolution is synchronous. Setup primes it while outputs are
      // inhibited; subsequent reconnects never spend seconds in DNS while the
      // bridge is switching.
      if (!pwmOutputInhibited()) return false;
      kickWatchdog();
      mqttAddrValid = Ethernet.hostByName(config.Mqtt.Host, mqttAddr);
      kickWatchdog();
      if (!mqttAddrValid) return false;
    }
  }
  if (!pwmOutputInhibited()) return false;
  mqttNet.setConnectionTimeout(500);
  mq.setSocketTimeout(2); // CONNACK wait, seconds; default 15s outlives the watchdog
  mq.setServer(mqttAddr, config.Mqtt.Port);
  mq.setBufferSize(1024);
  char availTopic[64];
  mqttAvailabilityTopic(availTopic, sizeof(availTopic), config.Mqtt.BaseTopic, nodeId);
  const char *user = config.Mqtt.Username[0] != '\0' ? config.Mqtt.Username : nullptr;
  const char *pass = config.Mqtt.Password[0] != '\0' ? config.Mqtt.Password : nullptr;
  kickWatchdog();
  const bool ok = mq.connect(nodeId, user, pass, availTopic, 0, true, "offline");
  kickWatchdog();
  if (!ok) {
    return false;
  }
  mq.publish(availTopic, "online", true);
  return true;
}

FLASHMEM static void publishState();

// One retained discovery config per task pass: pacing lets lwIP drain the
// send buffer between packets. When the last one lands, the first state
// follows immediately so entities populate without waiting an interval.
FLASHMEM static void publishDiscoveryStep() {
  JsonDocument doc;
  char topic[96];
  char payload[640];
  if (discoveryIdx < MqttSensorCount) {
    mqttBuildSensorDiscovery(doc, MqttSensors[discoveryIdx], config.Mqtt.BaseTopic, nodeId,
                             TEG_GIT_HASH);
    mqttDiscoveryTopic(topic, sizeof(topic), config.Mqtt.DiscoveryPrefix, "sensor", nodeId,
                       MqttSensors[discoveryIdx].object);
    const size_t n = serializeJson(doc, payload, sizeof(payload));
    if (mq.publish(topic, reinterpret_cast<const uint8_t *>(payload), n, true)) {
      mqttNet.flush();
      discoveryIdx++;
    }
    return;
  }
  mqttBuildFaultDiscovery(doc, config.Mqtt.BaseTopic, nodeId, TEG_GIT_HASH);
  mqttDiscoveryTopic(topic, sizeof(topic), config.Mqtt.DiscoveryPrefix, "binary_sensor", nodeId,
                     "fault");
  const size_t n = serializeJson(doc, payload, sizeof(payload));
  if (mq.publish(topic, reinterpret_cast<const uint8_t *>(payload), n, true)) {
    mqttNet.flush();
    discoveryPublished = true;
    publishState();
    sincePublish = 0;
  }
}

FLASHMEM static void publishState() {
  MqttStateValues v;
  const MeterReadings m = meterReadings();
  v.meterValid = m.valid;
  v.powerMw = m.powerMw;
  v.vrmsMv = m.vrmsMv;
  v.irmsMa = m.irmsMa;
  v.pfMilli = m.pfMilli;
  // Monotonic high-water mark: the net-energy accumulator can dip (reverse
  // flow, offset noise) and HA's total_increasing statistics would read any
  // >10% dip as a meter reset and double-count the recovery
  const uint64_t e = static_cast<uint64_t>(meterEnergyMwh());
  if (e > energyHighWaterMwh) {
    energyHighWaterMwh = e;
  }
  v.energyMwh = energyHighWaterMwh;
  v.modMilliHz = modulationActualMilliHz();
  v.indexMilli = modulationIndexNowMilli();
  v.hotDeciC = thermalHotDeciC();
  v.coldDeciC = thermalColdDeciC();
  v.dieDeciC = thermalChipDeciC();
  v.derateMilli = thermalDerateMilliNow();
  v.fault = vFaultTripped;
  v.pllState = pllStateStr();
  const PowerMonReadings aux = powerMonReadings();
  v.auxValid = aux.valid;
  if (aux.valid) {
    v.auxPowerMw = aux.powerMw;
    v.auxBusMv = aux.busMv;
    v.auxCurrentMa = aux.currentMa;
    // Same monotonic guard as the meter energy: HA's total_increasing reads
    // a dip as a meter reset and double-counts the recovery
    const uint64_t ae = powerMonEnergyMwh();
    if (ae > auxEnergyHighWaterMwh) {
      auxEnergyHighWaterMwh = ae;
    }
    v.auxEnergyMwh = auxEnergyHighWaterMwh;
    v.auxAlert = aux.alert;
  }

  JsonDocument doc;
  mqttBuildState(doc, v);
  char topic[64];
  char payload[512];
  mqttStateTopic(topic, sizeof(topic), config.Mqtt.BaseTopic, nodeId);
  const size_t n = serializeJson(doc, payload, sizeof(payload));
  if (!mq.publish(topic, reinterpret_cast<const uint8_t *>(payload), n, false)) {
    publishFailures++;
  }
}

void mqttTask() {
  if (!config.Mqtt.Enabled || config.Mqtt.Host[0] == '\0') {
    if (mq.connected()) {
      mqttConfigure(); // graceful offline + disconnect
    }
    return;
  }
  ensureNodeId();

  if (!mq.connected()) {
    if (sinceAttempt < backoffMs) {
      return;
    }
    sinceAttempt = 0;
    discoveryPublished = false;
    discoveryIdx = 0;
    if (!mqttConnect()) {
      // A dead DNS/black-holed broker costs up to ~2.5s per attempt (5s
      // with a stalled resolver): back off so the web UI and control tasks
      // keep their loop() share
      backoffMs = backoffMs >= 30000 ? 60000 : backoffMs * 2;
      return;
    }
    backoffMs = 10000;
    if (!config.Mqtt.DiscoveryEnabled) {
      publishState(); // no discovery phase: populate the state right away
      sincePublish = 0;
    }
  }

  mq.loop(); // keepalive + broker traffic

  if (!discoveryPublished && config.Mqtt.DiscoveryEnabled) {
    publishDiscoveryStep();
  }

  const uint32_t intervalMs = static_cast<uint32_t>(config.Mqtt.IntervalSeconds) * 1000UL;
  if (config.Mqtt.IntervalSeconds != 0 && sincePublish >= intervalMs) {
    sincePublish = 0;
    publishState();
  }
}

bool mqttConnected() {
  return mq.connected();
}

uint32_t mqttPublishFailures() {
  return publishFailures;
}
