#ifndef CONFIG_SERDE_H
#define CONFIG_SERDE_H

// MainConfig <-> JSON mapping: no filesystem or hardware dependencies
// (ArduinoJson is platform-independent), unit-tested natively. The file I/O
// wrappers live in config_json.cpp.

#include <stdio.h>
#include <ArduinoJson.h>
#include "config_json.h"

// Bounded, always-terminated string copy (portable across firmware and the
// native test host, unlike strlcpy)
inline void copyConfigString(char *dst, unsigned int dstSize, const char *src) {
  snprintf(dst, dstSize, "%s", src != nullptr ? src : "");
}

inline void configFromJson(const JsonDocument &doc, MainConfig &config) {
  JsonObjectConst Config_AsymmetricInduction = doc["Config"]["AsymmetricInduction"];
  config.AsymmetricInduction.IsEnabled = Config_AsymmetricInduction["IsEnabled"] | false;
  config.AsymmetricInduction.PreShiftNanos = Config_AsymmetricInduction["PreShiftNanos"] | 250;
  config.AsymmetricInduction.PostShiftNanos = Config_AsymmetricInduction["PostShiftNanos"] | 500;

  JsonObjectConst Config_Pwm = doc["Config"]["Pwm"];
  config.Pwm.PrintRegs = Config_Pwm["PrintRegs"] | false;
  config.Pwm.SyncPwm = Config_Pwm["SyncPwm"] | false;

  JsonObjectConst Config_Pwm_Tm1_Sm13 = Config_Pwm["Tm1"]["Sm13"];
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

  JsonObjectConst Config_Pwm_Tm2 = Config_Pwm["Tm2"];
  config.Pwm.Tm2.UseSpwm = Config_Pwm_Tm2["UseSpwm"] | false;
  config.Pwm.Tm2.SpwmCarrierFrequency = Config_Pwm_Tm2["SpwmCarrierFrequency"] | 20000;
  config.Pwm.Tm2.SpwmModulationFrequency = Config_Pwm_Tm2["SpwmModulationFrequency"] | 50;
  config.Pwm.Tm2.ModulationScheme = Config_Pwm_Tm2["ModulationScheme"] | 1;
  config.Pwm.Tm2.ModulationIndexMilli = Config_Pwm_Tm2["ModulationIndexMilli"] | 1000;
  config.Pwm.Tm2.ModulationCells = Config_Pwm_Tm2["ModulationCells"] | 2;
  config.Pwm.Tm2.CarrierDisposition = Config_Pwm_Tm2["CarrierDisposition"] | 0;
  config.Pwm.Tm2.DeadTimeCompensation = Config_Pwm_Tm2["DeadTimeCompensation"] | false;
  config.Pwm.Tm2.SoftStartMs = Config_Pwm_Tm2["SoftStartMs"] | 0;
  config.Pwm.Tm2.ReferenceWaveform = Config_Pwm_Tm2["ReferenceWaveform"] | 0;
  config.Pwm.Tm2.DpwmVariant = Config_Pwm_Tm2["DpwmVariant"] | 0;
  config.Pwm.Tm2.DpwmClampAngleDeg = Config_Pwm_Tm2["DpwmClampAngleDeg"] | 0;
  config.Pwm.Tm2.CarrierDitherMode = Config_Pwm_Tm2["CarrierDitherMode"] | 0;
  config.Pwm.Tm2.CarrierDitherPercent = Config_Pwm_Tm2["CarrierDitherPercent"] | 0;
  config.Pwm.Tm2.NearestLevelModulation = Config_Pwm_Tm2["NearestLevelModulation"] | false;

  JsonObjectConst Config_Pwm_Tm2_Sm20 = Config_Pwm_Tm2["Sm20"];
  config.Pwm.Tm2.Sm20.DeadTime = Config_Pwm_Tm2_Sm20["DeadTime"] | 50;
  config.Pwm.Tm2.Sm20.PwmFrequency = Config_Pwm_Tm2_Sm20["PwmFrequency"] | 1000;
  config.Pwm.Tm2.Sm20.ChannelA.DutyCycle = Config_Pwm_Tm2_Sm20["ChannelA"]["DutyCycle"] | 32768;
  config.Pwm.Tm2.Sm20.ChannelA.PhaseShift = Config_Pwm_Tm2_Sm20["ChannelA"]["PhaseShift"] | 0;
  config.Pwm.Tm2.Sm20.ChannelB.DutyCycle = Config_Pwm_Tm2_Sm20["ChannelB"]["DutyCycle"] | 32768;
  config.Pwm.Tm2.Sm20.ChannelB.PhaseShift = Config_Pwm_Tm2_Sm20["ChannelB"]["PhaseShift"] | 0;

  JsonObjectConst Config_Pwm_Tm2_Sm21 = Config_Pwm_Tm2["Sm21"];
  config.Pwm.Tm2.Sm21.DeadTime = Config_Pwm_Tm2_Sm21["DeadTime"] | 50;
  config.Pwm.Tm2.Sm21.PwmFrequency = Config_Pwm_Tm2_Sm21["PwmFrequency"] | 1000;
  config.Pwm.Tm2.Sm21.ChannelA.DutyCycle = Config_Pwm_Tm2_Sm21["ChannelA"]["DutyCycle"] | 32768;
  config.Pwm.Tm2.Sm21.ChannelA.PhaseShift = Config_Pwm_Tm2_Sm21["ChannelA"]["PhaseShift"] | 0;

  JsonObjectConst Config_Pwm_Tm2_Sm22 = Config_Pwm_Tm2["Sm22"];
  config.Pwm.Tm2.Sm22.DeadTime = Config_Pwm_Tm2_Sm22["DeadTime"] | 50;
  config.Pwm.Tm2.Sm22.PwmFrequency = Config_Pwm_Tm2_Sm22["PwmFrequency"] | 1000;
  config.Pwm.Tm2.Sm22.ChannelA.DutyCycle = Config_Pwm_Tm2_Sm22["ChannelA"]["DutyCycle"] | 32768;
  config.Pwm.Tm2.Sm22.ChannelA.PhaseShift = Config_Pwm_Tm2_Sm22["ChannelA"]["PhaseShift"] | 0;
  config.Pwm.Tm2.Sm22.ChannelB.DutyCycle = Config_Pwm_Tm2_Sm22["ChannelB"]["DutyCycle"] | 32768;
  config.Pwm.Tm2.Sm22.ChannelB.PhaseShift = Config_Pwm_Tm2_Sm22["ChannelB"]["PhaseShift"] | 0;

  JsonObjectConst Config_Pwm_Tm2_Sm23 = Config_Pwm_Tm2["Sm23"];
  config.Pwm.Tm2.Sm23.DeadTime = Config_Pwm_Tm2_Sm23["DeadTime"] | 50;
  config.Pwm.Tm2.Sm23.PwmFrequency = Config_Pwm_Tm2_Sm23["PwmFrequency"] | 1000;
  config.Pwm.Tm2.Sm23.ChannelA.DutyCycle = Config_Pwm_Tm2_Sm23["ChannelA"]["DutyCycle"] | 32768;
  config.Pwm.Tm2.Sm23.ChannelA.PhaseShift = Config_Pwm_Tm2_Sm23["ChannelA"]["PhaseShift"] | 0;
  config.Pwm.Tm2.Sm23.ChannelB.DutyCycle = Config_Pwm_Tm2_Sm23["ChannelB"]["DutyCycle"] | 32768;
  config.Pwm.Tm2.Sm23.ChannelB.PhaseShift = Config_Pwm_Tm2_Sm23["ChannelB"]["PhaseShift"] | 0;

  JsonObjectConst Config_Pwm_Tm3_Sm31 = Config_Pwm["Tm3"]["Sm31"];
  config.Pwm.Tm3.Sm31.DeadTime = Config_Pwm_Tm3_Sm31["DeadTime"] | 50;
  config.Pwm.Tm3.Sm31.PwmFrequency = Config_Pwm_Tm3_Sm31["PwmFrequency"] | 1000;
  config.Pwm.Tm3.Sm31.ChannelA.DutyCycle = Config_Pwm_Tm3_Sm31["ChannelA"]["DutyCycle"] | 32768;
  config.Pwm.Tm3.Sm31.ChannelA.PhaseShift = Config_Pwm_Tm3_Sm31["ChannelA"]["PhaseShift"] | 0;
  config.Pwm.Tm3.Sm31.ChannelB.DutyCycle = Config_Pwm_Tm3_Sm31["ChannelB"]["DutyCycle"] | 32768;
  config.Pwm.Tm3.Sm31.ChannelB.PhaseShift = Config_Pwm_Tm3_Sm31["ChannelB"]["PhaseShift"] | 0;

  JsonObjectConst Config_Pwm_Tm4_Sm40 = Config_Pwm["Tm4"]["Sm40"];
  config.Pwm.Tm4.Sm40.DeadTime = Config_Pwm_Tm4_Sm40["DeadTime"] | 50;
  config.Pwm.Tm4.Sm40.PwmFrequency = Config_Pwm_Tm4_Sm40["PwmFrequency"] | 1000;
  config.Pwm.Tm4.Sm40.ChannelA.DutyCycle = Config_Pwm_Tm4_Sm40["ChannelA"]["DutyCycle"] | 32768;
  config.Pwm.Tm4.Sm40.ChannelA.PhaseShift = Config_Pwm_Tm4_Sm40["ChannelA"]["PhaseShift"] | 0;

  JsonObjectConst Config_Pwm_Tm4_Sm41 = Config_Pwm["Tm4"]["Sm41"];
  config.Pwm.Tm4.Sm41.DeadTime = Config_Pwm_Tm4_Sm41["DeadTime"] | 50;
  config.Pwm.Tm4.Sm41.PwmFrequency = Config_Pwm_Tm4_Sm41["PwmFrequency"] | 1000;
  config.Pwm.Tm4.Sm41.ChannelA.DutyCycle = Config_Pwm_Tm4_Sm41["ChannelA"]["DutyCycle"] | 32768;
  config.Pwm.Tm4.Sm41.ChannelA.PhaseShift = Config_Pwm_Tm4_Sm41["ChannelA"]["PhaseShift"] | 0;

  JsonObjectConst Config_Pwm_Tm4_Sm42 = Config_Pwm["Tm4"]["Sm42"];
  config.Pwm.Tm4.Sm42.DeadTime = Config_Pwm_Tm4_Sm42["DeadTime"] | 50;
  config.Pwm.Tm4.Sm42.PwmFrequency = Config_Pwm_Tm4_Sm42["PwmFrequency"] | 1000;
  config.Pwm.Tm4.Sm42.ChannelA.DutyCycle = Config_Pwm_Tm4_Sm42["ChannelA"]["DutyCycle"] | 32768;
  config.Pwm.Tm4.Sm42.ChannelA.PhaseShift = Config_Pwm_Tm4_Sm42["ChannelA"]["PhaseShift"] | 0;
  config.Pwm.Tm4.Sm42.ChannelB.DutyCycle = Config_Pwm_Tm4_Sm42["ChannelB"]["DutyCycle"] | 32768;
  config.Pwm.Tm4.Sm42.ChannelB.PhaseShift = Config_Pwm_Tm4_Sm42["ChannelB"]["PhaseShift"] | 0;

  JsonObjectConst Config_Feedback = doc["Config"]["Feedback"];
  config.Feedback.Enabled = Config_Feedback["Enabled"] | false;
  config.Feedback.AnalogPin = Config_Feedback["AnalogPin"] | 41;
  config.Feedback.SetpointMillivolts = Config_Feedback["SetpointMillivolts"] | 0;
  config.Feedback.FullScaleMillivolts = Config_Feedback["FullScaleMillivolts"] | 3300;
  config.Feedback.KpMilli = Config_Feedback["KpMilli"] | 200;
  config.Feedback.KiMilli = Config_Feedback["KiMilli"] | 2000;
  config.Feedback.LoopHz = Config_Feedback["LoopHz"] | 1000;

  JsonObjectConst Config_FaultProtection = doc["Config"]["FaultProtection"];
  config.FaultProtection.Enabled = Config_FaultProtection["Enabled"] | false;
  config.FaultProtection.Pin = Config_FaultProtection["Pin"] | 32;
  config.FaultProtection.ActiveHigh = Config_FaultProtection["ActiveHigh"] | true;

  JsonObjectConst Config_Influx = doc["Config"]["Influx"];
  copyConfigString(config.Influx.Host, sizeof(config.Influx.Host), Config_Influx["Host"] | "ub-1.lan");
  config.Influx.Port = Config_Influx["Port"] | 8086;
  copyConfigString(config.Influx.Org, sizeof(config.Influx.Org), Config_Influx["Org"] | "501eaf58ac3171cd");
  copyConfigString(config.Influx.Bucket, sizeof(config.Influx.Bucket), Config_Influx["Bucket"] | "power_generator");
  copyConfigString(config.Influx.Token, sizeof(config.Influx.Token), Config_Influx["Token"] | "");
}

