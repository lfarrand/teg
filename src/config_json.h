#ifndef CONFIG_JSON_H
#define CONFIG_JSON_H

#include <stdint.h>

struct ChannelConfig {
  uint32_t OnPeriodMicroseconds{};
  uint16_t DutyCycle{};
  uint8_t PhaseShift{};
  bool Enabled = true;
};

struct SubmoduleConfig {
  uint16_t DeadTime{};
  uint32_t PwmFrequency{};
  ChannelConfig ChannelA;
  ChannelConfig ChannelB;
};

struct Module1Config {
  SubmoduleConfig Sm13;
};

struct Module2Config {
  bool UseSpwm = false;
  uint32_t SpwmCarrierFrequency = 20000;
  uint32_t SpwmModulationFrequency = 50;
  uint8_t ModulationScheme = 1;         // modulation.h ModScheme* (1 = unipolar SPWM)
  uint16_t ModulationIndexMilli = 1000; // thousandths; up to 1155 with THIPWM/SVPWM
  uint8_t ModulationCells = 2;          // 1-4 legs/cells, driven in order Sm20, Sm22, Sm21, Sm23
  uint8_t CarrierDisposition = 0;       // level-shifted only: 0 PD, 1 POD, 2 APOD
  bool DeadTimeCompensation = false;    // polarity-signed duty correction of 2*td*fsw
  uint16_t SoftStartMs = 0;             // modulation index ramp time; 0 = instant
  uint8_t ReferenceWaveform = 0;        // 0 sine, 1 trapezoid, 2 square (six-step)
  uint8_t DpwmVariant = 0;              // scheme 7: 0 MIN, 1 MAX, 2 GDPWM, 3 DPWM3
  int8_t DpwmClampAngleDeg = 0;         // GDPWM clamp angle: 0 = DPWM1, -30 = DPWM0, +30 = DPWM2
  uint8_t CarrierDitherMode = 0;        // 0 off, 1 random (LFSR), 2 triangular sweep
  uint8_t CarrierDitherPercent = 0;     // carrier period spread, 0-30%
  bool NearestLevelModulation = false;  // level-shifted only: snap cells to the nearest level
  SubmoduleConfig Sm20;
  SubmoduleConfig Sm21;
  SubmoduleConfig Sm22;
  SubmoduleConfig Sm23;
};

struct Module3Config {
  SubmoduleConfig Sm31;
};

struct Module4Config {
  SubmoduleConfig Sm40;
  SubmoduleConfig Sm41;
  SubmoduleConfig Sm42;
};

struct AsymmetricInductionConfig {
  bool IsEnabled = true;
  int32_t PreShiftNanos = 250;
  int32_t PostShiftNanos = 500;
};

// Closed-loop amplitude regulation: the feedback pin expects a DC voltage
// proportional to the regulated quantity (e.g. rectified+filtered output, or
// the DC bus). The PI output drives the modulation index target.
struct FeedbackConfig {
  bool Enabled = false;
  uint8_t AnalogPin = 41;              // A17
  uint32_t SetpointMillivolts = 0;     // regulate the feedback pin to this voltage
  uint32_t FullScaleMillivolts = 3300; // feedback voltage at full ADC scale
  uint16_t KpMilli = 200;              // index per volt of error, thousandths
  uint16_t KiMilli = 2000;             // index per volt-second, thousandths
  uint16_t LoopHz = 1000;
};

// Fast software trip: a transition on the fault pin masks every FlexPWM
// output from a high-priority GPIO interrupt (~1us). Latched until the next
// settings apply. Pin should be XBAR-capable (30/31/32) so a future hardware
// fault path can reuse it.
struct FaultProtectionConfig {
  bool Enabled = false;
  uint8_t Pin = 32;
  bool ActiveHigh = true;
};

// InfluxDB v2 metrics target. Metrics are disabled until a token is set —
// the token lives ONLY in /settings.cfg (or the web UI), never in source.
struct InfluxConfig {
  char Host[40] = "ub-1.lan";
  uint16_t Port = 8086;
  char Org[24] = "501eaf58ac3171cd";
  char Bucket[32] = "power_generator";
  char Token[96] = "";
};

struct PwmConfig {
  Module1Config Tm1;
  Module2Config Tm2;
  Module3Config Tm3;
  Module4Config Tm4;
  bool PrintRegs = false;
  bool SyncPwm = false;
  bool Verbose = false;
};

struct MainConfig {
  PwmConfig Pwm;
  AsymmetricInductionConfig AsymmetricInduction;
  FeedbackConfig Feedback;
  FaultProtectionConfig FaultProtection;
  InfluxConfig Influx;
};

extern MainConfig config;

void loadConfiguration(const char *filename);

void saveConfiguration(const char *filename);

void printFile(const char *filename);

void loadConfigurationFromFlash(const char* filename);

void saveConfigurationToFlash(const char* filename, const MainConfig& config);

#endif