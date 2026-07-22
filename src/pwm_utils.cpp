#include "pwm_utils.h"
#include "spwm_math.h"
#include "pwm_timing.h"
#include "modulation.h"
#include "capture.h"
#include "thermal.h"
#include "waveform.h"
#include "waveform_parse.h"
#include "utils.h"
#include <QNEthernet.h>
#include "config_json.h"

extern qindesign::network::EthernetServer server;

volatile uint32_t vPhase = 0;
volatile uint32_t vIsrCycles = 0;

// The reference generator is a fixed-point DDS (see spwm_math.h): a 32-bit
// phase accumulator advances by vPhaseIncrement every carrier cycle and wraps
// naturally on overflow — any modulation frequency, no drift, no wrap glitch.
// The LUT holds the SIGNED unit reference waveform (see modulation.h); the
// amplitude is applied at runtime via vIndexQ15 so closed-loop control and
// soft-start never rebuild the table. Lives in DTCM (zero-wait-state).
static int16_t spwmLut[SpwmLutSize];
static volatile uint32_t vPhaseIncrement = 0;

// Runtime amplitude: current index ramps toward the target by vIndexStepQ15
// per carrier cycle (soft-start / slew limit). The feedback loop moves the
// target; the ISR moves the current value.
static volatile uint32_t vIndexQ15 = 0;
static volatile uint32_t vIndexTargetQ15 = 0;
static volatile uint32_t vIndexStepQ15 = 1UL << 30;
static volatile int32_t vDtCompQ15 = 0;

volatile bool vFaultTripped = false;

// Modulation cells map onto FlexPWM2 submodules in this order; cells=2 drives
// Sm20+Sm22 (the original SPWM pair), 3-4 add Sm21/Sm23.
static SubModule *const CellSm[MaxModulationCells] = {&Sm20, &Sm22, &Sm21, &Sm23};
static const uint8_t CellLdokMasks[MaxModulationCells + 1] = {0, 0b0001, 0b0101, 0b0111, 0b1111};

static CellPlan cellPlan[MaxModulationCells]; // written only with the SPWM IRQ disabled
static volatile uint8_t vModScheme = ModSchemeSpwmUnipolar;
static volatile uint8_t vModCells = 2;
static volatile uint8_t vCellLdokMask = 0b0101;
static volatile uint8_t vDpwmVariant = DpwmMin;
static volatile uint32_t vDpwmClampPhase = 0;
static volatile bool vNearestLevel = false;

// Spread-spectrum carrier: per-cycle period entries with matched DDS
// increments (see modulation.h). All in DTCM; written with the IRQ disabled.
static volatile uint8_t vDitherMode = DitherOff;
static uint16_t ditherPeriods[DitherTableSize];
static uint32_t ditherIncrements[DitherTableSize];

// Sequence player (RefWaveSequence): per-segment reference levels and
// durations in carrier cycles. State is ISR-owned; (re)written only while the
// modulation IRQ is disabled.
static int16_t seqLevels[MaxWaveSegments];
static uint32_t seqCycles[MaxWaveSegments];
static volatile uint8_t vSeqCount = 0;
static volatile bool vRefSequence = false;
static uint8_t seqIndex = 0;
static uint32_t seqRemaining = 0;

// Sample-step playback of a long custom waveform: one stored sample per
// carrier cycle, read sequentially from the PSRAM store (cache-friendly),
// exact integer wrap at the end. State is ISR-owned, (re)written with the
// IRQ disabled.
static volatile bool vSampleStep = false;
static const int16_t *vWaveSamples = nullptr;
static uint32_t vWaveCount = 0;
static uint32_t waveIndex = 0;

// Beyond-PSRAM waveforms play through the SD double buffer (waveform.cpp)
static volatile bool vStreamPlayback = false;

bool spwmActive() {
  return config.Pwm.Tm2.UseSpwm && config.Pwm.Tm2.ModulationScheme != ModSchemeFixed;
}

const char *prescaleStr[] = {
  "fclk/1", "fclk/2", "fclk/4", "fclk/8", "fclk/16", "fclk/32", "fclk/64", "fclk/128"
};

SubModule Sm13(8, 7);
SubModule Sm20(4, 33);
SubModule Sm21(5);
SubModule Sm22(6, 9);
SubModule Sm23(36, 37);
SubModule Sm31(29, 28);
SubModule Sm40(22);
SubModule Sm41(23);
SubModule Sm42(2, 3);

eFlex::Timer &Tm1 = Sm13.timer();
eFlex::Timer &Tm2 = Sm20.timer();
eFlex::Timer &Tm3 = Sm31.timer();
eFlex::Timer &Tm4 = Sm40.timer();

extern MainConfig config;

struct SubmoduleSettings {
  uint32_t frequency;
  uint32_t deadtimeNs;
  uint16_t dutyA;
  uint16_t dutyB; // UINT16_MAX if no ChanB
  bool hasChanB;
  // Add phaseShift, etc., for special cases
};

