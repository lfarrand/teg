#include "pwm_utils.h"
#include "utils.h"
#include <QNEthernet.h>
#include "config_json.h"

extern qindesign::network::EthernetServer server;

volatile uint32_t vPhase = 0;
volatile uint32_t vIsrCycles = 0;

// SPWM sine generation is a fixed-point DDS: a 32-bit phase accumulator advances
// by vPhaseIncrement every carrier cycle and wraps naturally on overflow, so any
// modulation frequency is exact (resolution carrier/2^32 Hz) with no drift and
// no glitch at the wrap. The top SpwmLutBits of the phase index a sine table of
// channel-A duty values; the next 8 bits linearly interpolate between entries,
// keeping the ISR integer-only (no FPU context stacking on exception entry).
// Table lives in DTCM (zero-wait-state). Channel B duty is derived as 65536 - dutyA.
constexpr uint32_t SpwmLutBits = 11;
constexpr uint32_t SpwmLutSize = 1U << SpwmLutBits; // 2048 entries, 4 KB
static uint16_t spwmLut[SpwmLutSize];
static volatile uint32_t vPhaseIncrement = 0;

// Submodules 0 and 2 of FlexPWM2 (Sm20/Sm22) — the only LDOK bits the SPWM ISR touches
constexpr uint8_t Sm20Sm22Mask = (1U << 0) | (1U << 2);

const uint8_t PrescalerValues[] = {1, 2, 4, 8, 16, 32, 64, 128};

const char *prescaleStr[] = {
  "fclk/1", "fclk/2", "fclk/4", "fclk/8", "fclk/16", "fclk/32", "fclk/64", "fclk/128"
};

TeensyTimerTool::OneShotTimer startupTimer(TeensyTimerTool::TCK64);
TeensyTimerTool::OneShotTimer chargeToggleTimer(TeensyTimerTool::GPT1);
TeensyTimerTool::OneShotTimer dischargeToggleTimer(TeensyTimerTool::GPT2);
TeensyTimerTool::PeriodicTimer pwmSyncTimer(TeensyTimerTool::PIT);

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

void configureTimers() {
}

void startupTimerCallback() {
}

void chargeToggleTimerCallback() {
}

