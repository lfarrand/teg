#include "config_json.h"
#include "FS.h"
#include "SdFat.h"
#include <ArduinoJson.h>
#include "config_serde.h"
#include "ota_crc.h"
#include "pin_ownership.h"
#include <LittleFS.h>

extern SdFs sd;
extern LittleFS_QSPIFlash flashFS;
extern MainConfig config;

static uint32_t configGeneration = 0;

namespace {
class CrcPrint final : public Print {
 public:
  size_t write(uint8_t c) override {
    crc_ = otaCrc32Update(crc_, &c, 1);
    return 1;
  }
  uint32_t value() const { return crc_ ^ 0xFFFFFFFFu; }

 private:
  uint32_t crc_ = 0xFFFFFFFFu;
};

static uint32_t configPayloadCrc(const JsonDocument &doc) {
  CrcPrint sink;
  serializeJson(doc["Config"], sink);
  return sink.value();
}

static bool configIntegrity(const JsonDocument &doc, uint32_t &generation) {
  const JsonVariantConst gen = doc["StorageGeneration"];
  const JsonVariantConst crc = doc["StorageCrc32"];
  if (gen.isNull() && crc.isNull()) {
    generation = 0; // complete pre-integrity document; upgraded on next save
    return true;
  }
  if (gen.isNull() || crc.isNull() ||
      !(gen.is<int64_t>() || gen.is<uint64_t>()) ||
      !(crc.is<int64_t>() || crc.is<uint64_t>())) {
    return false;
  }
  const uint64_t g = gen.as<uint64_t>();
  const uint64_t c = crc.as<uint64_t>();
  if (g == 0 || g > UINT32_MAX || c > UINT32_MAX) {
    return false;
  }
  generation = static_cast<uint32_t>(g);
  return static_cast<uint32_t>(c) == configPayloadCrc(doc);
}

static bool generationNewer(uint32_t candidate, uint32_t current) {
  return static_cast<int32_t>(candidate - current) > 0;
}
} // namespace

static bool siblingName(const char *filename, const char *suffix, char *out,
                        size_t outSize) {
  const int n = snprintf(out, outSize, "%s%s", filename, suffix);
  return n > 0 && static_cast<size_t>(n) < outSize;
}

static bool readConfigDocument(const char *path, JsonDocument &doc,
                               uint32_t *generation = nullptr) {
  if (!sd.exists(path)) {
    return false;
  }
  FsFile file = sd.open(path, FILE_READ);
  if (!file) {
    return false;
  }
  const DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    Serial.print(F("Invalid JSON in "));
    Serial.print(path);
    Serial.print(F(": "));
    Serial.println(error.f_str());
    return false;
  }
  if (!configDocComplete(doc)) {
    Serial.print(F("Rejected incomplete or incompatible configuration: "));
    Serial.println(path);
    return false;
  }
  uint32_t parsedGeneration = 0;
  if (!configIntegrity(doc, parsedGeneration)) {
    Serial.print(F("Rejected configuration with invalid generation/CRC: "));
    Serial.println(path);
    return false;
  }
  if (generation != nullptr) {
    *generation = parsedGeneration;
  }
  return true;
}

