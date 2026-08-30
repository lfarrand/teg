// Configuration presets (item: presets + export/import).
//
// Presets are ordinary config JSON written to /presets/<name>.json with the
// secrets blanked, so a preset is safe to copy off the card or share. On
// load, restoreSecrets() restores the write PIN always. MQTT/Influx secrets
// restore only when the endpoint identity matches; otherwise they are cleared
// and that integration is disabled.
//
// Name validation lives in preset_name.h and is a security boundary: names
// arrive from the network and become filenames.

#include "presets.h"
#include "preset_name.h"
#include "config_json.h"
#include "config_serde.h"
#include "pin_ownership.h"
#include "pwm_utils.h"
#include "web_handlers.h" // configSaveNeeded
#include "utils.h"
#include "FS.h"
#include "SdFat.h"
#include <string.h>

extern SdFs sd;
extern MainConfig config;
extern bool sdAvailable;

// Walk the presets directory, invoking fn for each valid preset name.
// Returns false only when the card is unavailable.
static bool presetForEach(void *ctx, bool (*fn)(void *, const char *)) {
  if (!sdAvailable) {
    return false;
  }
  FsFile dir = sd.open(PresetDir);
  if (!dir) {
    return true; // nothing saved yet: an empty list, not an error
  }
  if (!dir.isDirectory()) {
    dir.close();
    return true;
  }
  FsFile entry;
  char fname[PresetPathMax + 16]; // room for names longer than a valid preset
  char name[PresetNameMax + 1];
  while (entry.openNext(&dir, O_RDONLY)) {
    const bool isDir = entry.isDirectory();
    const bool named = entry.getName(fname, sizeof(fname));
    entry.close();
    if (!isDir && named && presetNameFromFile(name, sizeof(name), fname)) {
      if (!fn(ctx, name)) {
        break;
      }
    }
  }
  dir.close();
  return true;
}

bool presetList(JsonDocument &doc) {
  JsonArray arr = doc["presets"].to<JsonArray>();
  return presetForEach(&arr, [](void *ctx, const char *name) {
    static_cast<JsonArray *>(ctx)->add(name);
    return true;
  });
}

// Find the stored spelling of a name, comparing the way FAT does. Returns
// true when a preset exists; exact tells the caller whether the stored name
// matches byte for byte.
struct PresetMatch {
  const char *wanted;
  char stored[PresetNameMax + 1];
  bool found;
  bool exact;
};

static bool presetFind(const char *name, PresetMatch &m) {
  m.wanted = name;
  m.found = false;
  m.exact = false;
  m.stored[0] = '\0';
  presetForEach(&m, [](void *ctx, const char *entry) {
    PresetMatch *pm = static_cast<PresetMatch *>(ctx);
    if (presetNamesEqualFold(entry, pm->wanted)) {
      pm->found = true;
      pm->exact = strcmp(entry, pm->wanted) == 0;
      snprintf(pm->stored, sizeof(pm->stored), "%s", entry);
      return pm->exact ? false : true; // an exact hit ends the search
    }
    return true;
  });
  return m.found;
}

