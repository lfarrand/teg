#include "config_json.h"
#include "FS.h"
#include "SdFat.h"
#include <ArduinoJson.h>
#include "config_serde.h"
#include <LittleFS.h>

extern SdFs sd;
extern LittleFS_QSPIFlash flashFS;
extern MainConfig config;

FLASHMEM void loadConfiguration(const char *filename) {
  Serial.println(F("Loading configuration from file"));
  Serial.println(F("Opening existing config file"));

  if (!sd.exists(filename)) {
    Serial.println(F("Config file does not exist, using defaults"));
    return;
  }

  FsFile file = sd.open(filename, FILE_READ);

  if (!file) {
    Serial.println(F("Failed to open config file for reading"));
    return;
  }

  JsonDocument doc;

  Serial.println(F("Deserializing config from file"));

  DeserializationError error = deserializeJson(doc, file);

  if (error) {
    Serial.println(F("Loading configuration failed, using default config"));
    Serial.println(error.f_str());
    return;
  }

  Serial.println(F("Config deserialized successfully"));

  configFromJson(doc, config);

  if (validateConfig(config)) {
    Serial.println(F("Invalid values in config; using defaults"));
  }

  Serial.println(F("Config loaded successfully"));

  file.close();
}

FLASHMEM void saveConfiguration(const char *filename) {
  Serial.println(F("Saving configuration to file"));

  if (sd.exists(filename)) {
    Serial.println(F("Deleting existing config file"));
    sd.remove(filename);
    Serial.println(F("Deleted existing config file"));
  } else {
    Serial.println(F("Existing config file did not exist"));
  }

  FsFile file = sd.open(filename, FILE_WRITE);
  if (!file) {
    Serial.println(F("Failed to create config file"));
    return;
  }

  JsonDocument doc;

  configToJson(config, doc);

  Serial.println(F("Writing config file to disk"));

  if (serializeJson(doc, file) == 0) {
    Serial.println(F("Failed to write to file"));
  }

  file.flush();

  Serial.println(F("Config saved successfully"));

  file.close();
}

FLASHMEM void printFile(const char *filename) {
  FsFile file = sd.open(filename);
  if (!file) {
    Serial.println(F("Failed to read file"));
    return;
  }

  while (file.available()) {
    Serial.print(static_cast<char>(file.read()));
  }
  Serial.println();

  file.close();
}

FLASHMEM void loadConfigurationFromFlash(const char* filename) {
  File file = flashFS.open(filename, FILE_READ);
  if (!file) {
    Serial.println(F("Failed to open config file from flash for reading"));
    return;
  }

  JsonDocument doc;

  DeserializationError error = deserializeJson(doc, file);

  if (error) {
    Serial.println(F("Loading configuration from flash failed, using default config"));
    Serial.println(error.f_str());
    return;
  }

  // Copy values as in loadConfiguration

  file.close();
}

FLASHMEM void saveConfigurationToFlash(const char* filename) {
  if (flashFS.exists(filename)) {
    flashFS.remove(filename);
  }

  File file = flashFS.open(filename, FILE_WRITE);
  if (!file) {
    Serial.println(F("Failed to create config file on flash"));
    return;
  }

  JsonDocument doc;

  // Set values as in saveConfiguration

  serializeJson(doc, file);

  file.close();
}