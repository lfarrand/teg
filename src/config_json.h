#ifndef CONFIG_JSON_H
#define CONFIG_JSON_H

#include <stdint.h>

#include "pwm_pair.h"

struct ChannelConfig {
  uint16_t DutyCycle{};
};

constexpr uint32_t DefaultPwmFrequencyHz = 1000;
constexpr uint32_t DefaultModulationCarrierHz = 20000;
constexpr uint32_t MinRepresentablePwmFrequencyHz = 18;
constexpr uint32_t MaxPwmFrequencyHz = 1000000;
// The modulation ISR, capture and dual-ADC path are release-qualified only up to
// this rate. Fixed-duty outputs may still use MaxPwmFrequencyHz without the ISR.
constexpr uint32_t MaxModulationCarrierHz = 200000;

struct SubmoduleConfig {
  // What this submodule's A/B pins physically drive. PairIndependent is what the
  // firmware has always done, so it stays the default - an update must not change
  // what the pins do on a board already wired. validateConfig() reduces a mode the
  // submodule cannot run (no B pin, or Sm42's independent start/stop timing) back
  // to Independent. See pwm_pair.h.
  uint8_t Pair = PairIndependent;
  // Nanoseconds, consumed only in PairHalfBridge - pairDeadTimeNs() forces zero for
  // the other modes. Defaults to the half-bridge floor rather than zero so that
  // switching a submodule to HalfBridge without touching this field cannot ask for
  // a zero gap, and so the compiled default matches the JSON fallback exactly.
  uint16_t DeadTime = MinHalfBridgeDeadTimeNs;
  // Not zero-initialised. This is the value a board falls back to when there is no
  // readable settings file, and 0 reaches computeAsymmetricTimings() as a divisor -
  // inf, an undefined cast, garbage edge timings, and then enabled outputs. Matches
  // the JSON fallback so the two paths cannot disagree.
  uint32_t PwmFrequency = DefaultPwmFrequencyHz;
  ChannelConfig ChannelA;
  ChannelConfig ChannelB;
};

struct Module1Config {
  SubmoduleConfig Sm13;
};

struct Module2Config {
  bool UseSpwm = false;
  uint32_t SpwmCarrierFrequency = DefaultModulationCarrierHz;
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
  bool WaveformSampleStep = false;      // custom waveform: one stored sample per carrier cycle
                                        // (full resolution, repeat = count/carrier) instead of
                                        // one period per modulation cycle via the DDS
  // FlexPWM2 is one modulation engine. These mirrors are retained in the JSON
  // schema for compatibility, but validateConfig() always derives all four from
  // SpwmCarrierFrequency before any register is touched.
  SubmoduleConfig Sm20{PairIndependent, MinHalfBridgeDeadTimeNs,
                       DefaultModulationCarrierHz, {}, {}};
  SubmoduleConfig Sm21{PairIndependent, MinHalfBridgeDeadTimeNs,
                       DefaultModulationCarrierHz, {}, {}};
  SubmoduleConfig Sm22{PairIndependent, MinHalfBridgeDeadTimeNs,
                       DefaultModulationCarrierHz, {}, {}};
  SubmoduleConfig Sm23{PairIndependent, MinHalfBridgeDeadTimeNs,
                       DefaultModulationCarrierHz, {}, {}};
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
  // Fail-dark compiled default. A missing configuration must never silently
  // select a specialised switching topology.
  bool IsEnabled = false;
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
  uint16_t LoopHz = 250;
};

// Fast software trip: a transition on the fault pin disconnects every FlexPWM
// output from a high-priority GPIO interrupt. The trip is latched until an
// authenticated explicit fault-clear request; applying settings is not an
// acknowledgement. Pin should be interrupt-capable and pass pin ownership checks.
struct FaultProtectionConfig {
  bool Enabled = false;
  uint8_t Pin = 32;
  bool ActiveHigh = true;
};