bool presetSave(const char *name, const char **errorOut) {
  char path[PresetPathMax];
  if (!presetPath(path, sizeof(path), name)) {
    *errorOut = "invalid preset name";
    return false;
  }
  if (!sdAvailable) {
    *errorOut = "no SD card";
    return false;
  }
  if (!sd.exists(PresetDir) && !sd.mkdir(PresetDir)) {
    *errorOut = "cannot create the presets directory";
    return false;
  }
  // FAT resolves names case-insensitively, so "night mode" would silently
  // overwrite an existing "Night mode". Refuse rather than destroy: only an
  // exact-name save is an intentional overwrite.
  PresetMatch m;
  if (presetFind(name, m) && !m.exact) {
    *errorOut = "a preset with this name (differing only in case) already exists";
    return false;
  }

  // Write to a temporary file and swap: a failure part-way through must not
  // destroy the preset being replaced
  char tmp[PresetPathMax + 4];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  if (sd.exists(tmp)) {
    sd.remove(tmp);
  }
  FsFile file = sd.open(tmp, FILE_WRITE);
  if (!file) {
    *errorOut = "cannot open the preset file";
    return false;
  }
  JsonDocument doc;
  configToJson(config, doc);
  redactSecrets(doc); // a preset never carries credentials
  const size_t written = serializeJson(doc, file);
  file.flush();
  const bool sizeOk = written > 0 && file.fileSize() == static_cast<uint64_t>(written);
  file.close();
  if (!sizeOk) {
    sd.remove(tmp); // partial write: the existing preset is untouched
    *errorOut = "failed to write the preset";
    return false;
  }
  if (sd.exists(path)) {
    sd.remove(path);
  }
  if (!sd.rename(tmp, path)) {
    sd.remove(tmp);
    *errorOut = "failed to store the preset";
    return false;
  }
  return true;
}

bool presetLoad(const char *name, const char **errorOut) {
  char path[PresetPathMax];
  if (!presetPath(path, sizeof(path), name)) {
    *errorOut = "invalid preset name";
    return false;
  }
  // Require the exact stored spelling: FAT would resolve a case-variant to
  // a different preset than the caller named, silently actuating hardware
  PresetMatch m;
  if (!presetFind(name, m) || !m.exact) {
    *errorOut = "preset not found";
    return false;
  }
  FsFile file = sd.open(path, FILE_READ);
  if (!file) {
    *errorOut = "cannot open the preset file";
    return false;
  }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    *errorOut = "preset file is not valid JSON";
    return false;
  }
  return configApplyDocument(doc, errorOut);
}

// Shared by preset load and settings import: apply a whole-config document
// the operator did not necessarily author.
bool configApplyDocument(const JsonDocument &doc, const char **errorOut) {
  // Absent sections would fall back to compiled defaults, silently disarming
  // fault protection, the current limit and thermal derating
  if (!configDocComplete(doc)) {
    *errorOut = "file is missing configuration sections (not a full settings export)";
    return false;
  }

  MainConfig previous;
  memcpy(&previous, &config, sizeof(previous));
  MainConfig candidate;
  configFromJson(doc, candidate);
  // Files cannot set secrets; restoreSecrets keeps PIN and same-endpoint
  // MQTT/Influx credentials (see config_serde.h).
  restoreSecrets(candidate, previous);
  if (const char *reason = configApiRejectReason(candidate)) {
    *errorOut = reason;
    return false;
  }
  if (validateConfig(candidate)) {
    writeLog("Loaded configuration contained invalid values; corrected");
  }
  PinValidationResult pinResult;
  if (!validatePinOwnership(candidate, &pinResult)) {
    *errorOut = "configuration contains an invalid or conflicting pin assignment";
    return false;
  }
  if (pwmInterruptRequired()) {
    disablePwmInterrupts();
  }
  memcpy(&config, &candidate, sizeof(config));
  applyPwmConfig(previous);
  // Not while a trip is latched: applyPwmConfig deliberately re-asserts the
  // masking, and the modulation ISR must not drive fault-masked submodules
  if (pwmInterruptRequired() && !vFaultTripped) {
    attachModule2PwmInterruptVectors();
    enablePwmInterrupts();
  }
  configSaveNeeded = true; // persist from loop(), not inside the HTTP handler
  return true;
}

bool presetDelete(const char *name, const char **errorOut) {
  char path[PresetPathMax];
  if (!presetPath(path, sizeof(path), name)) {
    *errorOut = "invalid preset name";
    return false;
  }
  PresetMatch m;
  if (!presetFind(name, m) || !m.exact) {
    *errorOut = "preset not found"; // exact spelling only, as for load
    return false;
  }
  if (!sd.remove(path)) {
    *errorOut = "failed to delete the preset";
    return false;
  }
  return true;
}
