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

  // The preset/import path refuses an incomplete document; the boot path did not, so a
  // settings file missing a safety section silently fell back to compiled defaults -
  // which have fault protection, the current limit and thermal derating all DISABLED.
  // Loading it anyway is still the right call at boot (refusing would leave those same
  // defaults, just with no operator settings either), but it must not be silent.
  if (!configDocComplete(doc)) {
    Serial.println(F("WARNING: config file is missing sections; defaults will apply "
                     "to them, and defaults DISABLE fault protection, the current "
                     "limit and thermal derating"));
  }

  configFromJson(doc, config);

  if (validateConfig(config)) {
    Serial.println(F("Invalid values in config; using defaults"));
  }

  Serial.println(F("Config loaded successfully"));

  file.close();
}

FLASHMEM void saveConfiguration(const char *filename) {
  extern bool sdAvailable;
  if (!sdAvailable) {
    Serial.println(F("No SD card - configuration not persisted"));
    return;
  }

  Serial.println(F("Saving configuration to file"));

  // Write to a temporary file and swap, rather than deleting the live one first.
  // The old sequence - remove(), then open(), then serialize - left a window in which
  // a reset, a brownout or a card pull destroyed every setting: the fault-protection
  // and current-limit configuration, and since 2026-07-30 the generated write PIN too,
  // which would then be silently reissued on the next boot. presets.cpp already used
  // temp-file-and-swap for exactly this reason.
  //
  // This is not a true atomic rename - FAT offers none, and the remove/rename pair
  // below still has a small window - but it is bounded by two directory operations
  // rather than by the whole serialization, and the old file survives until the new
  // one is completely written and flushed.
  char tmpName[64];
  snprintf(tmpName, sizeof(tmpName), "%s.tmp", filename);
  sd.remove(tmpName); // a leftover from an interrupted save is not interesting

  FsFile file = sd.open(tmpName, FILE_WRITE);
  if (!file) {
    Serial.println(F("Failed to create temporary config file"));
    return;
  }

  JsonDocument doc;

  configToJson(config, doc);

  Serial.println(F("Writing config file to disk"));

  const bool written = serializeJson(doc, file) != 0;
  if (!written) {
    Serial.println(F("Failed to write to file"));
  }

  file.flush();
  file.close();

  if (!written) {
    // Leave the existing configuration untouched rather than swapping in a partial
    // file. A stale config is recoverable; a truncated one is not.
    sd.remove(tmpName);
    return;
  }

  sd.remove(filename);
  if (!sd.rename(tmpName, filename)) {
    Serial.println(F("Failed to swap in the new config file"));
    return;
  }

  Serial.println(F("Config saved successfully"));
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