void setupSubmodule(SubModule &sm, const SubmoduleSettings &settings, bool printRegs) {
  constexpr uint32_t NanosecondsUnit = 1000000000;

  sm.setPwmLdok(false); // clear any pending LDOK so the buffered register writes below take

  // unit must be passed explicitly: setupDeadtime(value, ChanA) resolves to the
  // all-channels overload with unit=0 and silently divides by zero
  sm.setupDeadtime(settings.deadtimeNs, NanosecondsUnit);

  sm.setupDutyCycle(ChanA, settings.dutyA);
  if (settings.hasChanB && settings.dutyB != UINT16_MAX) {
    sm.setupDutyCycle(ChanB, settings.dutyB);
  }

  if (!sm.setPwmFrequency(settings.frequency, false, true)) {
    writeLog("Failed to set PWM freq");
  }

  // First call only: pin mux + counter start. No-op on reconfiguration.
  if (!sm.begin(true, false, false)) {
    writeLog("Failed to start submodule");
  }

  // Programs deadtime (DTCNT), output enable (OUTEN), polarity and duty registers.
  // begin() skips this on reconfiguration, so it must be called explicitly every time.
  if (!sm.updateSetting(false)) {
    writeLog("Failed to apply submodule settings");
  }

  sm.timer().enable();
  sm.setPwmLdok(true); // hardware loads the buffered values at the next PWM reload

  if (printRegs) {
    sm.printRegs();
  }
}

void configurePwm() {
  configureModule1();
  configureModule2();
  configureModule3();
  configureModule4();
}

// Reconfigure only the timers whose settings actually changed, so an update to
// one submodule never disturbs the others' outputs or phase.
void clearFaultTrip() {
  if (!vFaultTripped) {
    return;
  }
  vFaultTripped = false;
  writeLog("Fault cleared; re-enabling PWM outputs");
  Tm1.enable();
  Tm2.enable();
  Tm3.enable();
  Tm4.enable();
  captureConfigure(); // unfreeze the flight recorder
  if (spwmActive()) {
    attachModule2PwmInterruptVectors();
    enablePwmInterrupts();
  }
}

void applyPwmConfig(const MainConfig &previous) {
  clearFaultTrip();
  if (memcmp(&previous.FaultProtection, &config.FaultProtection, sizeof(config.FaultProtection)) != 0) {
    configureFaultProtection();
  }
  // Re-arms capture (and clears a freeze) on every apply — also covers the
  // fault-clear path above
  captureConfigure();
  thermalConfigure();
  if (memcmp(&previous.Pwm.Tm1, &config.Pwm.Tm1, sizeof(config.Pwm.Tm1)) != 0) {
    configureModule1();
  }
  if (memcmp(&previous.Pwm.Tm2, &config.Pwm.Tm2, sizeof(config.Pwm.Tm2)) != 0) {
    configureModule2();
  }
  if (memcmp(&previous.Pwm.Tm3, &config.Pwm.Tm3, sizeof(config.Pwm.Tm3)) != 0) {
    configureModule3();
  }
  if (memcmp(&previous.Pwm.Tm4, &config.Pwm.Tm4, sizeof(config.Pwm.Tm4)) != 0 ||
      memcmp(&previous.AsymmetricInduction, &config.AsymmetricInduction,
             sizeof(config.AsymmetricInduction)) != 0) {
    configureModule4();
  }
}

