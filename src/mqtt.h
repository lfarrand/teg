#ifndef MQTT_H
#define MQTT_H

// MQTT telemetry with Home Assistant discovery.

#include <stdint.h>

void mqttConfigure(); // (re)apply config: disconnects so the task reconnects
void mqttTask();      // call from loop(); manages connection + publishing

bool mqttConnected();
uint32_t mqttPublishFailures();

#endif
