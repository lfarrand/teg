#ifndef CONFIG_COMPARE_H
#define CONFIG_COMPARE_H

// Semantic comparisons for configuration change gates. Comparing these
// structs byte-for-byte is not valid: their padding bytes are indeterminate
// and can differ even when every configuration value is identical. When a
// compared config struct gains a member, add it to the matching overload.

#include <stddef.h>
#include <string.h>

#include "config_json.h"

template <size_t N>
inline bool configStringsEqual(const char (&left)[N], const char (&right)[N]) {
  return strncmp(left, right, N) == 0;
}

inline bool configValuesEqual(const ChannelConfig &left, const ChannelConfig &right) {
  return left.DutyCycle == right.DutyCycle;
}

inline bool configValuesEqual(const SubmoduleConfig &left, const SubmoduleConfig &right) {
  return left.Pair == right.Pair && left.DeadTime == right.DeadTime &&
         left.PwmFrequency == right.PwmFrequency &&
         configValuesEqual(left.ChannelA, right.ChannelA) &&
         configValuesEqual(left.ChannelB, right.ChannelB);
}

inline bool configValuesEqual(const Module1Config &left, const Module1Config &right) {
  return configValuesEqual(left.Sm13, right.Sm13);
}

inline bool configValuesEqual(const Module2Config &left, const Module2Config &right) {
  return left.UseSpwm == right.UseSpwm &&
         left.SpwmCarrierFrequency == right.SpwmCarrierFrequency &&
         left.SpwmModulationFrequency == right.SpwmModulationFrequency &&
         left.ModulationScheme == right.ModulationScheme &&
         left.ModulationIndexMilli == right.ModulationIndexMilli &&
         left.ModulationCells == right.ModulationCells &&
         left.CarrierDisposition == right.CarrierDisposition &&
         left.DeadTimeCompensation == right.DeadTimeCompensation &&
         left.SoftStartMs == right.SoftStartMs &&
         left.ReferenceWaveform == right.ReferenceWaveform &&
         left.DpwmVariant == right.DpwmVariant &&
         left.DpwmClampAngleDeg == right.DpwmClampAngleDeg &&
         left.CarrierDitherMode == right.CarrierDitherMode &&
         left.CarrierDitherPercent == right.CarrierDitherPercent &&
         left.NearestLevelModulation == right.NearestLevelModulation &&
         left.WaveformSampleStep == right.WaveformSampleStep &&
         configValuesEqual(left.Sm20, right.Sm20) &&
         configValuesEqual(left.Sm21, right.Sm21) &&
         configValuesEqual(left.Sm22, right.Sm22) &&
         configValuesEqual(left.Sm23, right.Sm23);
}

inline bool configValuesEqual(const Module3Config &left, const Module3Config &right) {
  return configValuesEqual(left.Sm31, right.Sm31);
}

inline bool configValuesEqual(const Module4Config &left, const Module4Config &right) {
  return configValuesEqual(left.Sm40, right.Sm40) &&
         configValuesEqual(left.Sm41, right.Sm41) &&
         configValuesEqual(left.Sm42, right.Sm42);
}

inline bool configValuesEqual(const PwmConfig &left, const PwmConfig &right) {
  return configValuesEqual(left.Tm1, right.Tm1) &&
         configValuesEqual(left.Tm2, right.Tm2) &&
         configValuesEqual(left.Tm3, right.Tm3) &&
         configValuesEqual(left.Tm4, right.Tm4) && left.PrintRegs == right.PrintRegs &&
         left.SyncPwm == right.SyncPwm && left.Verbose == right.Verbose;
}

inline bool configValuesEqual(const AsymmetricInductionConfig &left,
                              const AsymmetricInductionConfig &right) {
  return left.IsEnabled == right.IsEnabled && left.PreShiftNanos == right.PreShiftNanos &&
         left.PostShiftNanos == right.PostShiftNanos;
}

inline bool configValuesEqual(const FeedbackConfig &left, const FeedbackConfig &right) {
  return left.Enabled == right.Enabled && left.AnalogPin == right.AnalogPin &&
         left.SetpointMillivolts == right.SetpointMillivolts &&
         left.FullScaleMillivolts == right.FullScaleMillivolts &&
         left.KpMilli == right.KpMilli && left.KiMilli == right.KiMilli &&
         left.LoopHz == right.LoopHz;
}

inline bool configValuesEqual(const FaultProtectionConfig &left,
                              const FaultProtectionConfig &right) {
  return left.Enabled == right.Enabled && left.Pin == right.Pin &&
         left.ActiveHigh == right.ActiveHigh;
}

inline bool configValuesEqual(const CurrentLimitConfig &left,
                              const CurrentLimitConfig &right) {
  return left.Enabled == right.Enabled && left.Pin == right.Pin &&
         left.ThresholdMillivolts == right.ThresholdMillivolts &&
         left.CycleByCycle == right.CycleByCycle && left.FilterCount == right.FilterCount &&
         left.FilterPeriod == right.FilterPeriod;
}

inline bool configValuesEqual(const MqttConfig &left, const MqttConfig &right) {
  return left.Enabled == right.Enabled && configStringsEqual(left.Host, right.Host) &&
         left.Port == right.Port && configStringsEqual(left.Username, right.Username) &&
         configStringsEqual(left.Password, right.Password) &&
         configStringsEqual(left.BaseTopic, right.BaseTopic) &&
         configStringsEqual(left.DiscoveryPrefix, right.DiscoveryPrefix) &&
         left.DiscoveryEnabled == right.DiscoveryEnabled &&
         left.IntervalSeconds == right.IntervalSeconds;
}

inline bool configValuesEqual(const PllConfig &left, const PllConfig &right) {
  return left.Enabled == right.Enabled &&
         left.PhaseOffsetCentiDeg == right.PhaseOffsetCentiDeg && left.MinHz == right.MinHz &&
         left.MaxHz == right.MaxHz && left.BandwidthDeciHz == right.BandwidthDeciHz &&
         left.ZeroMillivolts == right.ZeroMillivolts &&
         left.MinLevelMillivolts == right.MinLevelMillivolts;
}

inline bool configValuesEqual(const MpptConfig &left, const MpptConfig &right) {
  return left.Enabled == right.Enabled && left.IntervalMs == right.IntervalMs &&
         left.StepMilli == right.StepMilli && left.MinStepMilli == right.MinStepMilli &&
         left.MinIndexMilli == right.MinIndexMilli &&
         left.MaxIndexMilli == right.MaxIndexMilli && left.DeadbandMw == right.DeadbandMw &&
         left.RestartDeltaMw == right.RestartDeltaMw;
}

inline bool configValuesEqual(const PowerMonConfig &left, const PowerMonConfig &right) {
  return left.Enabled == right.Enabled && left.Address == right.Address &&
         left.ShuntMicroOhm == right.ShuntMicroOhm &&
         left.CurrentLsbMicroAmp == right.CurrentLsbMicroAmp &&
         left.AlertMilliAmp == right.AlertMilliAmp && left.IntervalMs == right.IntervalMs &&
         left.PgEfusePin == right.PgEfusePin && left.PgBuckPin == right.PgBuckPin &&
         left.AlertPin == right.AlertPin && left.ImonPin == right.ImonPin &&
         left.ImonRimonOhm == right.ImonRimonOhm;
}

#endif
