#ifndef PRESETS_H
#define PRESETS_H

// Named configuration presets stored on the SD card. Presets never contain
// secrets: they are written redacted and loading preserves whatever is
// currently stored, so a preset file can be copied off the card or emailed
// without leaking the InfluxDB token, MQTT password or write PIN.

#include <stdint.h>
#include <ArduinoJson.h>

// Fills doc with {"presets":["name",...]}; returns false when the SD card
// or the presets directory is unavailable
bool presetList(JsonDocument &doc);

// Save the CURRENT configuration under name (overwrites). errorOut is set
// on failure.
bool presetSave(const char *name, const char **errorOut);

// Load a preset into the live config: secrets preserved, values validated,
// hardware applied, and the result persisted to the main settings file.
bool presetLoad(const char *name, const char **errorOut);

bool presetDelete(const char *name, const char **errorOut);

// Apply a whole-config document (preset file or imported settings): secrets
// are always taken from the running config, missing sections are rejected
// rather than defaulted, and the result is validated, applied and queued
// for persistence.
bool configApplyDocument(const JsonDocument &doc, const char **errorOut);

#endif