void buildSpwmLut() {
  const Module2Config &tm2 = config.Pwm.Tm2;

  uint8_t cells = tm2.ModulationCells;
  if (cells < 1) cells = 1;
  if (cells > MaxModulationCells) cells = MaxModulationCells;
  if (tm2.ModulationScheme == ModSchemeSvpwm || tm2.ModulationScheme == ModSchemeDpwm) {
    cells = 3; // three phase legs, always
  }
  if (tm2.ModulationScheme == ModSchemeSvpwm3D) {
    cells = 4; // three phase legs + the neutral leg (Sm23, pins 36/37)
  }

  uint16_t indexMilli = tm2.ModulationIndexMilli;
  if (indexMilli > MaxModulationIndexMilli) indexMilli = MaxModulationIndexMilli;

  // Reference source: built-in shapes, an uploaded reference table, or the
  // uploaded segment sequence (which bypasses the DDS entirely)
  vRefSequence = false;
  vSampleStep = false;
  vStreamPlayback = false;
  if (tm2.ModulationScheme == ModSchemeThipwm) {
    buildUnitReferenceLut(spwmLut, SpwmLutSize, RefWaveSine, true);
  } else if (tm2.ReferenceWaveform == RefWaveCustom) {
    if (waveformType() == WaveTypeReference && waveformIsStreaming()) {
      // Too long for PSRAM: one sample per carrier cycle straight off SD
      // (period mode is meaningless at this scale; the toggle is ignored)
      vStreamPlayback = true;
      buildUnitReferenceLut(spwmLut, SpwmLutSize, RefWaveSine, false); // unused
    } else if (waveformType() == WaveTypeReference) {
      if (tm2.WaveformSampleStep) {
        // Full-resolution playback straight from the PSRAM store
        vWaveSamples = waveformSamples();
        vWaveCount = waveformCount();
        waveIndex = 0;
        vSampleStep = true;
        buildUnitReferenceLut(spwmLut, SpwmLutSize, RefWaveSine, false); // unused
      } else {
        // Period mode: the whole file is one fundamental period, rendered
        // through the 2048-point DDS table
        resampleReference(waveformSamples(), waveformCount(), spwmLut, SpwmLutSize);
      }
    } else {
      buildUnitReferenceLut(spwmLut, SpwmLutSize, RefWaveSine, false);
      writeLog("No reference waveform uploaded - using sine");
    }
  } else if (tm2.ReferenceWaveform == RefWaveSequence) {
    buildUnitReferenceLut(spwmLut, SpwmLutSize, RefWaveSine, false); // unused fallback
    const int16_t *levels;
    const uint32_t *micros;
    const uint32_t n = waveformSegments(&levels, &micros);
    if (n > 0) {
      for (uint32_t k = 0; k < n; k++) {
        seqLevels[k] = levels[k];
        seqCycles[k] = microsToCycles(micros[k], tm2.SpwmCarrierFrequency);
      }
      vSeqCount = static_cast<uint8_t>(n);
      seqIndex = 0;
      seqRemaining = seqCycles[0];
      vRefSequence = true;
    } else {
      writeLog("No sequence uploaded - using sine");
    }
  } else {
    buildUnitReferenceLut(spwmLut, SpwmLutSize, tm2.ReferenceWaveform, false);
  }
  vDpwmVariant = tm2.DpwmVariant;
  vDpwmClampPhase = degreesToPhase(tm2.DpwmClampAngleDeg);
  vNearestLevel = tm2.NearestLevelModulation;

  for (uint8_t k = 0; k < MaxModulationCells; k++) {
    cellPlan[k] = modulationCellPlan(tm2.ModulationScheme, tm2.CarrierDisposition, k, cells);
  }
  vModScheme = tm2.ModulationScheme;
  vModCells = cells;
  vCellLdokMask = CellLdokMasks[cells];

  const uint32_t carrier = tm2.SpwmCarrierFrequency;
  vPhaseIncrement = spwmPhaseIncrement(carrier, tm2.SpwmModulationFrequency);
  vPhase = 0;

  // Amplitude: the ISR ramps the current index toward the target. The
  // current value persists across reconfigures, so soft-start ramps from
  // wherever the output already is, and from zero on a cold start.
  const uint32_t targetQ15 = indexMilliToQ15(indexMilli);
  vIndexStepQ15 = softStartStepQ15(targetQ15, tm2.SoftStartMs, carrier);
  setModulationIndexTargetQ15(targetQ15); // applies the thermal derate cap

  vDtCompQ15 = tm2.DeadTimeCompensation ? deadtimeCompQ15(tm2.Sm20.DeadTime, carrier) : 0;

  const uint64_t actualMilliHz = spwmActualMilliHz(vPhaseIncrement, carrier);
  char strBuf[LOG_BUF_SIZE];
  snprintf(strBuf, sizeof(strBuf),
           "Modulation: scheme %u, %u cell(s), index %u/1000, %lu.%03luHz on %luHz carrier, dtcomp %ld/32768",
           vModScheme, cells, indexMilli,
           static_cast<uint32_t>(actualMilliHz / 1000), static_cast<uint32_t>(actualMilliHz % 1000), carrier,
           static_cast<long>(vDtCompQ15));
  writeLog(strBuf);
}

// Thermal derating acts as a ceiling on the modulation index (see thermal.cpp)
static volatile uint16_t vThermalDerateMilli = 1000;

void setThermalDerateMilli(uint16_t derateMilli) {
  vThermalDerateMilli = derateMilli > 1000 ? 1000 : derateMilli;
}

// Clamped setter for the closed-loop controller (and anything else that wants
// live amplitude control without touching the LUT). The effective ceiling is
// the scheme maximum scaled by the current thermal derate factor.
void setModulationIndexTargetQ15(uint32_t targetQ15) {
  const uint32_t capQ15 = (indexMilliToQ15(MaxModulationIndexMilli) * vThermalDerateMilli) / 1000U;
  vIndexTargetQ15 = targetQ15 > capQ15 ? capQ15 : targetQ15;
}

// Live telemetry for the web UI status endpoint
uint32_t modulationIndexNowMilli() {
  return (vIndexQ15 * 1000UL) >> 15;
}

uint32_t modulationIndexTargetMilli() {
  return (vIndexTargetQ15 * 1000UL) >> 15;
}

uint64_t modulationActualMilliHz() {
  return spwmActualMilliHz(vPhaseIncrement, config.Pwm.Tm2.SpwmCarrierFrequency);
}

void configureModule1() {
  SubmoduleSettings settings = {
    .frequency = config.Pwm.Tm1.Sm13.PwmFrequency,
    .deadtimeNs = config.Pwm.Tm1.Sm13.DeadTime,
    .dutyA = config.Pwm.Tm1.Sm13.ChannelA.DutyCycle,
    .dutyB = config.Pwm.Tm1.Sm13.ChannelB.DutyCycle,
    .hasChanB = true
  };
  setupSubmodule(Sm13, settings, config.Pwm.PrintRegs);
  writeLog("Started TM1");
}