FLASHMEM bool loadConfiguration(const char *filename) {
  Serial.println(F("Loading configuration"));

  char tmpName[96];
  char bakName[96];
  if (!siblingName(filename, ".tmp", tmpName, sizeof(tmpName)) ||
      !siblingName(filename, ".bak", bakName, sizeof(bakName))) {
    Serial.println(F("Configuration path is too long"));
    return false;
  }

  const char *candidates[] = {filename, tmpName, bakName};
  MainConfig bestConfig;
  uint8_t bestIndex = 0xFF;
  uint32_t bestGeneration = 0;
  for (uint8_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
    JsonDocument doc;
    uint32_t generation = 0;
    if (!readConfigDocument(candidates[i], doc, &generation)) {
      continue;
    }

    // Decode into a temporary object. The live configuration changes only
    // after the entire document has passed shape and range validation.
    MainConfig loaded;
    configFromJson(doc, loaded);
    if (validateConfig(loaded)) {
      Serial.print(F("Out-of-range configuration values corrected in "));
      Serial.println(candidates[i]);
    }
    PinValidationResult pinResult;
    if (!validatePinOwnership(loaded, &pinResult)) {
      Serial.print(F("Rejected configuration pin conflict on pin "));
      Serial.println(pinResult.pin);
      continue;
    }
    if (bestIndex == 0xFF || generationNewer(generation, bestGeneration)) {
      memcpy(&bestConfig, &loaded, sizeof(bestConfig));
      bestIndex = i;
      bestGeneration = generation;
    }
  }

  if (bestIndex != 0xFF) {
    memcpy(&config, &bestConfig, sizeof(config));
    configGeneration = bestGeneration;

    if (bestIndex == 1) {
      // Power failed after the verified temp file was written but before the
      // directory swap. Keep any damaged live file as .bad, then finish the
      // interrupted transaction. Failure to promote is non-fatal: the valid
      // temp remains available on the next boot.
      char badName[96];
      if (siblingName(filename, ".bad", badName, sizeof(badName))) {
        sd.remove(badName);
        if (sd.exists(filename)) {
          sd.rename(filename, badName);
        }
      }
      if (sd.rename(tmpName, filename)) {
        Serial.println(F("Recovered configuration from verified temporary file"));
      } else {
        Serial.println(F("Loaded temporary configuration; promotion failed"));
      }
    } else if (bestIndex == 2) {
      char badName[96];
      if (siblingName(filename, ".bad", badName, sizeof(badName))) {
        sd.remove(badName);
        if (sd.exists(filename)) {
          sd.rename(filename, badName);
        }
      }
      if (sd.rename(bakName, filename)) {
        Serial.println(F("Recovered configuration from backup and repaired live file"));
      } else {
        Serial.println(F("Loaded backup configuration; promotion failed"));
      }
    } else {
      Serial.println(F("Configuration loaded successfully"));
    }
    return true;
  }

  Serial.println(F("No complete configuration found; fail-dark defaults loaded and PWM release inhibited"));
  return false;
}

FLASHMEM bool saveConfiguration(const char *filename) {
  extern bool sdAvailable;
  if (!sdAvailable) {
    Serial.println(F("No SD card - configuration not persisted"));
    return false;
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
  char tmpName[96];
  char bakName[96];
  if (!siblingName(filename, ".tmp", tmpName, sizeof(tmpName)) ||
      !siblingName(filename, ".bak", bakName, sizeof(bakName))) {
    Serial.println(F("Configuration path is too long"));
    return false;
  }
  // loadConfiguration() has already had the opportunity to recover a valid
  // leftover temp. At runtime this is now an obsolete transaction.
  sd.remove(tmpName);

  FsFile file = sd.open(tmpName, FILE_WRITE);
  if (!file) {
    Serial.println(F("Failed to create temporary config file"));
    return false;
  }

  JsonDocument doc;

  configToJson(config, doc);
  uint32_t nextGeneration = configGeneration + 1U;
  if (nextGeneration == 0) nextGeneration = 1; // reserve zero for legacy files
  doc["StorageGeneration"] = nextGeneration;
  doc["StorageCrc32"] = configPayloadCrc(doc);

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
    return false;
  }

  // Reopen and parse what the card actually stored before moving either live
  // directory entry. A successful serialize() alone cannot detect every card
  // removal or short-write failure.
  JsonDocument verify;
  uint32_t verifiedGeneration = 0;
  if (!readConfigDocument(tmpName, verify, &verifiedGeneration) ||
      verifiedGeneration != nextGeneration) {
    Serial.println(F("Temporary configuration failed read-back verification"));
    sd.remove(tmpName);
    return false;
  }

  // Keep one known-previous copy. At every power-loss point at least one of
  // live, tmp or backup contains a complete document, and loadConfiguration()
  // checks all three in that order.
  sd.remove(bakName);
  const bool hadLive = sd.exists(filename);
  if (hadLive && !sd.rename(filename, bakName)) {
    Serial.println(F("Failed to preserve previous configuration"));
    return false; // live and verified tmp both remain recoverable
  }
  if (!sd.rename(tmpName, filename)) {
    Serial.println(F("Failed to swap in new configuration; restoring backup"));
    if (hadLive && !sd.exists(filename)) {
      sd.rename(bakName, filename);
    }
    return false;
  }

  configGeneration = nextGeneration;
  Serial.println(F("Config saved successfully"));
  return true;
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
