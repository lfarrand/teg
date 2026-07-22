#ifndef CONFIG_JSON_H
#define CONFIG_JSON_H

#include <stdint.h>

struct ChannelConfig {
  uint16_t DutyCycle{};
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
  bool WaveformSampleStep = false;      // custom waveform: one stored sample per carrier cycle
                                        // (full resolution, repeat = count/carrier) instead of
                                        // one period per modulation cycle via the DDS
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

// Hardware overcurrent protection: an on-chip analog comparator (ACMP)
// compares the current-sense pin against its internal 6-bit DAC threshold;
// the comparator output routes through XBARA1 to FlexPWM2's private FAULT0
// input, disabling the modulated outputs (Sm20-23 A/B) with no software in
// the loop. Latched mode holds them off until the fault is cleared;
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

// Write protection for the API: when WritePin is set, POST /api/config
// requires a matching X-Auth-Pin header. Redacted from GET responses.
struct SecurityConfig {
  char WritePin[16] = "";
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
// with the PLL (frequency/phase vs amplitude - the grid-tie MPPT topology).
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

// DS18B20 probes on OneWire (index 0 = TEG hot side, 1 = cold side) plus the
// RT1062 die temperature. The worst of (hot, die) linearly derates the
// modulation index between DerateStartC and DerateEndC.
struct ThermalConfig {
  bool Enabled = false;
  uint8_t OneWirePin = 21;
  uint8_t DerateStartC = 70;
  uint8_t DerateEndC = 90;
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
  SecurityConfig Security;
  CaptureConfig Capture;
  MeterConfig Meter;
  PllConfig Pll;
  MpptConfig Mppt;
  ThermalConfig Thermal;
};

extern MainConfig config;

void loadConfiguration(const char *filename);

void saveConfiguration(const char *filename);

void printFile(const char *filename);

#endif