void configureModule2() {
  // Precompute the reference table and cell plans if modulation is enabled
  if (spwmActive()) {
    buildSpwmLut();
  }

  // Carrier geometry: 180deg-shifted cells run with inverted output polarity
  // (the ISR complements their duty). Everything else is explicitly restored
  // to normal polarity so scheme changes never leave a leg inverted.
  for (uint8_t k = 0; k < MaxModulationCells; k++) {
    const bool inverted = spwmActive() && k < vModCells && cellPlan[k].polarityInverted;
    CellSm[k]->setupLevel(inverted ? kPWM_LowTrue : kPWM_HighTrue);
  }

  // Configure Sm20 (Channels A and B)
  SubmoduleSettings sm20Settings = {
    .frequency = config.Pwm.Tm2.Sm20.PwmFrequency,
    .deadtimeNs = config.Pwm.Tm2.Sm20.DeadTime,
    .dutyA = config.Pwm.Tm2.Sm20.ChannelA.DutyCycle,
    .dutyB = config.Pwm.Tm2.Sm20.ChannelB.DutyCycle,
    .hasChanB = true
  };
  setupSubmodule(Sm20, sm20Settings, config.Pwm.PrintRegs);

  // Configure Sm21 (Channel A only)
  SubmoduleSettings sm21Settings = {
    .frequency = config.Pwm.Tm2.Sm21.PwmFrequency,
    .deadtimeNs = config.Pwm.Tm2.Sm21.DeadTime,
    .dutyA = config.Pwm.Tm2.Sm21.ChannelA.DutyCycle,
    .dutyB = UINT16_MAX, // No Channel B
    .hasChanB = false
  };
  setupSubmodule(Sm21, sm21Settings, config.Pwm.PrintRegs);

  // Configure Sm22 (Channels A and B)
  SubmoduleSettings sm22Settings = {
    .frequency = config.Pwm.Tm2.Sm22.PwmFrequency,
    .deadtimeNs = config.Pwm.Tm2.Sm22.DeadTime,
    .dutyA = config.Pwm.Tm2.Sm22.ChannelA.DutyCycle,
    .dutyB = config.Pwm.Tm2.Sm22.ChannelB.DutyCycle,
    .hasChanB = true
  };
  setupSubmodule(Sm22, sm22Settings, config.Pwm.PrintRegs);

  // Configure Sm23 (Channels A and B)
  SubmoduleSettings sm23Settings = {
    .frequency = config.Pwm.Tm2.Sm23.PwmFrequency,
    .deadtimeNs = config.Pwm.Tm2.Sm23.DeadTime,
    .dutyA = config.Pwm.Tm2.Sm23.ChannelA.DutyCycle,
    .dutyB = config.Pwm.Tm2.Sm23.ChannelB.DutyCycle,
    .hasChanB = true
  };
  setupSubmodule(Sm23, sm23Settings, config.Pwm.PrintRegs);

  // Dither tables need the final prescaler, so build them after submodule setup
  buildDitherState();

  writeLog("Started TM2");
}

void buildDitherState() {
  const Module2Config &tm2 = config.Pwm.Tm2;
  if (!spwmActive() || tm2.CarrierDitherMode == DitherOff || tm2.CarrierDitherPercent == 0) {
    vDitherMode = DitherOff;
    return;
  }
  const uint32_t effectiveClockHz = F_BUS_ACTUAL >> Sm20.prescaler();
  buildDitherTables(ditherPeriods, ditherIncrements, effectiveClockHz,
                    tm2.SpwmCarrierFrequency, tm2.SpwmModulationFrequency,
                    tm2.CarrierDitherPercent);
  vDitherMode = tm2.CarrierDitherMode;

  char strBuf[LOG_BUF_SIZE];
  snprintf(strBuf, sizeof(strBuf), "Carrier dither: mode %u, +/-%u%%, %u-%u ticks",
           vDitherMode, tm2.CarrierDitherPercent > MaxDitherPercent ? MaxDitherPercent : tm2.CarrierDitherPercent,
           ditherPeriods[0], ditherPeriods[DitherTableSize - 1]);
  writeLog(strBuf);
}

void configureModule3() {
  SubmoduleSettings settings = {
    .frequency = config.Pwm.Tm3.Sm31.PwmFrequency,
    .deadtimeNs = config.Pwm.Tm3.Sm31.DeadTime,
    .dutyA = config.Pwm.Tm3.Sm31.ChannelA.DutyCycle,
    .dutyB = config.Pwm.Tm3.Sm31.ChannelB.DutyCycle,
    .hasChanB = true
  };
  setupSubmodule(Sm31, settings, config.Pwm.PrintRegs);
  writeLog("Started TM3");
}

