#include "config_json.h"
#include "FS.h"
#include "SdFat.h"
#include <ArduinoJson.h>
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

  JsonObject Config_AsymmetricInduction = doc["Config"]["AsymmetricInduction"];
  config.AsymmetricInduction.IsEnabled = Config_AsymmetricInduction["IsEnabled"] | false;
  config.AsymmetricInduction.PreShiftNanos = Config_AsymmetricInduction["PreShiftNanos"] | 250;
  config.AsymmetricInduction.PostShiftNanos = Config_AsymmetricInduction["PostShiftNanos"] | 500;

  JsonObject Config_Pwm = doc["Config"]["Pwm"];
  config.Pwm.PrintRegs = Config_Pwm["PrintRegs"] | false;
  config.Pwm.SyncPwm = Config_Pwm["SyncPwm"] | false;

  JsonObject Config_Pwm_Tm1_Sm13 = Config_Pwm["Tm1"]["Sm13"];
  config.Pwm.Tm1.Sm13.DeadTime = Config_Pwm_Tm1_Sm13["DeadTime"] | 50;
  config.Pwm.Tm1.Sm13.PwmFrequency = Config_Pwm_Tm1_Sm13["PwmFrequency"] | 1000;
  config.Pwm.Tm1.Sm13.ChannelA.OnPeriodMicroseconds = Config_Pwm_Tm1_Sm13["ChannelA"]["OnPeriodMicroseconds"] | 1000;
  config.Pwm.Tm1.Sm13.ChannelA.DutyCycle = Config_Pwm_Tm1_Sm13["ChannelA"]["DutyCycle"] | 32768;
  config.Pwm.Tm1.Sm13.ChannelA.PhaseShift = Config_Pwm_Tm1_Sm13["ChannelA"]["PhaseShift"] | 0;
  config.Pwm.Tm1.Sm13.ChannelA.Enabled = Config_Pwm_Tm1_Sm13["ChannelA"]["Enabled"] | true;
  config.Pwm.Tm1.Sm13.ChannelB.OnPeriodMicroseconds = Config_Pwm_Tm1_Sm13["ChannelB"]["OnPeriodMicroseconds"] | 1000;
  config.Pwm.Tm1.Sm13.ChannelB.DutyCycle = Config_Pwm_Tm1_Sm13["ChannelB"]["DutyCycle"] | 32768;
  config.Pwm.Tm1.Sm13.ChannelB.PhaseShift = Config_Pwm_Tm1_Sm13["ChannelB"]["PhaseShift"] | 0;
  config.Pwm.Tm1.Sm13.ChannelB.Enabled = Config_Pwm_Tm1_Sm13["ChannelB"]["Enabled"] | true;

  JsonObject Config_Pwm_Tm2 = Config_Pwm["Tm2"];
  config.Pwm.Tm2.UseSpwm = Config_Pwm_Tm2["UseSpwm"] | false;
  config.Pwm.Tm2.SpwmCarrierFrequency = Config_Pwm_Tm2["SpwmCarrierFrequency"] | 20000;
  config.Pwm.Tm2.SpwmModulationFrequency = Config_Pwm_Tm2["SpwmModulationFrequency"] | 50;

  JsonObject Config_Pwm_Tm2_Sm20 = Config_Pwm_Tm2["Sm20"];
  config.Pwm.Tm2.Sm20.DeadTime = Config_Pwm_Tm2_Sm20["DeadTime"] | 50;
  config.Pwm.Tm2.Sm20.PwmFrequency = Config_Pwm_Tm2_Sm20["PwmFrequency"] | 1000;
  config.Pwm.Tm2.Sm20.ChannelA.DutyCycle = Config_Pwm_Tm2_Sm20["ChannelA"]["DutyCycle"] | 32768;
  config.Pwm.Tm2.Sm20.ChannelA.PhaseShift = Config_Pwm_Tm2_Sm20["ChannelA"]["PhaseShift"] | 0;
  config.Pwm.Tm2.Sm20.ChannelB.DutyCycle = Config_Pwm_Tm2_Sm20["ChannelB"]["DutyCycle"] | 32768;
  config.Pwm.Tm2.Sm20.ChannelB.PhaseShift = Config_Pwm_Tm2_Sm20["ChannelB"]["PhaseShift"] | 0;

  JsonObject Config_Pwm_Tm2_Sm21 = Config_Pwm_Tm2["Sm21"];
  config.Pwm.Tm2.Sm21.DeadTime = Config_Pwm_Tm2_Sm21["DeadTime"] | 50;
  config.Pwm.Tm2.Sm21.PwmFrequency = Config_Pwm_Tm2_Sm21["PwmFrequency"] | 1000;
  config.Pwm.Tm2.Sm21.ChannelA.DutyCycle = Config_Pwm_Tm2_Sm21["ChannelA"]["DutyCycle"] | 32768;
  config.Pwm.Tm2.Sm21.ChannelA.PhaseShift = Config_Pwm_Tm2_Sm21["ChannelA"]["PhaseShift"] | 0;

  JsonObject Config_Pwm_Tm2_Sm22 = Config_Pwm_Tm2["Sm22"];
  config.Pwm.Tm2.Sm22.DeadTime = Config_Pwm_Tm2_Sm22["DeadTime"] | 50;
  config.Pwm.Tm2.Sm22.PwmFrequency = Config_Pwm_Tm2_Sm22["PwmFrequency"] | 1000;
  config.Pwm.Tm2.Sm22.ChannelA.DutyCycle = Config_Pwm_Tm2_Sm22["ChannelA"]["DutyCycle"] | 32768;
  config.Pwm.Tm2.Sm22.ChannelA.PhaseShift = Config_Pwm_Tm2_Sm22["ChannelA"]["PhaseShift"] | 0;
  config.Pwm.Tm2.Sm22.ChannelB.DutyCycle = Config_Pwm_Tm2_Sm22["ChannelB"]["DutyCycle"] | 32768;
  config.Pwm.Tm2.Sm22.ChannelB.PhaseShift = Config_Pwm_Tm2_Sm22["ChannelB"]["PhaseShift"] | 0;

  JsonObject Config_Pwm_Tm2_Sm23 = Config_Pwm_Tm2["Sm23"];
  config.Pwm.Tm2.Sm23.DeadTime = Config_Pwm_Tm2_Sm23["DeadTime"] | 50;
  config.Pwm.Tm2.Sm23.PwmFrequency = Config_Pwm_Tm2_Sm23["PwmFrequency"] | 1000;
  config.Pwm.Tm2.Sm23.ChannelA.DutyCycle = Config_Pwm_Tm2_Sm23["ChannelA"]["DutyCycle"] | 32768;
  config.Pwm.Tm2.Sm23.ChannelA.PhaseShift = Config_Pwm_Tm2_Sm23["ChannelA"]["PhaseShift"] | 0;
  config.Pwm.Tm2.Sm23.ChannelB.DutyCycle = Config_Pwm_Tm2_Sm23["ChannelB"]["DutyCycle"] | 32768;
  config.Pwm.Tm2.Sm23.ChannelB.PhaseShift = Config_Pwm_Tm2_Sm23["ChannelB"]["PhaseShift"] | 0;

  JsonObject Config_Pwm_Tm3_Sm31 = Config_Pwm["Tm3"]["Sm31"];
  config.Pwm.Tm3.Sm31.DeadTime = Config_Pwm_Tm3_Sm31["DeadTime"] | 50;
  config.Pwm.Tm3.Sm31.PwmFrequency = Config_Pwm_Tm3_Sm31["PwmFrequency"] | 1000;
  config.Pwm.Tm3.Sm31.ChannelA.DutyCycle = Config_Pwm_Tm3_Sm31["ChannelA"]["DutyCycle"] | 32768;
  config.Pwm.Tm3.Sm31.ChannelA.PhaseShift = Config_Pwm_Tm3_Sm31["ChannelA"]["PhaseShift"] | 0;
  config.Pwm.Tm3.Sm31.ChannelB.DutyCycle = Config_Pwm_Tm3_Sm31["ChannelB"]["DutyCycle"] | 32768;
  config.Pwm.Tm3.Sm31.ChannelB.PhaseShift = Config_Pwm_Tm3_Sm31["ChannelB"]["PhaseShift"] | 0;

  JsonObject Config_Pwm_Tm4_Sm40 = Config_Pwm["Tm4"]["Sm40"];
  config.Pwm.Tm4.Sm40.DeadTime = Config_Pwm_Tm4_Sm40["DeadTime"] | 50;
  config.Pwm.Tm4.Sm40.PwmFrequency = Config_Pwm_Tm4_Sm40["PwmFrequency"] | 1000;
  config.Pwm.Tm4.Sm40.ChannelA.DutyCycle = Config_Pwm_Tm4_Sm40["ChannelA"]["DutyCycle"] | 32768;
  config.Pwm.Tm4.Sm40.ChannelA.PhaseShift = Config_Pwm_Tm4_Sm40["ChannelA"]["PhaseShift"] | 0;

  JsonObject Config_Pwm_Tm4_Sm41 = Config_Pwm["Tm4"]["Sm41"];
  config.Pwm.Tm4.Sm41.DeadTime = Config_Pwm_Tm4_Sm41["DeadTime"] | 50;
  config.Pwm.Tm4.Sm41.PwmFrequency = Config_Pwm_Tm4_Sm41["PwmFrequency"] | 1000;
  config.Pwm.Tm4.Sm41.ChannelA.DutyCycle = Config_Pwm_Tm4_Sm41["ChannelA"]["DutyCycle"] | 32768;
  config.Pwm.Tm4.Sm41.ChannelA.PhaseShift = Config_Pwm_Tm4_Sm41["ChannelA"]["PhaseShift"] | 0;

  JsonObject Config_Pwm_Tm4_Sm42 = Config_Pwm["Tm4"]["Sm42"];
  config.Pwm.Tm4.Sm42.DeadTime = Config_Pwm_Tm4_Sm42["DeadTime"] | 50;
  config.Pwm.Tm4.Sm42.PwmFrequency = Config_Pwm_Tm4_Sm42["PwmFrequency"] | 1000;
  config.Pwm.Tm4.Sm42.ChannelA.DutyCycle = Config_Pwm_Tm4_Sm42["ChannelA"]["DutyCycle"] | 32768;
  config.Pwm.Tm4.Sm42.ChannelA.PhaseShift = Config_Pwm_Tm4_Sm42["ChannelA"]["PhaseShift"] | 0;
  config.Pwm.Tm4.Sm42.ChannelB.DutyCycle = Config_Pwm_Tm4_Sm42["ChannelB"]["DutyCycle"] | 32768;
  config.Pwm.Tm4.Sm42.ChannelB.PhaseShift = Config_Pwm_Tm4_Sm42["ChannelB"]["PhaseShift"] | 0;

  if (config.Pwm.Tm1.Sm13.PwmFrequency < 1 || config.Pwm.Tm1.Sm13.PwmFrequency > 1000000) {
    config.Pwm.Tm1.Sm13.PwmFrequency = 1000;
    Serial.println(F("Invalid PWM freq in config; using default"));
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

  JsonObject Config_AsymmetricInduction = doc["Config"]["AsymmetricInduction"].to<JsonObject>();
  Config_AsymmetricInduction["IsEnabled"] = config.AsymmetricInduction.IsEnabled;
  Config_AsymmetricInduction["PreShiftNanos"] = config.AsymmetricInduction.PreShiftNanos;
  Config_AsymmetricInduction["PostShiftNanos"] = config.AsymmetricInduction.PostShiftNanos;

  JsonObject Config_Pwm = doc["Config"]["Pwm"].to<JsonObject>();
  Config_Pwm["PrintRegs"] = config.Pwm.PrintRegs;
  Config_Pwm["SyncPwm"] = config.Pwm.SyncPwm;

  JsonObject Config_Pwm_Tm1_Sm13 = Config_Pwm["Tm1"]["Sm13"].to<JsonObject>();
  Config_Pwm_Tm1_Sm13["DeadTime"] = config.Pwm.Tm1.Sm13.DeadTime;
  Config_Pwm_Tm1_Sm13["PwmFrequency"] = config.Pwm.Tm1.Sm13.PwmFrequency;

  JsonObject Config_Pwm_Tm1_Sm13_ChannelA = Config_Pwm_Tm1_Sm13["ChannelA"].to<JsonObject>();
  Config_Pwm_Tm1_Sm13_ChannelA["OnPeriodMicroseconds"] = config.Pwm.Tm1.Sm13.ChannelA.OnPeriodMicroseconds;
  Config_Pwm_Tm1_Sm13_ChannelA["DutyCycle"] = config.Pwm.Tm1.Sm13.ChannelA.DutyCycle;
  Config_Pwm_Tm1_Sm13_ChannelA["PhaseShift"] = config.Pwm.Tm1.Sm13.ChannelA.PhaseShift;
  Config_Pwm_Tm1_Sm13_ChannelA["Enabled"] = config.Pwm.Tm1.Sm13.ChannelA.Enabled;

  JsonObject Config_Pwm_Tm1_Sm13_ChannelB = Config_Pwm_Tm1_Sm13["ChannelB"].to<JsonObject>();
  Config_Pwm_Tm1_Sm13_ChannelB["OnPeriodMicroseconds"] = config.Pwm.Tm1.Sm13.ChannelB.OnPeriodMicroseconds;
  Config_Pwm_Tm1_Sm13_ChannelB["DutyCycle"] = config.Pwm.Tm1.Sm13.ChannelB.DutyCycle;
  Config_Pwm_Tm1_Sm13_ChannelB["PhaseShift"] = config.Pwm.Tm1.Sm13.ChannelB.PhaseShift;
  Config_Pwm_Tm1_Sm13_ChannelB["Enabled"] = config.Pwm.Tm1.Sm13.ChannelB.Enabled;

  JsonObject Config_Pwm_Tm2 = Config_Pwm["Tm2"].to<JsonObject>();
  Config_Pwm_Tm2["UseSpwm"] = config.Pwm.Tm2.UseSpwm;
  Config_Pwm_Tm2["SpwmCarrierFrequency"] = config.Pwm.Tm2.SpwmCarrierFrequency;
  Config_Pwm_Tm2["SpwmModulationFrequency"] = config.Pwm.Tm2.SpwmModulationFrequency;

  JsonObject Config_Pwm_Tm2_Sm20 = Config_Pwm_Tm2["Sm20"].to<JsonObject>();
  Config_Pwm_Tm2_Sm20["DeadTime"] = config.Pwm.Tm2.Sm20.DeadTime;
  Config_Pwm_Tm2_Sm20["PwmFrequency"] = config.Pwm.Tm2.Sm20.PwmFrequency;

  JsonObject Config_Pwm_Tm2_Sm20_ChannelA = Config_Pwm_Tm2_Sm20["ChannelA"].to<JsonObject>();
  Config_Pwm_Tm2_Sm20_ChannelA["DutyCycle"] = config.Pwm.Tm2.Sm20.ChannelA.DutyCycle;
  Config_Pwm_Tm2_Sm20_ChannelA["PhaseShift"] = config.Pwm.Tm2.Sm20.ChannelA.PhaseShift;

  JsonObject Config_Pwm_Tm2_Sm20_ChannelB = Config_Pwm_Tm2_Sm20["ChannelB"].to<JsonObject>();
  Config_Pwm_Tm2_Sm20_ChannelB["DutyCycle"] = config.Pwm.Tm2.Sm20.ChannelB.DutyCycle;
  Config_Pwm_Tm2_Sm20_ChannelB["PhaseShift"] = config.Pwm.Tm2.Sm20.ChannelB.PhaseShift;

  JsonObject Config_Pwm_Tm2_Sm21 = Config_Pwm_Tm2["Sm21"].to<JsonObject>();
  Config_Pwm_Tm2_Sm21["DeadTime"] = config.Pwm.Tm2.Sm21.DeadTime;
  Config_Pwm_Tm2_Sm21["PwmFrequency"] = config.Pwm.Tm2.Sm21.PwmFrequency;

  JsonObject Config_Pwm_Tm2_Sm21_ChannelA = Config_Pwm_Tm2_Sm21["ChannelA"].to<JsonObject>();
  Config_Pwm_Tm2_Sm21_ChannelA["DutyCycle"] = config.Pwm.Tm2.Sm21.ChannelA.DutyCycle;
  Config_Pwm_Tm2_Sm21_ChannelA["PhaseShift"] = config.Pwm.Tm2.Sm21.ChannelA.PhaseShift;

  JsonObject Config_Pwm_Tm2_Sm22 = Config_Pwm_Tm2["Sm22"].to<JsonObject>();
  Config_Pwm_Tm2_Sm22["DeadTime"] = config.Pwm.Tm2.Sm22.DeadTime;
  Config_Pwm_Tm2_Sm22["PwmFrequency"] = config.Pwm.Tm2.Sm22.PwmFrequency;

  JsonObject Config_Pwm_Tm2_Sm22_ChannelA = Config_Pwm_Tm2_Sm22["ChannelA"].to<JsonObject>();
  Config_Pwm_Tm2_Sm22_ChannelA["DutyCycle"] = config.Pwm.Tm2.Sm22.ChannelA.DutyCycle;
  Config_Pwm_Tm2_Sm22_ChannelA["PhaseShift"] = config.Pwm.Tm2.Sm22.ChannelA.PhaseShift;

  JsonObject Config_Pwm_Tm2_Sm22_ChannelB = Config_Pwm_Tm2_Sm22["ChannelB"].to<JsonObject>();
  Config_Pwm_Tm2_Sm22_ChannelB["DutyCycle"] = config.Pwm.Tm2.Sm22.ChannelB.DutyCycle;
  Config_Pwm_Tm2_Sm22_ChannelB["PhaseShift"] = config.Pwm.Tm2.Sm22.ChannelB.PhaseShift;

  JsonObject Config_Pwm_Tm2_Sm23 = Config_Pwm_Tm2["Sm23"].to<JsonObject>();
  Config_Pwm_Tm2_Sm23["DeadTime"] = config.Pwm.Tm2.Sm23.DeadTime;
  Config_Pwm_Tm2_Sm23["PwmFrequency"] = config.Pwm.Tm2.Sm23.PwmFrequency;

  JsonObject Config_Pwm_Tm2_Sm23_ChannelA = Config_Pwm_Tm2_Sm23["ChannelA"].to<JsonObject>();
  Config_Pwm_Tm2_Sm23_ChannelA["DutyCycle"] = config.Pwm.Tm2.Sm23.ChannelA.DutyCycle;
  Config_Pwm_Tm2_Sm23_ChannelA["PhaseShift"] = config.Pwm.Tm2.Sm23.ChannelA.PhaseShift;

  JsonObject Config_Pwm_Tm2_Sm23_ChannelB = Config_Pwm_Tm2_Sm23["ChannelB"].to<JsonObject>();
  Config_Pwm_Tm2_Sm23_ChannelB["DutyCycle"] = config.Pwm.Tm2.Sm23.ChannelB.DutyCycle;
  Config_Pwm_Tm2_Sm23_ChannelB["PhaseShift"] = config.Pwm.Tm2.Sm23.ChannelB.PhaseShift;

  JsonObject Config_Pwm_Tm3_Sm31 = Config_Pwm["Tm3"]["Sm31"].to<JsonObject>();
  Config_Pwm_Tm3_Sm31["DeadTime"] = config.Pwm.Tm3.Sm31.DeadTime;
  Config_Pwm_Tm3_Sm31["PwmFrequency"] = config.Pwm.Tm3.Sm31.PwmFrequency;

  JsonObject Config_Pwm_Tm3_Sm31_ChannelA = Config_Pwm_Tm3_Sm31["ChannelA"].to<JsonObject>();
  Config_Pwm_Tm3_Sm31_ChannelA["DutyCycle"] = config.Pwm.Tm3.Sm31.ChannelA.DutyCycle;
  Config_Pwm_Tm3_Sm31_ChannelA["PhaseShift"] = config.Pwm.Tm3.Sm31.ChannelA.PhaseShift;

  JsonObject Config_Pwm_Tm3_Sm31_ChannelB = Config_Pwm_Tm3_Sm31["ChannelB"].to<JsonObject>();
  Config_Pwm_Tm3_Sm31_ChannelB["DutyCycle"] = config.Pwm.Tm3.Sm31.ChannelB.DutyCycle;
  Config_Pwm_Tm3_Sm31_ChannelB["PhaseShift"] = config.Pwm.Tm3.Sm31.ChannelB.PhaseShift;

  JsonObject Config_Pwm_Tm4 = Config_Pwm["Tm4"].to<JsonObject>();

  JsonObject Config_Pwm_Tm4_Sm40 = Config_Pwm_Tm4["Sm40"].to<JsonObject>();
  Config_Pwm_Tm4_Sm40["DeadTime"] = config.Pwm.Tm4.Sm40.DeadTime;
  Config_Pwm_Tm4_Sm40["PwmFrequency"] = config.Pwm.Tm4.Sm40.PwmFrequency;

  JsonObject Config_Pwm_Tm4_Sm40_ChannelA = Config_Pwm_Tm4_Sm40["ChannelA"].to<JsonObject>();
  Config_Pwm_Tm4_Sm40_ChannelA["DutyCycle"] = config.Pwm.Tm4.Sm40.ChannelA.DutyCycle;
  Config_Pwm_Tm4_Sm40_ChannelA["PhaseShift"] = config.Pwm.Tm4.Sm40.ChannelA.PhaseShift;

  JsonObject Config_Pwm_Tm4_Sm41 = Config_Pwm_Tm4["Sm41"].to<JsonObject>();
  Config_Pwm_Tm4_Sm41["DeadTime"] = config.Pwm.Tm4.Sm41.DeadTime;
  Config_Pwm_Tm4_Sm41["PwmFrequency"] = config.Pwm.Tm4.Sm41.PwmFrequency;

  JsonObject Config_Pwm_Tm4_Sm41_ChannelA = Config_Pwm_Tm4_Sm41["ChannelA"].to<JsonObject>();
  Config_Pwm_Tm4_Sm41_ChannelA["DutyCycle"] = config.Pwm.Tm4.Sm41.ChannelA.DutyCycle;
  Config_Pwm_Tm4_Sm41_ChannelA["PhaseShift"] = config.Pwm.Tm4.Sm41.ChannelA.PhaseShift;

  JsonObject Config_Pwm_Tm4_Sm42 = Config_Pwm_Tm4["Sm42"].to<JsonObject>();
  Config_Pwm_Tm4_Sm42["DeadTime"] = config.Pwm.Tm4.Sm42.DeadTime;
  Config_Pwm_Tm4_Sm42["PwmFrequency"] = config.Pwm.Tm4.Sm42.PwmFrequency;

  JsonObject Config_Pwm_Tm4_Sm42_ChannelA = Config_Pwm_Tm4_Sm42["ChannelA"].to<JsonObject>();
  Config_Pwm_Tm4_Sm42_ChannelA["DutyCycle"] = config.Pwm.Tm4.Sm42.ChannelA.DutyCycle;
  Config_Pwm_Tm4_Sm42_ChannelA["PhaseShift"] = config.Pwm.Tm4.Sm42.ChannelA.PhaseShift;

  JsonObject Config_Pwm_Tm4_Sm42_ChannelB = Config_Pwm_Tm4_Sm42["ChannelB"].to<JsonObject>();
  Config_Pwm_Tm4_Sm42_ChannelB["DutyCycle"] = config.Pwm.Tm4.Sm42.ChannelB.DutyCycle;
  Config_Pwm_Tm4_Sm42_ChannelB["PhaseShift"] = config.Pwm.Tm4.Sm42.ChannelB.PhaseShift;

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