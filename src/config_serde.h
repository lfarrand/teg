#ifndef CONFIG_SERDE_H
#define CONFIG_SERDE_H

// MainConfig <-> JSON mapping: no filesystem or hardware dependencies
// (ArduinoJson is platform-independent), unit-tested natively. The file I/O
// wrappers live in config_json.cpp.

#include <stdio.h>
#include <ArduinoJson.h>
#include "config_json.h"
#include "acmp_math.h" // pin routing check in validateConfig
#include "modulation.h" // scheme/dither enums for the PLL exclusion rules

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
  config.Pwm.Verbose = Config_Pwm["Verbose"] | false;

  JsonObjectConst Config_Pwm_Tm1_Sm13 = Config_Pwm["Tm1"]["Sm13"];
  config.Pwm.Tm1.Sm13.Pair = Config_Pwm_Tm1_Sm13["Pair"] | PairIndependent;
  config.Pwm.Tm1.Sm13.DeadTime = Config_Pwm_Tm1_Sm13["DeadTime"] | MinHalfBridgeDeadTimeNs;
  config.Pwm.Tm1.Sm13.PwmFrequency = Config_Pwm_Tm1_Sm13["PwmFrequency"] | 1000;
  config.Pwm.Tm1.Sm13.ChannelA.DutyCycle = Config_Pwm_Tm1_Sm13["ChannelA"]["DutyCycle"] | 0;
  config.Pwm.Tm1.Sm13.ChannelB.DutyCycle = Config_Pwm_Tm1_Sm13["ChannelB"]["DutyCycle"] | 0;

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
  config.Pwm.Tm2.WaveformSampleStep = Config_Pwm_Tm2["WaveformSampleStep"] | false;

  JsonObjectConst Config_Pwm_Tm2_Sm20 = Config_Pwm_Tm2["Sm20"];
  config.Pwm.Tm2.Sm20.Pair = Config_Pwm_Tm2_Sm20["Pair"] | PairIndependent;
  config.Pwm.Tm2.Sm20.DeadTime = Config_Pwm_Tm2_Sm20["DeadTime"] | MinHalfBridgeDeadTimeNs;
  config.Pwm.Tm2.Sm20.PwmFrequency = Config_Pwm_Tm2_Sm20["PwmFrequency"] | 1000;
  config.Pwm.Tm2.Sm20.ChannelA.DutyCycle = Config_Pwm_Tm2_Sm20["ChannelA"]["DutyCycle"] | 0;
  config.Pwm.Tm2.Sm20.ChannelB.DutyCycle = Config_Pwm_Tm2_Sm20["ChannelB"]["DutyCycle"] | 0;

  JsonObjectConst Config_Pwm_Tm2_Sm21 = Config_Pwm_Tm2["Sm21"];
  config.Pwm.Tm2.Sm21.Pair = Config_Pwm_Tm2_Sm21["Pair"] | PairIndependent;
  config.Pwm.Tm2.Sm21.DeadTime = Config_Pwm_Tm2_Sm21["DeadTime"] | MinHalfBridgeDeadTimeNs;
  config.Pwm.Tm2.Sm21.PwmFrequency = Config_Pwm_Tm2_Sm21["PwmFrequency"] | 1000;
  config.Pwm.Tm2.Sm21.ChannelA.DutyCycle = Config_Pwm_Tm2_Sm21["ChannelA"]["DutyCycle"] | 0;

  JsonObjectConst Config_Pwm_Tm2_Sm22 = Config_Pwm_Tm2["Sm22"];
  config.Pwm.Tm2.Sm22.Pair = Config_Pwm_Tm2_Sm22["Pair"] | PairIndependent;
  config.Pwm.Tm2.Sm22.DeadTime = Config_Pwm_Tm2_Sm22["DeadTime"] | MinHalfBridgeDeadTimeNs;
  config.Pwm.Tm2.Sm22.PwmFrequency = Config_Pwm_Tm2_Sm22["PwmFrequency"] | 1000;
  config.Pwm.Tm2.Sm22.ChannelA.DutyCycle = Config_Pwm_Tm2_Sm22["ChannelA"]["DutyCycle"] | 0;
  config.Pwm.Tm2.Sm22.ChannelB.DutyCycle = Config_Pwm_Tm2_Sm22["ChannelB"]["DutyCycle"] | 0;

  JsonObjectConst Config_Pwm_Tm2_Sm23 = Config_Pwm_Tm2["Sm23"];
  config.Pwm.Tm2.Sm23.Pair = Config_Pwm_Tm2_Sm23["Pair"] | PairIndependent;
  config.Pwm.Tm2.Sm23.DeadTime = Config_Pwm_Tm2_Sm23["DeadTime"] | MinHalfBridgeDeadTimeNs;
  config.Pwm.Tm2.Sm23.PwmFrequency = Config_Pwm_Tm2_Sm23["PwmFrequency"] | 1000;
  config.Pwm.Tm2.Sm23.ChannelA.DutyCycle = Config_Pwm_Tm2_Sm23["ChannelA"]["DutyCycle"] | 0;
  config.Pwm.Tm2.Sm23.ChannelB.DutyCycle = Config_Pwm_Tm2_Sm23["ChannelB"]["DutyCycle"] | 0;

  JsonObjectConst Config_Pwm_Tm3_Sm31 = Config_Pwm["Tm3"]["Sm31"];
  config.Pwm.Tm3.Sm31.Pair = Config_Pwm_Tm3_Sm31["Pair"] | PairIndependent;
  config.Pwm.Tm3.Sm31.DeadTime = Config_Pwm_Tm3_Sm31["DeadTime"] | MinHalfBridgeDeadTimeNs;
  config.Pwm.Tm3.Sm31.PwmFrequency = Config_Pwm_Tm3_Sm31["PwmFrequency"] | 1000;
  config.Pwm.Tm3.Sm31.ChannelA.DutyCycle = Config_Pwm_Tm3_Sm31["ChannelA"]["DutyCycle"] | 0;
  config.Pwm.Tm3.Sm31.ChannelB.DutyCycle = Config_Pwm_Tm3_Sm31["ChannelB"]["DutyCycle"] | 0;

  JsonObjectConst Config_Pwm_Tm4_Sm40 = Config_Pwm["Tm4"]["Sm40"];
  config.Pwm.Tm4.Sm40.Pair = Config_Pwm_Tm4_Sm40["Pair"] | PairIndependent;
  config.Pwm.Tm4.Sm40.DeadTime = Config_Pwm_Tm4_Sm40["DeadTime"] | MinHalfBridgeDeadTimeNs;
  config.Pwm.Tm4.Sm40.PwmFrequency = Config_Pwm_Tm4_Sm40["PwmFrequency"] | 1000;
  config.Pwm.Tm4.Sm40.ChannelA.DutyCycle = Config_Pwm_Tm4_Sm40["ChannelA"]["DutyCycle"] | 0;

  JsonObjectConst Config_Pwm_Tm4_Sm41 = Config_Pwm["Tm4"]["Sm41"];
  config.Pwm.Tm4.Sm41.Pair = Config_Pwm_Tm4_Sm41["Pair"] | PairIndependent;
  config.Pwm.Tm4.Sm41.DeadTime = Config_Pwm_Tm4_Sm41["DeadTime"] | MinHalfBridgeDeadTimeNs;
  config.Pwm.Tm4.Sm41.PwmFrequency = Config_Pwm_Tm4_Sm41["PwmFrequency"] | 1000;
  config.Pwm.Tm4.Sm41.ChannelA.DutyCycle = Config_Pwm_Tm4_Sm41["ChannelA"]["DutyCycle"] | 0;

  JsonObjectConst Config_Pwm_Tm4_Sm42 = Config_Pwm["Tm4"]["Sm42"];
  config.Pwm.Tm4.Sm42.Pair = Config_Pwm_Tm4_Sm42["Pair"] | PairIndependent;
  config.Pwm.Tm4.Sm42.DeadTime = Config_Pwm_Tm4_Sm42["DeadTime"] | MinHalfBridgeDeadTimeNs;
  config.Pwm.Tm4.Sm42.PwmFrequency = Config_Pwm_Tm4_Sm42["PwmFrequency"] | 1000;
  config.Pwm.Tm4.Sm42.ChannelA.DutyCycle = Config_Pwm_Tm4_Sm42["ChannelA"]["DutyCycle"] | 0;
  config.Pwm.Tm4.Sm42.ChannelB.DutyCycle = Config_Pwm_Tm4_Sm42["ChannelB"]["DutyCycle"] | 0;

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

  JsonObjectConst Config_CurrentLimit = doc["Config"]["CurrentLimit"];
  config.CurrentLimit.Enabled = Config_CurrentLimit["Enabled"] | false;
  config.CurrentLimit.Pin = Config_CurrentLimit["Pin"] | 40;
  config.CurrentLimit.ThresholdMillivolts = Config_CurrentLimit["ThresholdMillivolts"] | 2475;
  config.CurrentLimit.CycleByCycle = Config_CurrentLimit["CycleByCycle"] | false;
  config.CurrentLimit.FilterCount = Config_CurrentLimit["FilterCount"] | 0;
  config.CurrentLimit.FilterPeriod = Config_CurrentLimit["FilterPeriod"] | 0;

  JsonObjectConst Config_Influx = doc["Config"]["Influx"];
  copyConfigString(config.Influx.Host, sizeof(config.Influx.Host), Config_Influx["Host"] | "ub-1.lan");
  config.Influx.Port = Config_Influx["Port"] | 8086;
  copyConfigString(config.Influx.Org, sizeof(config.Influx.Org), Config_Influx["Org"] | "501eaf58ac3171cd");
  copyConfigString(config.Influx.Bucket, sizeof(config.Influx.Bucket), Config_Influx["Bucket"] | "power_generator");
  copyConfigString(config.Influx.Token, sizeof(config.Influx.Token), Config_Influx["Token"] | "");
  config.Influx.IntervalSeconds = Config_Influx["IntervalSeconds"] | 10;

  JsonObjectConst Config_Mqtt = doc["Config"]["Mqtt"];
  config.Mqtt.Enabled = Config_Mqtt["Enabled"] | false;
  copyConfigString(config.Mqtt.Host, sizeof(config.Mqtt.Host), Config_Mqtt["Host"] | "");
  config.Mqtt.Port = Config_Mqtt["Port"] | 1883;
  copyConfigString(config.Mqtt.Username, sizeof(config.Mqtt.Username), Config_Mqtt["Username"] | "");
  copyConfigString(config.Mqtt.Password, sizeof(config.Mqtt.Password), Config_Mqtt["Password"] | "");
  copyConfigString(config.Mqtt.BaseTopic, sizeof(config.Mqtt.BaseTopic), Config_Mqtt["BaseTopic"] | "teg");
  copyConfigString(config.Mqtt.DiscoveryPrefix, sizeof(config.Mqtt.DiscoveryPrefix),
                   Config_Mqtt["DiscoveryPrefix"] | "homeassistant");
  config.Mqtt.DiscoveryEnabled = Config_Mqtt["DiscoveryEnabled"] | true;
  config.Mqtt.IntervalSeconds = Config_Mqtt["IntervalSeconds"] | 10;

  config.Mtp.Enabled = doc["Config"]["Mtp"]["Enabled"] | false;

  copyConfigString(config.Security.WritePin, sizeof(config.Security.WritePin),
                   doc["Config"]["Security"]["WritePin"] | "");

  config.Capture.Enabled = doc["Config"]["Capture"]["Enabled"] | false;

  JsonObjectConst Config_Meter = doc["Config"]["Meter"];
  config.Meter.Enabled = Config_Meter["Enabled"] | false;
  config.Meter.CurrentPin = Config_Meter["CurrentPin"] | 40;
  config.Meter.VoltageZeroMillivolts = Config_Meter["VoltageZeroMillivolts"] | 1650;
  config.Meter.CurrentZeroMillivolts = Config_Meter["CurrentZeroMillivolts"] | 1650;
  config.Meter.CurrentMilliampPerVolt = Config_Meter["CurrentMilliampPerVolt"] | 10000;
  config.Meter.VoltageRatioMilli = Config_Meter["VoltageRatioMilli"] | 1000;

  JsonObjectConst Config_Pll = doc["Config"]["Pll"];
  config.Pll.Enabled = Config_Pll["Enabled"] | false;
  config.Pll.PhaseOffsetCentiDeg = Config_Pll["PhaseOffsetCentiDeg"] | 0;
  config.Pll.MinHz = Config_Pll["MinHz"] | 45;
  config.Pll.MaxHz = Config_Pll["MaxHz"] | 55;
  config.Pll.BandwidthDeciHz = Config_Pll["BandwidthDeciHz"] | 200;
  config.Pll.ZeroMillivolts = Config_Pll["ZeroMillivolts"] | 1650;
  config.Pll.MinLevelMillivolts = Config_Pll["MinLevelMillivolts"] | 100;

  JsonObjectConst Config_Mppt = doc["Config"]["Mppt"];
  config.Mppt.Enabled = Config_Mppt["Enabled"] | false;
  config.Mppt.IntervalMs = Config_Mppt["IntervalMs"] | 3000;
  config.Mppt.StepMilli = Config_Mppt["StepMilli"] | 20;
  config.Mppt.MinStepMilli = Config_Mppt["MinStepMilli"] | 5;
  config.Mppt.MinIndexMilli = Config_Mppt["MinIndexMilli"] | 50;
  config.Mppt.MaxIndexMilli = Config_Mppt["MaxIndexMilli"] | 1000;
  config.Mppt.DeadbandMw = Config_Mppt["DeadbandMw"] | 10;
  config.Mppt.RestartDeltaMw = Config_Mppt["RestartDeltaMw"] | 1000;

  JsonObjectConst Config_Thermal = doc["Config"]["Thermal"];
  config.Thermal.Enabled = Config_Thermal["Enabled"] | false;
  config.Thermal.OneWirePin = Config_Thermal["OneWirePin"] | 21;
  config.Thermal.DerateStartC = Config_Thermal["DerateStartC"] | 70;
  config.Thermal.DerateEndC = Config_Thermal["DerateEndC"] | 90;
}