void configureModule4() {
  // Configure Sm40 (Channel A only)
  SubmoduleSettings sm40Settings = {
    .frequency = config.Pwm.Tm4.Sm40.PwmFrequency,
    .deadtimeNs = config.Pwm.Tm4.Sm40.DeadTime,
    .dutyA = config.Pwm.Tm4.Sm40.ChannelA.DutyCycle,
    .dutyB = UINT16_MAX, // No Channel B
    .hasChanB = false
  };
  setupSubmodule(Sm40, sm40Settings, config.Pwm.PrintRegs);

  // Configure Sm41 (Channel A only, with phase shift handling)
  SubmoduleSettings sm41Settings = {
    .frequency = config.Pwm.Tm4.Sm41.PwmFrequency,
    .deadtimeNs = config.Pwm.Tm4.Sm41.DeadTime,
    .dutyA = config.Pwm.Tm4.Sm41.ChannelA.DutyCycle,
    .dutyB = UINT16_MAX, // No Channel B
    .hasChanB = false
  };
  setupSubmodule(Sm41, sm41Settings, config.Pwm.PrintRegs);

  // Configure Sm42 (Channels A and B, with phase shift and Asymmetric Induction)
  if (config.AsymmetricInduction.IsEnabled) {
    // Asymmetric Induction mode: Custom timing for Sm42
    const AsymmetricTimings t = computeAsymmetricTimings(
      F_BUS_ACTUAL, config.Pwm.Tm4.Sm42.PwmFrequency, config.Pwm.Tm4.Sm42.ChannelA.DutyCycle,
      config.AsymmetricInduction.PreShiftNanos, config.AsymmetricInduction.PostShiftNanos,
      MAX_COUNTER_VALUE);

    Sm42.disableOutput(ChanA);
    Sm42.disableOutput(ChanB);

    Sm42.setPwmLdok(false);
    Sm42.setPrescaler(static_cast<pwm_clock_prescale_t>(t.prescalerIndex));
    Sm42.setupDeadtime(config.Pwm.Tm4.Sm42.DeadTime, 1000000000);

    if (!Sm42.setPwmFrequency(config.Pwm.Tm4.Sm42.PwmFrequency, false, true)) {
      writeLog("Failed to set SM42 PWM freq.");
    }

    // First call only: pin mux + counter start. No-op on reconfiguration.
    if (!Sm42.begin(true, false, false)) {
      writeLog("Failed to start SM42");
    }

    // Programs deadtime/output-enable/polarity plus standard VALx values...
    if (!Sm42.updateSetting(false)) {
      writeLog("Failed to apply SM42 settings");
    }

    // ...which the custom asymmetric edge timings then overwrite
    Sm42.setInitValue(t.periodStart);
    Sm42.setVal0Value(t.periodStart);
    Sm42.setVal1Value(t.periodEnd);
    Sm42.setVal2Value(t.startChanA);
    Sm42.setVal3Value(t.stopChanA);
    Sm42.setVal4Value(t.startChanB);
    Sm42.setVal5Value(t.stopChanB);

    Sm42.setPwmLdok(true);
    Tm4.enable();
    Sm42.enableOutput(ChanA);
    Sm42.enableOutput(ChanB);
  } else {
    // Standard mode for Sm42
    SubmoduleSettings sm42Settings = {
      .frequency = config.Pwm.Tm4.Sm42.PwmFrequency,
      .deadtimeNs = config.Pwm.Tm4.Sm42.DeadTime,
      .dutyA = config.Pwm.Tm4.Sm42.ChannelA.DutyCycle,
      .dutyB = config.Pwm.Tm4.Sm42.ChannelB.DutyCycle,
      .hasChanB = true
    };
    setupSubmodule(Sm42, sm42Settings, config.Pwm.PrintRegs);
  }

  // Log summary for all submodules
  if (config.Pwm.Verbose) {
    char strBuf[LOG_BUF_SIZE];
    snprintf(strBuf, sizeof(strBuf), "PWM 4.0: %luHz, dtA:%u, dcA:%u%%",
             Sm40.pwmFrequency(), Sm40.deadtimeSetting(ChanA), Sm40.dutyCycleSetting(ChanA));
    writeLog(strBuf);
    snprintf(strBuf, sizeof(strBuf), "PWM 4.1: %luHz, dtA:%u, dcA:%u%%",
             Sm41.pwmFrequency(), Sm41.deadtimeSetting(ChanA), Sm41.dutyCycleSetting(ChanA));
    writeLog(strBuf);
    snprintf(strBuf, sizeof(strBuf), "PWM 4.2: %luHz, dtA:%u, dcA:%u%%, dtB:%u, dcB:%u%%",
             Sm42.pwmFrequency(), Sm42.deadtimeSetting(ChanA), Sm42.dutyCycleSetting(ChanA),
             Sm42.deadtimeSetting(ChanB), Sm42.dutyCycleSetting(ChanB));
    writeLog(strBuf);
  }

  writeLog("Started TM4");
}

uint8_t calculateBestPrescaler(uint32_t pwmFrequency) {
  return bestPrescalerIndex(F_BUS_ACTUAL, pwmFrequency, MAX_COUNTER_VALUE);
}

void attachInterruptVectors() {
  Serial.println(F("Attaching PWM interrupt vectors"));

  if (spwmActive()) {
    attachModule2PwmInterruptVectors();
    enablePwmInterrupts();
  }
}

void attachModule2PwmInterruptVectors() {
  Serial.println(F("Attaching module 2 PWM interrupt vectors"));
  attachInterruptVector(IRQ_FLEXPWM2_0, &IsrOverflowSm20);
}

void enablePwmInterrupts() {
  Serial.println(F("Enabling PWM interrupts"));

  enableModule2PwmInterrupts();
}