// Hardware overcurrent protection: an on-chip analog comparator (ACMP)
// compares the current-sense pin against its internal 6-bit DAC threshold;
// the comparator output fans out through XBARA1 to private FAULT0 inputs on
// FlexPWM1 and FlexPWM2, disabling Sm13 A/B (pins 8/7) and the modulated
// Sm20-23 A/B outputs with no software in the loop. Latched mode holds them
// off until the fault is cleared;
// cycle-by-cycle mode lets the hardware re-enable at each cycle boundary
// while the comparator is quiet (current limiting, not a fault).
struct CurrentLimitConfig {
  bool Enabled = false;
  uint8_t Pin = 40;                    // A16, the Meter current-pin default; must be ACMP-reachable
  uint16_t ThresholdMillivolts = 2475; // at the pin; quantized to 3300/64 ~ 51.6mV DAC steps
  bool CycleByCycle = false;           // false = latched fault (manual clear)
  uint8_t FilterCount = 0;             // CMP filter samples 0-7; 0 = continuous mode (RM-recommended)
  uint8_t FilterPeriod = 0;            // CMP sample period in bus clocks; 0 = bypass
};

// InfluxDB v2 metrics target. Metrics are disabled until a token is set —
// the token lives ONLY in /settings.cfg (or the web UI), never in source.
struct InfluxConfig {
  char Host[40] = "ub-1.lan";
  uint16_t Port = 8086;
  char Org[24] = "501eaf58ac3171cd";
  char Bucket[32] = "power_generator";
  char Token[96] = "";
  uint16_t IntervalSeconds = 10; // metrics push cadence; 0 disables
};

// USB MTP file access to the SD card and QSPI flash. OFF by default: a host
// transfer runs to completion inside one service call, stalling the control
// tasks meanwhile, so this is a maintenance-mode feature. USB is always
// composite; enabling starts the service on the next inhibited pass.
// Disabling does not tear the session down until the next boot.
struct MtpConfig {
  bool Enabled = false;
};

// Access control for every /api endpoint. The request must come from the local
// subnet, pass Host/Origin checks and carry a matching X-Auth-Pin header. The
// credential is redacted from configuration responses.
struct SecurityConfig {
  char WritePin[16] = "";
};

// MQTT telemetry with Home Assistant discovery. Read-only in v1 (no command
// topics). Password is a secret: redacted from GET, empty POST keeps the
// stored value - same contract as the Influx token.
struct MqttConfig {
  bool Enabled = false;
  char Host[40] = "";
  uint16_t Port = 1883;
  char Username[32] = "";
  char Password[64] = ""; // HA/Mosquitto setups commonly use long passwords
  char BaseTopic[24] = "teg";
  char DiscoveryPrefix[16] = "homeassistant";
  bool DiscoveryEnabled = true;
  uint16_t IntervalSeconds = 10;
};

// PWM-synchronous waveform capture of the feedback pin into PSRAM (one sample
// per carrier cycle at the reload point). Freezes on fault trip.
struct CaptureConfig {
  bool Enabled = false;
};

// Dual-channel power metering: the capture ISR also samples a current sensor
// (on the other ADC module) every carrier cycle and accumulates V*I.
// Requires Capture.Enabled. Both channels are assumed mid-rail biased.
struct MeterConfig {
  bool Enabled = false;
  uint8_t CurrentPin = 40;               // A16
  uint16_t VoltageZeroMillivolts = 1650; // voltage-sense bias point at the pin
  uint16_t CurrentZeroMillivolts = 1650; // current-sensor zero-amp output
  uint32_t CurrentMilliampPerVolt = 10000; // e.g. ACS712-20A: 100mV/A
  uint32_t VoltageRatioMilli = 1000;       // output volts per pin volt, x1000
                                           // (e.g. a 240:1 divider = 240000)
};