void dischargeToggleTimerCallback() {
}

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
void applyPwmConfig(const MainConfig &previous) {
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
  // The table is one pure sine cycle; its contents don't depend on the config,
  // so it only needs to be generated once.
  static bool lutBuilt = false;
  if (!lutBuilt) {
    constexpr float32_t TwoPi = 6.28318530717958648f;
    const float32_t phaseStep = TwoPi / static_cast<float32_t>(SpwmLutSize);
    for (uint32_t i = 0; i < SpwmLutSize; i++) {
      const float32_t s = roundf((MidDutyCycle - 1) * arm_sin_f32(phaseStep * static_cast<float32_t>(i)));
      spwmLut[i] = static_cast<uint16_t>(MidDutyCycle + s);
    }
    lutBuilt = true;
  }

  // DDS tuning word: the modulation phase advances by mod/carrier of a full
  // cycle (2^32) per carrier period, so the output frequency is exact to
  // carrier/2^32 Hz for ANY carrier/modulation ratio.
  const uint32_t carrier = config.Pwm.Tm2.SpwmCarrierFrequency;
  const uint32_t modulation = config.Pwm.Tm2.SpwmModulationFrequency;
  vPhaseIncrement = carrier > 0
                      ? static_cast<uint32_t>((static_cast<uint64_t>(modulation) << 32) / carrier)
                      : 0;
  vPhase = 0;

  const uint64_t actualMilliHz = (static_cast<uint64_t>(vPhaseIncrement) * carrier * 1000ULL) >> 32;
  char strBuf[LOG_BUF_SIZE];
  snprintf(strBuf, sizeof(strBuf), "SPWM DDS: %lu.%03luHz modulation on %luHz carrier",
           static_cast<uint32_t>(actualMilliHz / 1000), static_cast<uint32_t>(actualMilliHz % 1000), carrier);
  writeLog(strBuf);
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
  // Precompute the sine table if SPWM is enabled
  if (config.Pwm.Tm2.UseSpwm) {
    buildSpwmLut();
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

  writeLog("Started TM2");
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
    .dutyA = config.Pwm.Tm4.Sm41.ChannelA.PhaseShift != 0 ? FIFTY_PERCENT_DUTY : config.Pwm.Tm4.Sm41.ChannelA.DutyCycle,
    .dutyB = UINT16_MAX, // No Channel B
    .hasChanB = false
  };
  setupSubmodule(Sm41, sm41Settings, config.Pwm.PrintRegs);

  // Configure Sm42 (Channels A and B, with phase shift and Asymmetric Induction)
  if (config.AsymmetricInduction.IsEnabled) {
    // Asymmetric Induction mode: Custom timing for Sm42
    const uint8_t prescalerIndex = calculateBestPrescaler(config.Pwm.Tm4.Sm42.PwmFrequency);
    const float32_t dutyCycleA = static_cast<float32_t>(config.Pwm.Tm4.Sm42.ChannelA.DutyCycle) / 65536.0f;
    constexpr int32_t nanosecondsPerSecond = 1000000000;
    constexpr float32_t nanosecondsPerSecondFloat = static_cast<float32_t>(nanosecondsPerSecond);
    const float32_t clockTicksPerNanosecond = F_BUS_ACTUAL / nanosecondsPerSecondFloat;
    const uint32_t periodTicks = static_cast<uint32_t>(roundf(
      F_BUS_ACTUAL / static_cast<float32_t>(config.Pwm.Tm4.Sm42.PwmFrequency)));
    const int16_t pulseTicksA = static_cast<int16_t>(roundf(periodTicks * dutyCycleA));
    const int16_t preShiftTicks = static_cast<int16_t>(roundf(
      clockTicksPerNanosecond * config.AsymmetricInduction.PreShiftNanos));
    const int16_t postShiftTicks = static_cast<int16_t>(roundf(
      clockTicksPerNanosecond * config.AsymmetricInduction.PostShiftNanos));
    constexpr int16_t periodStart = 0;
    const int16_t periodEnd = periodTicks - 1;
    const int16_t startChanA = periodStart;
    const int16_t stopChanA = startChanA + pulseTicksA;
    const int16_t startChanB = stopChanA - preShiftTicks;
    const int16_t stopChanB = periodEnd - postShiftTicks;

    Sm42.disableOutput(ChanA);
    Sm42.disableOutput(ChanB);

    Sm42.setPwmLdok(false);
    Sm42.setPrescaler(static_cast<pwm_clock_prescale_t>(prescalerIndex));
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
    Sm42.setInitValue(periodStart);
    Sm42.setVal0Value(periodStart);
    Sm42.setVal1Value(periodEnd);
    Sm42.setVal2Value(startChanA);
    Sm42.setVal3Value(stopChanA);
    Sm42.setVal4Value(startChanB);
    Sm42.setVal5Value(stopChanB);

    Sm42.setPwmLdok(true);
    Tm4.enable();
    Sm42.enableOutput(ChanA);
    Sm42.enableOutput(ChanB);
  } else {
    // Standard mode for Sm42
    SubmoduleSettings sm42Settings = {
      .frequency = config.Pwm.Tm4.Sm42.PwmFrequency,
      .deadtimeNs = config.Pwm.Tm4.Sm42.DeadTime,
      .dutyA = config.Pwm.Tm4.Sm42.ChannelA.PhaseShift != 0
                 ? FIFTY_PERCENT_DUTY
                 : config.Pwm.Tm4.Sm42.ChannelA.DutyCycle,
      .dutyB = config.Pwm.Tm4.Sm42.ChannelB.PhaseShift != 0
                 ? FIFTY_PERCENT_DUTY
                 : config.Pwm.Tm4.Sm42.ChannelB.DutyCycle,
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
  const size_t numPrescalers = sizeof(PrescalerValues) / sizeof(PrescalerValues[0]);

  for (uint8_t i = 0; i < numPrescalers; i++) {
    uint32_t effectiveClock = F_BUS_ACTUAL / PrescalerValues[i];
    uint32_t periodTicks = effectiveClock / pwmFrequency;

    if (periodTicks <= MAX_COUNTER_VALUE) {
      return i;
    }
  }

  return numPrescalers - 1;
}

void attachInterruptVectors() {
  Serial.println(F("Attaching PWM interrupt vectors"));

  if (config.Pwm.Tm2.UseSpwm) {
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
  vPhase = phase + vPhaseIncrement; // wraps on overflow = seamless cycle boundary

  // Top bits index the sine table, the next 8 bits interpolate linearly between
  // adjacent entries (max slope is ~100 counts/entry, so the maths stays small).
  const uint32_t idx = phase >> (32 - SpwmLutBits);
  const int32_t frac = (phase >> (32 - SpwmLutBits - 8)) & 0xFF;
  const int32_t a = spwmLut[idx];
  const int32_t b = spwmLut[(idx + 1) & (SpwmLutSize - 1)];
  const uint16_t dutyA = static_cast<uint16_t>(a + (((b - a) * frac) >> 8));

  Tm2.setPwmLdok(Sm20Sm22Mask, false);

  Sm20.updateDutyCycle(dutyA);
  Sm22.updateDutyCycle(static_cast<uint16_t>(0U - dutyA)); // 65536 - dutyA == MidDutyCycle - s

  Sm20.clearStatusFlags(kPWM_CompareVal1Flag);

  Tm2.setPwmLdok(Sm20Sm22Mask, true);

  vIsrCycles = ARM_DWT_CYCCNT - t0;

  asm volatile("dsb");
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