void disablePwmInterrupts() {
  Serial.println(F("Disabling PWM interrupts"));

  disableModule2PwmInterrupts();
}

void enableModule2PwmInterrupts() {
  Serial.println(F("Enabling module 2 PWM interrupts"));
  // Default priority is 128, shared with USB/Ethernet/systick; raise the SPWM
  // duty-update IRQ above them so their handlers can't add jitter.
  NVIC_SET_PRIORITY(IRQ_FLEXPWM2_0, 32);
  NVIC_ENABLE_IRQ(IRQ_FLEXPWM2_0);
  Sm20.enableInterrupts(kPWM_CompareVal1InterruptEnable);
}

void disableModule2PwmInterrupts() {
  Serial.println(F("Disabling module 2 PWM interrupts"));
  Sm20.disableInterrupts(kPWM_CompareVal1InterruptEnable);
  NVIC_DISABLE_IRQ(IRQ_FLEXPWM2_0);
}

// Integer-only on purpose: an FPU instruction here would trigger the M7's lazy
// FPU context stacking (~17 extra cycles and a 26-word exception frame).
FASTRUN void IsrOverflowSm20() {
  const uint32_t t0 = ARM_DWT_CYCCNT;

  digitalToggleFast(TriggerPin);

  const uint32_t phase = vPhase;

  // Slew-limited amplitude: soft-start ramp and closed-loop target tracking
  const uint32_t idx = rampIndexQ15(vIndexQ15, vIndexTargetQ15, vIndexStepQ15);
  vIndexQ15 = idx;

  const uint8_t scheme = vModScheme;
  const uint8_t cells = vModCells;
  const uint8_t mask = vCellLdokMask;
  const int32_t comp = vDtCompQ15;
  uint32_t increment = vPhaseIncrement;

  Tm2.setPwmLdok(mask, false);

  // Spread-spectrum carrier: pick this cycle's period and its matched DDS
  // increment, and rewrite the (buffered) period registers before the duty
  // update below reads VAL1 back
  const uint8_t dither = vDitherMode;
  if (dither != DitherOff) {
    static uint16_t lfsr = 0xACE1;
    static uint32_t sweep = 0;
    uint32_t entry;
    if (dither == DitherRandom) {
      lfsr = nextLfsr16(lfsr);
      entry = lfsr & (DitherTableSize - 1);
    } else {
      entry = triangleIndex(sweep++, DitherTableSize);
    }
    const uint16_t period = ditherPeriods[entry];
    increment = ditherIncrements[entry];
    for (uint8_t k = 0; k < cells; k++) {
      CellSm[k]->setVal0Value(period >> 1);
      CellSm[k]->setVal1Value(period - 1);
    }
  }

  vPhase = phase + increment; // wraps on overflow = seamless cycle boundary

  const bool sequence = vRefSequence;
  const bool stepped = sequence || vSampleStep || vStreamPlayback;

  if (!stepped && (scheme == ModSchemeSvpwm || scheme == ModSchemeDpwm || scheme == ModSchemeSvpwm3D)) {
    int32_t v[3];
    threePhaseScaledRefs(spwmLut, phase, idx, v);
    const int32_t zss = scheme == ModSchemeDpwm
                          ? dpwmZss(spwmLut, phase, vDpwmClampPhase, vDpwmVariant, v)
                          : continuousSvmZss(v);
    for (uint8_t k = 0; k < 3; k++) {
      CellSm[k]->updateDutyCycle(dutyFromScaled(v[k] + zss, comp, v[k]));
    }
    if (scheme == ModSchemeSvpwm3D) {
      // Neutral leg carries the zero sequence, so phase-to-neutral = pure
      // reference. No dead-time comp: its current is the (unknown) sum of
      // the phase currents.
      CellSm[3]->updateDutyCycle(dutyFromScaled(zss, 0, 0));
    }
  } else {
    int32_t s;
    if (sequence) {
      // Segment player: hold each level for its duration in carrier cycles
      if (seqRemaining == 0) {
        seqIndex = static_cast<uint8_t>(seqIndex + 1 >= vSeqCount ? 0 : seqIndex + 1);
        seqRemaining = seqCycles[seqIndex];
      }
      seqRemaining--;
      s = seqLevels[seqIndex];
    } else if (vSampleStep) {
      // One stored sample per carrier cycle, repeating ad infinitum
      s = vWaveSamples[waveIndex];
      if (++waveIndex >= vWaveCount) {
        waveIndex = 0;
      }
    } else if (vStreamPlayback) {
      s = waveformStreamNext(); // SD double buffer; holds last sample on underrun
    } else {
      s = refFromPhase(spwmLut, phase);
    }
    // Dead-time compensation applies to full-reference legs; the band-sliced
    // level-shifted cells switch at most one pair per instant, handled per cell
    const uint16_t ref = refToDuty(s, idx, scheme == ModSchemeLevelShifted ? 0 : comp);
    const bool nlm = vNearestLevel;
    for (uint8_t k = 0; k < cells; k++) {
      const uint16_t duty =
        modulationFinalDuty(modulationCellDuty(scheme, ref, k, cells, nlm), cellPlan[k]);
      CellSm[k]->updateDutyCycle(duty);
    }
  }

  Sm20.clearStatusFlags(kPWM_CompareVal1Flag);

  Tm2.setPwmLdok(mask, true);

  captureTick(); // reload point = average-current instant for centre-aligned PWM

  vIsrCycles = ARM_DWT_CYCCNT - t0;

  asm volatile("dsb");
}