// Grid/reference PLL: locks the DDS fundamental in frequency and phase to an
// external AC reference sensed on the capture channel (Feedback.AnalogPin,
// sampled once per carrier cycle - requires Capture.Enabled). SOGI-QSG +
// SRF-PLL; steers the DDS phase increment only, never the accumulator, so
// the output waveform has no discontinuities. Mutually exclusive with
// carrier dither, stepped waveform playback, and the Feedback amplitude
// loop (which reads the same pin as a DC level).
struct PllConfig {
  bool Enabled = false;
  int16_t PhaseOffsetCentiDeg = 0;  // inverter fundamental leads reference; -18000..18000
  uint16_t MinHz = 45;              // steering clamp = anti-windup rail + lock window
  uint16_t MaxHz = 55;
  uint16_t BandwidthDeciHz = 200;   // loop natural frequency, 0.1Hz units (20.0Hz);
                                    // capped internally at carrier/(20*pi)
  uint16_t ZeroMillivolts = 1650;   // reference mid-rail bias at the pin
  uint16_t MinLevelMillivolts = 100; // amplitude floor: below = no reference
};

// Maximum power point tracking: adaptive-step perturb & observe on the
// modulation index, fed by the Meter's real-power measurement (requires
// Capture + Meter enabled; silently idle otherwise, like the meter itself).
// Mutually exclusive with the Feedback loop (same actuator). Compatible
// with the PLL (frequency/phase vs amplitude on the bench).
struct MpptConfig {
  bool Enabled = false;
  uint16_t IntervalMs = 3000;    // evaluation cadence; must cover the index
                                 // ramp plus a full 1s meter window
  uint16_t StepMilli = 20;       // initial/maximum index step, thousandths
  uint16_t MinStepMilli = 5;     // limit-cycle step at the peak
  uint16_t MinIndexMilli = 50;
  uint16_t MaxIndexMilli = 1000;
  uint16_t DeadbandMw = 10;      // |dP| below this = measurement noise
  uint32_t RestartDeltaMw = 1000; // |dP| above this = source shifted: re-track
};

// Up to two DS18B20 probes on OneWire plus the RT1062 die temperature. Probe
// ROMs are sorted for stable labels, but thermal protection deliberately uses
// the hottest valid external probe or die reading rather than trusting roles.
struct ThermalConfig {
  bool Enabled = false;
  uint8_t OneWirePin = 21;
  uint8_t DerateStartC = 70;
  uint8_t DerateEndC = 90;
};

// Aux power monitor: the MOSFET driver board's built-in telemetry (INA226
// across the 10 mOhm input shunt R24, TPS25983 IMON/PG) read over Wire2
// (pins 24/25) and surfaced on /api/status, MQTT and InfluxDB. Pin value
// 255 = that signal not wired. ImonPin stays off (255) by default: it needs
// R_IMON fitted on the driver board AND the ADC modules free (see
// power_monitor.cpp - the capture ISR owns both when Capture.Enabled).
struct PowerMonConfig {
  bool Enabled = false;
  uint8_t Address = 0x40;              // JP2+JP6 bridged on the driver board
  uint32_t ShuntMicroOhm = 10000;      // R24 = 10 mOhm
  uint16_t CurrentLsbMicroAmp = 50;    // 50 uA/bit -> CAL 10240, FS 1.64 A
  uint16_t AlertMilliAmp = 1500;       // latched shunt alert; 0 disables
  uint16_t IntervalMs = 250;           // I2C poll cadence
  uint8_t PgEfusePin = 14;             // TPS259_PG tap (3.3 V logic)
  uint8_t PgBuckPin = 15;              // TPSM843_PG tap
  uint8_t AlertPin = 20;               // INA226 ALERT, open-drain
  uint8_t ImonPin = 255;               // e.g. 38 once R_IMON is fitted
  uint16_t ImonRimonOhm = 4530;        // R_IMON -> 1.101 V/A at 243 uA/A
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
  CurrentLimitConfig CurrentLimit;
  InfluxConfig Influx;
  MqttConfig Mqtt;
  MtpConfig Mtp;
  SecurityConfig Security;
  CaptureConfig Capture;
  MeterConfig Meter;
  PllConfig Pll;
  MpptConfig Mppt;
  ThermalConfig Thermal;
  PowerMonConfig PowerMon;
};

extern MainConfig config;

// True only when a complete, validated live/temp/backup document was loaded.
bool loadConfiguration(const char *filename);

// Crash-safe save through a verified temporary file and recoverable backup.
bool saveConfiguration(const char *filename);

void printFile(const char *filename);

#endif