// Blank out secrets before a config document leaves the device (GET /api/config)
inline void redactSecrets(JsonDocument &doc) {
  doc["Config"]["Influx"]["Token"] = "";
  doc["Config"]["Security"]["WritePin"] = "";
  doc["Config"]["Mqtt"]["Password"] = "";
}

// An empty secret in an incoming document means "keep the current value" —
// the UI round-trips the redacted (empty) fields, and that must not wipe the
// stored credentials. To actually clear a secret, edit /settings.cfg.
inline void preserveSecrets(MainConfig &config, const MainConfig &previous) {
  if (config.Influx.Token[0] == '\0') {
    copyConfigString(config.Influx.Token, sizeof(config.Influx.Token), previous.Influx.Token);
  }
  if (config.Security.WritePin[0] == '\0') {
    copyConfigString(config.Security.WritePin, sizeof(config.Security.WritePin), previous.Security.WritePin);
  }
  if (config.Mqtt.Password[0] == '\0') {
    copyConfigString(config.Mqtt.Password, sizeof(config.Mqtt.Password), previous.Mqtt.Password);
  }
}

// UNCONDITIONAL secret restore, for documents the operator did not author
// (presets, imported files). preserveSecrets only fills in EMPTY fields, so
// a file carrying a real credential would replace the live one - locking
// the operator out via a changed write PIN, or pairing a stolen broker host
// with the device's genuine password. Config applied from such a file must
// never be able to set a secret; only an explicit UI edit can.
inline void restoreSecrets(MainConfig &config, const MainConfig &previous) {
  copyConfigString(config.Influx.Token, sizeof(config.Influx.Token), previous.Influx.Token);
  copyConfigString(config.Security.WritePin, sizeof(config.Security.WritePin),
                   previous.Security.WritePin);
  copyConfigString(config.Mqtt.Password, sizeof(config.Mqtt.Password), previous.Mqtt.Password);
}