// ---------------------------------------------------------------------------
// Closed-loop amplitude regulation (item: feedback). Runs from loop() at
// Feedback.LoopHz; the feedback pin carries a DC voltage proportional to the
// regulated quantity. The PI output drives the modulation index target, which
// the ISR tracks with its slew limit.
// ---------------------------------------------------------------------------
#include "pi_controller.h"

void runFeedbackLoop() {
  if (!config.Feedback.Enabled || !spwmActive()) {
    return;
  }

  static elapsedMicros sinceLastRun;
  const uint32_t loopHz = config.Feedback.LoopHz > 0 ? config.Feedback.LoopHz : 1000;
  const uint32_t periodUs = 1000000UL / loopHz;
  if (sinceLastRun < periodUs) {
    return;
  }
  const float dt = static_cast<float>(static_cast<uint32_t>(sinceLastRun)) / 1000000.0f;
  sinceLastRun = 0;

  // Prefer the PWM-synchronous samples (12-bit, taken at the reload point)
  // when capture is running; fall back to a plain 10-bit analogRead otherwise
  float measuredMv;
  if (captureActive() && !captureIsFrozen()) {
    measuredMv = static_cast<float>(captureMeanRaw(64)) *
                 (static_cast<float>(config.Feedback.FullScaleMillivolts) / 4095.0f);
  } else {
    const int raw = analogRead(config.Feedback.AnalogPin);
    measuredMv = static_cast<float>(raw) * (static_cast<float>(config.Feedback.FullScaleMillivolts) / 1023.0f);
  }
  const float errorVolts = (static_cast<float>(config.Feedback.SetpointMillivolts) - measuredMv) / 1000.0f;

  static PiController pi;
  pi.kp = static_cast<float>(config.Feedback.KpMilli) / 1000.0f;
  pi.ki = static_cast<float>(config.Feedback.KiMilli) / 1000.0f;
  pi.outMin = 0.0f;
  pi.outMax = static_cast<float>(MaxModulationIndexMilli) / 1000.0f;

  const float indexOut = piUpdate(pi, errorVolts, dt);
  setModulationIndexTargetQ15(static_cast<uint32_t>(indexOut * 32768.0f));
}

// ---------------------------------------------------------------------------
// Fault protection (item: protection). Fast software trip: a transition on
// the fault pin masks every FlexPWM output from a high-priority GPIO
// interrupt (~1us pin-to-off). Latched until the next settings apply.
// A true hardware fault path (XBAR -> FlexPWM FAULT0, <10ns, zero software)
// can reuse the same pin; see docs/RT1170_PSPWM.md for the bring-up notes.
// ---------------------------------------------------------------------------
FASTRUN static void faultTripIsr() {
  // Mask all outputs on all four timers (MASK register + local FORCE)
  Tm1.disable();
  Tm2.disable();
  Tm3.disable();
  Tm4.disable();
  NVIC_DISABLE_IRQ(IRQ_FLEXPWM2_0);
  vFaultTripped = true;
  asm volatile("dsb");
}

void configureFaultProtection() {
  static bool attached = false;
  static uint8_t attachedPin = 0;

  if (attached) {
    detachInterrupt(digitalPinToInterrupt(attachedPin));
    attached = false;
  }

  if (!config.FaultProtection.Enabled) {
    return;
  }

  const uint8_t pin = config.FaultProtection.Pin;
  pinMode(pin, config.FaultProtection.ActiveHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pin), faultTripIsr,
                  config.FaultProtection.ActiveHigh ? RISING : FALLING);
  // Trip must preempt everything, including the modulation ISR (priority 32)
  NVIC_SET_PRIORITY(IRQ_GPIO6789, 16);
  attached = true;
  attachedPin = pin;

  char strBuf[LOG_BUF_SIZE];
  snprintf(strBuf, sizeof(strBuf), "Fault trip armed on pin %u (active %s)",
           pin, config.FaultProtection.ActiveHigh ? "high" : "low");
  writeLog(strBuf);
}

bool xbarConnect(uint8_t input, uint8_t output) {
  if (input >= 88 || output >= 132) {
    return false;
  }

  char strBuf[LOG_BUF_SIZE];
  uint32_t ret;
  volatile uint16_t *xbar_select_reg = &XBARA1_SEL0 + (output / 2);
  uint16_t val = *xbar_select_reg;

  ret = snprintf(strBuf, sizeof(strBuf),
                 "  Value of XBARA1_SEL%hu (address 0x%" PRIXPTR ") register before writing is 0x%04X", (output / 2),
                 reinterpret_cast<uintptr_t>(&XBARA1_SEL0 + (output / 2)), *xbar_select_reg);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  if (output & 1) {
    val = (val & 0x00FF) | (input << 8);
  } else {
    val = (val & 0xFF00) | input;
  }

  ret = snprintf(strBuf, sizeof(strBuf), "  Writing value 0x%04X to register XBARA1_SEL%hu (address 0x%" PRIXPTR ")",
                 val, (output / 2), reinterpret_cast<uintptr_t>(&XBARA1_SEL0 + (output / 2)));
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  *xbar_select_reg = val;

  ret = snprintf(strBuf, sizeof(strBuf),
                 "  Value of XBARA1_SEL%hu (address 0x%" PRIXPTR ") register after writing is 0x%04X", (output / 2),
                 reinterpret_cast<uintptr_t>(&XBARA1_SEL0 + (output / 2)), *xbar_select_reg);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  return true;
}