// Clamps out-of-range values; returns true if anything was corrected.
inline bool validateConfig(MainConfig &config) {
  bool corrected = false;
  if (config.Pwm.Tm1.Sm13.PwmFrequency < 1 || config.Pwm.Tm1.Sm13.PwmFrequency > 1000000) {
    config.Pwm.Tm1.Sm13.PwmFrequency = 1000;
    corrected = true;
  }
  return corrected;
}

inline void configToJson(const MainConfig &config, JsonDocument &doc) {
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
  Config_Pwm_Tm2["ModulationScheme"] = config.Pwm.Tm2.ModulationScheme;
  Config_Pwm_Tm2["ModulationIndexMilli"] = config.Pwm.Tm2.ModulationIndexMilli;
  Config_Pwm_Tm2["ModulationCells"] = config.Pwm.Tm2.ModulationCells;
  Config_Pwm_Tm2["CarrierDisposition"] = config.Pwm.Tm2.CarrierDisposition;
  Config_Pwm_Tm2["DeadTimeCompensation"] = config.Pwm.Tm2.DeadTimeCompensation;
  Config_Pwm_Tm2["SoftStartMs"] = config.Pwm.Tm2.SoftStartMs;
  Config_Pwm_Tm2["ReferenceWaveform"] = config.Pwm.Tm2.ReferenceWaveform;
  Config_Pwm_Tm2["DpwmVariant"] = config.Pwm.Tm2.DpwmVariant;
  Config_Pwm_Tm2["DpwmClampAngleDeg"] = config.Pwm.Tm2.DpwmClampAngleDeg;
  Config_Pwm_Tm2["CarrierDitherMode"] = config.Pwm.Tm2.CarrierDitherMode;
  Config_Pwm_Tm2["CarrierDitherPercent"] = config.Pwm.Tm2.CarrierDitherPercent;
  Config_Pwm_Tm2["NearestLevelModulation"] = config.Pwm.Tm2.NearestLevelModulation;

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

  JsonObject Config_Feedback = doc["Config"]["Feedback"].to<JsonObject>();
  Config_Feedback["Enabled"] = config.Feedback.Enabled;
  Config_Feedback["AnalogPin"] = config.Feedback.AnalogPin;
  Config_Feedback["SetpointMillivolts"] = config.Feedback.SetpointMillivolts;
  Config_Feedback["FullScaleMillivolts"] = config.Feedback.FullScaleMillivolts;
  Config_Feedback["KpMilli"] = config.Feedback.KpMilli;
  Config_Feedback["KiMilli"] = config.Feedback.KiMilli;
  Config_Feedback["LoopHz"] = config.Feedback.LoopHz;

  JsonObject Config_FaultProtection = doc["Config"]["FaultProtection"].to<JsonObject>();
  Config_FaultProtection["Enabled"] = config.FaultProtection.Enabled;
  Config_FaultProtection["Pin"] = config.FaultProtection.Pin;
  Config_FaultProtection["ActiveHigh"] = config.FaultProtection.ActiveHigh;

  JsonObject Config_Influx = doc["Config"]["Influx"].to<JsonObject>();
  Config_Influx["Host"] = config.Influx.Host;
  Config_Influx["Port"] = config.Influx.Port;
  Config_Influx["Org"] = config.Influx.Org;
  Config_Influx["Bucket"] = config.Influx.Bucket;
  Config_Influx["Token"] = config.Influx.Token;
}

#endif