// A whole-config document must carry every safety-relevant section: absent
// keys fall back to compiled defaults, so a truncated or older-firmware
// file would silently disarm fault protection, the hardware current limit
// and thermal derating rather than leaving them as configured.
inline bool configDocComplete(const JsonDocument &doc) {
  JsonObjectConst c = doc["Config"];
  if (c.isNull()) {
    return false;
  }
  return !c["Pwm"].isNull() && !c["FaultProtection"].isNull() &&
         !c["CurrentLimit"].isNull() && !c["Thermal"].isNull();
}

// Clamps out-of-range values; returns true if anything was corrected.
inline bool validateConfig(MainConfig &config) {
  bool corrected = false;

  // Pair modes: reduce anything the submodule cannot physically run back to
  // Independent. Sm21/Sm40/Sm41 have no channel-B pin on a Teensy 4.1, and pairing
  // one would run DTCNT1 at its 0x07FF reset value (~13.6us) as the falling-edge
  // dead time because the core never writes that register. Sm42 has a B pin but
  // drives it from independent start/stop values. See pwm_pair.h.
  const auto sanitisePair = [&corrected](uint8_t &mode, uint8_t submodule) {
    const uint8_t safe = pairModeSanitised(mode, submodule);
    if (safe != mode) {
      mode = safe;
      corrected = true;
    }
  };
  sanitisePair(config.Pwm.Tm1.Sm13.Pair, PairSm13);
  sanitisePair(config.Pwm.Tm3.Sm31.Pair, PairSm31);

  // Per-submodule PwmFrequency was never range-checked either, and one of these clocks
  // the SPWM ISR. Zero reaches computeAsymmetricTimings() as a divisor; absurdly high
  // values produce a period of a handful of counter ticks and no usable duty
  // resolution. 1 Hz to 1 MHz brackets everything the hardware can actually do.
  const auto clampFreq = [&corrected](uint32_t &hz) {
    if (hz < 1 || hz > 1000000) {
      hz = 1000;
      corrected = true;
    }
  };
  clampFreq(config.Pwm.Tm2.Sm20.PwmFrequency);
  clampFreq(config.Pwm.Tm2.Sm21.PwmFrequency);
  clampFreq(config.Pwm.Tm2.Sm22.PwmFrequency);
  clampFreq(config.Pwm.Tm2.Sm23.PwmFrequency);
  clampFreq(config.Pwm.Tm3.Sm31.PwmFrequency);
  clampFreq(config.Pwm.Tm4.Sm40.PwmFrequency);
  clampFreq(config.Pwm.Tm4.Sm41.PwmFrequency);
  clampFreq(config.Pwm.Tm4.Sm42.PwmFrequency);
  // Tm1.Sm13 is clamped by the existing check further down.

  // ModulationCells was never range-checked: a hand-edited settings file or a POST
  // could set 0 or 200, and configureModule2 then silently clamps to a different value
  // than anything else reasons about. Clamp it once, here, so every consumer agrees.
  if (config.Pwm.Tm2.ModulationCells < 1 || config.Pwm.Tm2.ModulationCells > MaxModulationCells) {
    config.Pwm.Tm2.ModulationCells =
        config.Pwm.Tm2.ModulationCells < 1 ? 1 : MaxModulationCells;
    corrected = true;
  }

  // Only the permanent hardware facts are corrected here - no channel-B pin, and
  // Sm42's independent start/stop timing. Those never change, so persisting the
  // correction is right.
  sanitisePair(config.Pwm.Tm2.Sm20.Pair, PairSm20);
  sanitisePair(config.Pwm.Tm2.Sm21.Pair, PairSm21);
  sanitisePair(config.Pwm.Tm2.Sm22.Pair, PairSm22);
  sanitisePair(config.Pwm.Tm2.Sm23.Pair, PairSm23);

  // The SCHEME gate is deliberately NOT applied here. It depends on the modulation
  // scheme, which changes, so rewriting Pair would silently and permanently discard
  // the operator's intent: they wired a half-bridge, selected an inverting scheme
  // once, and the leg is un-paired for good with nothing in the config left to say it
  // was ever meant to be a leg. configureModule2() applies the gate at the point of
  // use instead, so setupSubmodule() can tell "not a pair" from "a pair we are
  // refusing to drive" - and hold that leg's outputs off rather than fall back to
  // independent channels carrying their static duties.
  sanitisePair(config.Pwm.Tm4.Sm40.Pair, PairSm40);
  sanitisePair(config.Pwm.Tm4.Sm41.Pair, PairSm41);
  sanitisePair(config.Pwm.Tm4.Sm42.Pair, PairSm42);

  // Dead time is clamped to what DTCNT can represent, for every submodule and every
  // mode. A value above ~13.6us would otherwise wrap - programming a SHORTER gap
  // than asked for, which is the worst direction for this particular field.
  const auto clampDeadTime = [&corrected](uint16_t &deadTime) {
    if (deadTime > pairMaxDeadTimeNs()) {
      deadTime = static_cast<uint16_t>(pairMaxDeadTimeNs());
      corrected = true;
    }
  };
  clampDeadTime(config.Pwm.Tm1.Sm13.DeadTime);
  clampDeadTime(config.Pwm.Tm2.Sm20.DeadTime);
  clampDeadTime(config.Pwm.Tm2.Sm21.DeadTime);
  clampDeadTime(config.Pwm.Tm2.Sm22.DeadTime);
  clampDeadTime(config.Pwm.Tm2.Sm23.DeadTime);
  clampDeadTime(config.Pwm.Tm3.Sm31.DeadTime);
  clampDeadTime(config.Pwm.Tm4.Sm40.DeadTime);
  clampDeadTime(config.Pwm.Tm4.Sm41.DeadTime);
  clampDeadTime(config.Pwm.Tm4.Sm42.DeadTime);
  if (config.Pwm.Tm1.Sm13.PwmFrequency < 1 || config.Pwm.Tm1.Sm13.PwmFrequency > 1000000) {
    config.Pwm.Tm1.Sm13.PwmFrequency = 1000;
    corrected = true;
  }
  if (acmpRouteForPin(config.CurrentLimit.Pin).cmp == 0) {
    config.CurrentLimit.Pin = 40;
    corrected = true;
  }
  if (config.CurrentLimit.ThresholdMillivolts < 100 || config.CurrentLimit.ThresholdMillivolts > 3300) {
    config.CurrentLimit.ThresholdMillivolts = 2475;
    corrected = true;
  }
  if (config.CurrentLimit.FilterCount > 7) {
    config.CurrentLimit.FilterCount = 0;
    corrected = true;
  }
  if (config.Pll.MinHz < 1 || config.Pll.MaxHz > 400 || config.Pll.MinHz >= config.Pll.MaxHz) {
    config.Pll.MinHz = 45;
    config.Pll.MaxHz = 55;
    corrected = true;
  }
  if (config.Pll.PhaseOffsetCentiDeg < -18000 || config.Pll.PhaseOffsetCentiDeg > 18000) {
    config.Pll.PhaseOffsetCentiDeg = 0;
    corrected = true;
  }
  if (config.Pll.BandwidthDeciHz < 10 || config.Pll.BandwidthDeciHz > 500) {
    config.Pll.BandwidthDeciHz = 200;
    corrected = true;
  }
  if (config.Pll.ZeroMillivolts > 3300) {
    config.Pll.ZeroMillivolts = 1650;
    corrected = true;
  }
  if (config.Pll.MinLevelMillivolts < 20 || config.Pll.MinLevelMillivolts > 3300) {
    // A zero floor disables both the signal-presence gate and the
    // normalization denominator: ADC noise would sweep the output across
    // the whole clamp window instead of coasting
    config.Pll.MinLevelMillivolts = 100;
    corrected = true;
  }
  // PLL exclusions, PLL-disabling rules first so a disabled PLL never
  // corrects unrelated features. Feedback: regulates the same pin as a DC
  // level, physically contradictory with an AC reference on it. Stepped
  // playback: the DDS phase does not drive the output. Nominal outside the
  // clamp window: unlock/coast would snap the output to a rail. Carrier
  // below 18x nominal: the discrete SOGI step g leaves its tested envelope.
  if (config.Pll.Enabled && config.Feedback.Enabled) {
    config.Pll.Enabled = false;
    corrected = true;
  }
  if (config.Pll.Enabled &&
      (config.Pwm.Tm2.ReferenceWaveform == RefWaveSequence ||
       (config.Pwm.Tm2.ReferenceWaveform == RefWaveCustom && config.Pwm.Tm2.WaveformSampleStep))) {
    config.Pll.Enabled = false;
    corrected = true;
  }
  if (config.Pll.Enabled && (config.Pwm.Tm2.SpwmModulationFrequency < config.Pll.MinHz ||
                             config.Pwm.Tm2.SpwmModulationFrequency > config.Pll.MaxHz)) {
    config.Pll.Enabled = false;
    corrected = true;
  }
  if (config.Pll.Enabled &&
      config.Pwm.Tm2.SpwmCarrierFrequency < 18U * config.Pwm.Tm2.SpwmModulationFrequency) {
    config.Pll.Enabled = false;
    corrected = true;
  }
  // Dither: the ISR takes per-cycle increments from tables built at the
  // CONFIGURED frequency - PLL steering would be silently disconnected
  if (config.Pll.Enabled && config.Pwm.Tm2.CarrierDitherMode != DitherOff) {
    config.Pwm.Tm2.CarrierDitherMode = DitherOff;
    corrected = true;
  }
  // MPPT clamps and exclusions. The meter reading at an evaluation can be
  // ~1s old and covers the previous second, so a settled measurement needs
  // two meter periods past the perturbation, plus the soft-start ramp.
  {
    const uint32_t floorMs = 2100U + config.Pwm.Tm2.SoftStartMs;
    if (config.Mppt.IntervalMs < floorMs) {
      config.Mppt.IntervalMs = floorMs > 60000U ? 60000U : static_cast<uint16_t>(floorMs);
      corrected = true;
    }
  }
  if (config.Mppt.RestartDeltaMw < 100 ||
      config.Mppt.DeadbandMw >= config.Mppt.RestartDeltaMw) {
    // A tiny restart threshold fires on every delta (step never adapts); a
    // deadband above it makes the noise rejection unreachable dead code
    config.Mppt.DeadbandMw = 10;
    config.Mppt.RestartDeltaMw = 1000;
    corrected = true;
  }
  if (config.Mppt.MinIndexMilli >= config.Mppt.MaxIndexMilli ||
      config.Mppt.MaxIndexMilli > MaxModulationIndexMilli) {
    config.Mppt.MinIndexMilli = 50;
    config.Mppt.MaxIndexMilli = 1000;
    corrected = true;
  }
  if (config.Mppt.MinStepMilli < 1 || config.Mppt.MinStepMilli > config.Mppt.StepMilli ||
      config.Mppt.StepMilli > 200) {
    config.Mppt.StepMilli = 20;
    config.Mppt.MinStepMilli = 5;
    corrected = true;
  }
  // Feedback and MPPT drive the same actuator (the index target): the older
  // feature wins, matching the PLL-vs-Feedback precedent
  if (config.Mppt.Enabled && config.Feedback.Enabled) {
    config.Mppt.Enabled = false;
    corrected = true;
  }
  if (config.Mqtt.Port == 0) {
    config.Mqtt.Port = 1883;
    corrected = true;
  }
  if (config.Mqtt.IntervalSeconds == 0 || config.Mqtt.IntervalSeconds > 3600) {
    // 0 would connect, discover and report online while every sensor sits
    // at 'unknown' forever
    config.Mqtt.IntervalSeconds = 10;
    corrected = true;
  }
  if (config.Mqtt.BaseTopic[0] == '\0') {
    copyConfigString(config.Mqtt.BaseTopic, sizeof(config.Mqtt.BaseTopic), "teg");
    corrected = true;
  }
  if (config.Mqtt.DiscoveryPrefix[0] == '\0') {
    copyConfigString(config.Mqtt.DiscoveryPrefix, sizeof(config.Mqtt.DiscoveryPrefix),
                     "homeassistant");
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
  Config_Pwm["Verbose"] = config.Pwm.Verbose;

  JsonObject Config_Pwm_Tm1_Sm13 = Config_Pwm["Tm1"]["Sm13"].to<JsonObject>();
  Config_Pwm_Tm1_Sm13["Pair"] = config.Pwm.Tm1.Sm13.Pair;
  Config_Pwm_Tm1_Sm13["DeadTime"] = config.Pwm.Tm1.Sm13.DeadTime;
  Config_Pwm_Tm1_Sm13["PwmFrequency"] = config.Pwm.Tm1.Sm13.PwmFrequency;

  JsonObject Config_Pwm_Tm1_Sm13_ChannelA = Config_Pwm_Tm1_Sm13["ChannelA"].to<JsonObject>();
  Config_Pwm_Tm1_Sm13_ChannelA["DutyCycle"] = config.Pwm.Tm1.Sm13.ChannelA.DutyCycle;

  JsonObject Config_Pwm_Tm1_Sm13_ChannelB = Config_Pwm_Tm1_Sm13["ChannelB"].to<JsonObject>();
  Config_Pwm_Tm1_Sm13_ChannelB["DutyCycle"] = config.Pwm.Tm1.Sm13.ChannelB.DutyCycle;

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
  Config_Pwm_Tm2["WaveformSampleStep"] = config.Pwm.Tm2.WaveformSampleStep;

  JsonObject Config_Pwm_Tm2_Sm20 = Config_Pwm_Tm2["Sm20"].to<JsonObject>();
  Config_Pwm_Tm2_Sm20["Pair"] = config.Pwm.Tm2.Sm20.Pair;
  Config_Pwm_Tm2_Sm20["DeadTime"] = config.Pwm.Tm2.Sm20.DeadTime;
  Config_Pwm_Tm2_Sm20["PwmFrequency"] = config.Pwm.Tm2.Sm20.PwmFrequency;

  JsonObject Config_Pwm_Tm2_Sm20_ChannelA = Config_Pwm_Tm2_Sm20["ChannelA"].to<JsonObject>();
  Config_Pwm_Tm2_Sm20_ChannelA["DutyCycle"] = config.Pwm.Tm2.Sm20.ChannelA.DutyCycle;

  JsonObject Config_Pwm_Tm2_Sm20_ChannelB = Config_Pwm_Tm2_Sm20["ChannelB"].to<JsonObject>();
  Config_Pwm_Tm2_Sm20_ChannelB["DutyCycle"] = config.Pwm.Tm2.Sm20.ChannelB.DutyCycle;

  JsonObject Config_Pwm_Tm2_Sm21 = Config_Pwm_Tm2["Sm21"].to<JsonObject>();
  Config_Pwm_Tm2_Sm21["Pair"] = config.Pwm.Tm2.Sm21.Pair;
  Config_Pwm_Tm2_Sm21["DeadTime"] = config.Pwm.Tm2.Sm21.DeadTime;
  Config_Pwm_Tm2_Sm21["PwmFrequency"] = config.Pwm.Tm2.Sm21.PwmFrequency;

  JsonObject Config_Pwm_Tm2_Sm21_ChannelA = Config_Pwm_Tm2_Sm21["ChannelA"].to<JsonObject>();
  Config_Pwm_Tm2_Sm21_ChannelA["DutyCycle"] = config.Pwm.Tm2.Sm21.ChannelA.DutyCycle;

  JsonObject Config_Pwm_Tm2_Sm22 = Config_Pwm_Tm2["Sm22"].to<JsonObject>();
  Config_Pwm_Tm2_Sm22["Pair"] = config.Pwm.Tm2.Sm22.Pair;
  Config_Pwm_Tm2_Sm22["DeadTime"] = config.Pwm.Tm2.Sm22.DeadTime;
  Config_Pwm_Tm2_Sm22["PwmFrequency"] = config.Pwm.Tm2.Sm22.PwmFrequency;

  JsonObject Config_Pwm_Tm2_Sm22_ChannelA = Config_Pwm_Tm2_Sm22["ChannelA"].to<JsonObject>();
  Config_Pwm_Tm2_Sm22_ChannelA["DutyCycle"] = config.Pwm.Tm2.Sm22.ChannelA.DutyCycle;

  JsonObject Config_Pwm_Tm2_Sm22_ChannelB = Config_Pwm_Tm2_Sm22["ChannelB"].to<JsonObject>();
  Config_Pwm_Tm2_Sm22_ChannelB["DutyCycle"] = config.Pwm.Tm2.Sm22.ChannelB.DutyCycle;

  JsonObject Config_Pwm_Tm2_Sm23 = Config_Pwm_Tm2["Sm23"].to<JsonObject>();
  Config_Pwm_Tm2_Sm23["Pair"] = config.Pwm.Tm2.Sm23.Pair;
  Config_Pwm_Tm2_Sm23["DeadTime"] = config.Pwm.Tm2.Sm23.DeadTime;
  Config_Pwm_Tm2_Sm23["PwmFrequency"] = config.Pwm.Tm2.Sm23.PwmFrequency;

  JsonObject Config_Pwm_Tm2_Sm23_ChannelA = Config_Pwm_Tm2_Sm23["ChannelA"].to<JsonObject>();
  Config_Pwm_Tm2_Sm23_ChannelA["DutyCycle"] = config.Pwm.Tm2.Sm23.ChannelA.DutyCycle;

  JsonObject Config_Pwm_Tm2_Sm23_ChannelB = Config_Pwm_Tm2_Sm23["ChannelB"].to<JsonObject>();
  Config_Pwm_Tm2_Sm23_ChannelB["DutyCycle"] = config.Pwm.Tm2.Sm23.ChannelB.DutyCycle;

  JsonObject Config_Pwm_Tm3_Sm31 = Config_Pwm["Tm3"]["Sm31"].to<JsonObject>();
  Config_Pwm_Tm3_Sm31["Pair"] = config.Pwm.Tm3.Sm31.Pair;
  Config_Pwm_Tm3_Sm31["DeadTime"] = config.Pwm.Tm3.Sm31.DeadTime;
  Config_Pwm_Tm3_Sm31["PwmFrequency"] = config.Pwm.Tm3.Sm31.PwmFrequency;

  JsonObject Config_Pwm_Tm3_Sm31_ChannelA = Config_Pwm_Tm3_Sm31["ChannelA"].to<JsonObject>();
  Config_Pwm_Tm3_Sm31_ChannelA["DutyCycle"] = config.Pwm.Tm3.Sm31.ChannelA.DutyCycle;

  JsonObject Config_Pwm_Tm3_Sm31_ChannelB = Config_Pwm_Tm3_Sm31["ChannelB"].to<JsonObject>();
  Config_Pwm_Tm3_Sm31_ChannelB["DutyCycle"] = config.Pwm.Tm3.Sm31.ChannelB.DutyCycle;

  JsonObject Config_Pwm_Tm4 = Config_Pwm["Tm4"].to<JsonObject>();

  JsonObject Config_Pwm_Tm4_Sm40 = Config_Pwm_Tm4["Sm40"].to<JsonObject>();
  Config_Pwm_Tm4_Sm40["Pair"] = config.Pwm.Tm4.Sm40.Pair;
  Config_Pwm_Tm4_Sm40["DeadTime"] = config.Pwm.Tm4.Sm40.DeadTime;
  Config_Pwm_Tm4_Sm40["PwmFrequency"] = config.Pwm.Tm4.Sm40.PwmFrequency;

  JsonObject Config_Pwm_Tm4_Sm40_ChannelA = Config_Pwm_Tm4_Sm40["ChannelA"].to<JsonObject>();
  Config_Pwm_Tm4_Sm40_ChannelA["DutyCycle"] = config.Pwm.Tm4.Sm40.ChannelA.DutyCycle;

  JsonObject Config_Pwm_Tm4_Sm41 = Config_Pwm_Tm4["Sm41"].to<JsonObject>();
  Config_Pwm_Tm4_Sm41["Pair"] = config.Pwm.Tm4.Sm41.Pair;
  Config_Pwm_Tm4_Sm41["DeadTime"] = config.Pwm.Tm4.Sm41.DeadTime;
  Config_Pwm_Tm4_Sm41["PwmFrequency"] = config.Pwm.Tm4.Sm41.PwmFrequency;

  JsonObject Config_Pwm_Tm4_Sm41_ChannelA = Config_Pwm_Tm4_Sm41["ChannelA"].to<JsonObject>();
  Config_Pwm_Tm4_Sm41_ChannelA["DutyCycle"] = config.Pwm.Tm4.Sm41.ChannelA.DutyCycle;

  JsonObject Config_Pwm_Tm4_Sm42 = Config_Pwm_Tm4["Sm42"].to<JsonObject>();
  Config_Pwm_Tm4_Sm42["Pair"] = config.Pwm.Tm4.Sm42.Pair;
  Config_Pwm_Tm4_Sm42["DeadTime"] = config.Pwm.Tm4.Sm42.DeadTime;
  Config_Pwm_Tm4_Sm42["PwmFrequency"] = config.Pwm.Tm4.Sm42.PwmFrequency;

  JsonObject Config_Pwm_Tm4_Sm42_ChannelA = Config_Pwm_Tm4_Sm42["ChannelA"].to<JsonObject>();
  Config_Pwm_Tm4_Sm42_ChannelA["DutyCycle"] = config.Pwm.Tm4.Sm42.ChannelA.DutyCycle;

  JsonObject Config_Pwm_Tm4_Sm42_ChannelB = Config_Pwm_Tm4_Sm42["ChannelB"].to<JsonObject>();
  Config_Pwm_Tm4_Sm42_ChannelB["DutyCycle"] = config.Pwm.Tm4.Sm42.ChannelB.DutyCycle;

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

  JsonObject Config_CurrentLimit = doc["Config"]["CurrentLimit"].to<JsonObject>();
  Config_CurrentLimit["Enabled"] = config.CurrentLimit.Enabled;
  Config_CurrentLimit["Pin"] = config.CurrentLimit.Pin;
  Config_CurrentLimit["ThresholdMillivolts"] = config.CurrentLimit.ThresholdMillivolts;
  Config_CurrentLimit["CycleByCycle"] = config.CurrentLimit.CycleByCycle;
  Config_CurrentLimit["FilterCount"] = config.CurrentLimit.FilterCount;
  Config_CurrentLimit["FilterPeriod"] = config.CurrentLimit.FilterPeriod;

  JsonObject Config_Influx = doc["Config"]["Influx"].to<JsonObject>();
  Config_Influx["Host"] = config.Influx.Host;
  Config_Influx["Port"] = config.Influx.Port;
  Config_Influx["Org"] = config.Influx.Org;
  Config_Influx["Bucket"] = config.Influx.Bucket;
  Config_Influx["Token"] = config.Influx.Token;
  Config_Influx["IntervalSeconds"] = config.Influx.IntervalSeconds;

  JsonObject Config_Mqtt = doc["Config"]["Mqtt"].to<JsonObject>();
  Config_Mqtt["Enabled"] = config.Mqtt.Enabled;
  Config_Mqtt["Host"] = config.Mqtt.Host;
  Config_Mqtt["Port"] = config.Mqtt.Port;
  Config_Mqtt["Username"] = config.Mqtt.Username;
  Config_Mqtt["Password"] = config.Mqtt.Password;
  Config_Mqtt["BaseTopic"] = config.Mqtt.BaseTopic;
  Config_Mqtt["DiscoveryPrefix"] = config.Mqtt.DiscoveryPrefix;
  Config_Mqtt["DiscoveryEnabled"] = config.Mqtt.DiscoveryEnabled;
  Config_Mqtt["IntervalSeconds"] = config.Mqtt.IntervalSeconds;

  doc["Config"]["Mtp"].to<JsonObject>()["Enabled"] = config.Mtp.Enabled;

  doc["Config"]["Security"].to<JsonObject>()["WritePin"] = config.Security.WritePin;

  doc["Config"]["Capture"].to<JsonObject>()["Enabled"] = config.Capture.Enabled;

  JsonObject Config_Meter = doc["Config"]["Meter"].to<JsonObject>();
  Config_Meter["Enabled"] = config.Meter.Enabled;
  Config_Meter["CurrentPin"] = config.Meter.CurrentPin;
  Config_Meter["VoltageZeroMillivolts"] = config.Meter.VoltageZeroMillivolts;
  Config_Meter["CurrentZeroMillivolts"] = config.Meter.CurrentZeroMillivolts;
  Config_Meter["CurrentMilliampPerVolt"] = config.Meter.CurrentMilliampPerVolt;
  Config_Meter["VoltageRatioMilli"] = config.Meter.VoltageRatioMilli;

  JsonObject Config_Pll = doc["Config"]["Pll"].to<JsonObject>();
  Config_Pll["Enabled"] = config.Pll.Enabled;
  Config_Pll["PhaseOffsetCentiDeg"] = config.Pll.PhaseOffsetCentiDeg;
  Config_Pll["MinHz"] = config.Pll.MinHz;
  Config_Pll["MaxHz"] = config.Pll.MaxHz;
  Config_Pll["BandwidthDeciHz"] = config.Pll.BandwidthDeciHz;
  Config_Pll["ZeroMillivolts"] = config.Pll.ZeroMillivolts;
  Config_Pll["MinLevelMillivolts"] = config.Pll.MinLevelMillivolts;

  JsonObject Config_Mppt = doc["Config"]["Mppt"].to<JsonObject>();
  Config_Mppt["Enabled"] = config.Mppt.Enabled;
  Config_Mppt["IntervalMs"] = config.Mppt.IntervalMs;
  Config_Mppt["StepMilli"] = config.Mppt.StepMilli;
  Config_Mppt["MinStepMilli"] = config.Mppt.MinStepMilli;
  Config_Mppt["MinIndexMilli"] = config.Mppt.MinIndexMilli;
  Config_Mppt["MaxIndexMilli"] = config.Mppt.MaxIndexMilli;
  Config_Mppt["DeadbandMw"] = config.Mppt.DeadbandMw;
  Config_Mppt["RestartDeltaMw"] = config.Mppt.RestartDeltaMw;

  JsonObject Config_Thermal = doc["Config"]["Thermal"].to<JsonObject>();
  Config_Thermal["Enabled"] = config.Thermal.Enabled;
  Config_Thermal["OneWirePin"] = config.Thermal.OneWirePin;
  Config_Thermal["DerateStartC"] = config.Thermal.DerateStartC;
  Config_Thermal["DerateEndC"] = config.Thermal.DerateEndC;
}

#endif