void enableXbar() {
  writeLog("Enabling XBAR");

  CCM_CCGR2 |= CCM_CCGR2_XBAR1(CCM_CCGR_ON);

  writeLog("XBAR connecting PIT TRIG0 to PWM1.3 EXT_SYNC");
  if (xbarConnect(XBARA1_IN_PIT_TRIGGER0, XBARA1_OUT_FLEXPWM1_PWM3_EXT_SYNC)) {
    writeLog("XBAR connected PIT TRIG0 to PWM1.3 EXT_SYNC");
  } else {
    writeLog("ERROR: XBAR did not connect PIT TRIG0 to PWM1.3 EXT_SYNC");
  }

  writeLog("XBAR connecting PIT TRIG0 to PWM2.0 EXT_SYNC");
  if (xbarConnect(XBARA1_IN_PIT_TRIGGER0, XBARA1_OUT_FLEXPWM2_PWM0_EXT_SYNC)) {
    writeLog("XBAR connected PIT TRIG0 to PWM2.0 EXT_SYNC");
  } else {
    writeLog("ERROR: XBAR did not connect PIT TRIG0 to PWM2.0 EXT_SYNC");
  }

  writeLog("XBAR connecting PIT TRIG0 to PWM3.1 EXT_SYNC");
  if (xbarConnect(XBARA1_IN_PIT_TRIGGER0, XBARA1_OUT_FLEXPWM3_EXT_SYNC1)) {
    writeLog("XBAR connected PIT TRIG0 to PWM3.1 EXT_SYNC");
  } else {
    writeLog("ERROR: XBAR did not connect PIT TRIG0 to PWM3.1 EXT_SYNC");
  }

  writeLog("XBAR connecting PIT TRIG0 to PWM4.0 EXT_SYNC");
  if (xbarConnect(XBARA1_IN_PIT_TRIGGER0, XBARA1_OUT_FLEXPWM4_EXT_SYNC0)) {
    writeLog("XBAR connected PIT TRIG0 to PWM4.0 EXT_SYNC");
  } else {
    writeLog("ERROR: XBAR did not connect PIT TRIG0 to PWM4.0 EXT_SYNC");
  }

  writeLog("Enabled XBAR");
}

void logPwmModuleStats(const char *moduleId, SubModule &pwmModule, bool hasChanB) {
  char strBuf[LOG_BUF_SIZE];
  uint32_t ret;

  uint32_t pwmFrequency = pwmModule.pwmFrequency();
  uint32_t pwmMode = pwmModule.pwmMode();
  uint16_t deadtimeSettingChanA = pwmModule.deadtimeSetting(ChanA);
  uint16_t dutyCycleSettingChanA = pwmModule.dutyCycleSetting(ChanA);
  const char *prescale = prescaleStr[pwmModule.prescaler()];

  if (hasChanB) {
    uint16_t deadtimeSettingChanB = pwmModule.deadtimeSetting(ChanB);
    uint16_t dutyCycleSettingChanB = pwmModule.dutyCycleSetting(ChanB);
    ret = snprintf(strBuf, sizeof(strBuf), "%s %luHz %s md:%lu dtA:%hu dcA:%hhu%% dtB:%hu dcB:%hhu%%", moduleId,
                   pwmFrequency, prescale, pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA, deadtimeSettingChanB,
                   dutyCycleSettingChanB);
  } else {
    ret = snprintf(strBuf, sizeof(strBuf), "%s %luHz %s md:%lu dtA:%hu dcA:%hhu%%", moduleId, pwmFrequency, prescale,
                   pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA);
  }

  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  if (config.Pwm.PrintRegs) {
    pwmModule.printRegs();
  }
}

void printStats() {
  logPwmModuleStats("1.3", Sm13, true);
  logPwmModuleStats("2.0", Sm20, true);
  logPwmModuleStats("2.1", Sm21, false);
  logPwmModuleStats("2.2", Sm22, true);
  logPwmModuleStats("2.3", Sm23, true);
  logPwmModuleStats("3.1", Sm31, true);
  logPwmModuleStats("4.0", Sm40, false);
  logPwmModuleStats("4.1", Sm41, false);
  logPwmModuleStats("4.2", Sm42, true);

  char strBuf[LOG_BUF_SIZE];
  if (const uint32_t ret = snprintf(strBuf, sizeof(strBuf), "IP: %d.%d.%d.%d",
                                    qindesign::network::Ethernet.localIP()[0],
                                    qindesign::network::Ethernet.localIP()[1],
                                    qindesign::network::Ethernet.localIP()[2],
                                    qindesign::network::Ethernet.localIP()[3]); ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }
}
