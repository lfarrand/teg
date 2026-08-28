#include "pwm_utils.h"
#include "spwm_math.h"
#include "pwm_timing.h"
#include "modulation.h"
#include "capture.h"
#include "acmp.h"
#include "pll.h"
#include "mppt.h"
#include "mqtt.h"
#include "thermal.h"
#include "mtp_service.h"
#include "power_monitor.h"
#include "waveform.h"
#include "waveform_parse.h"
#include "utils.h"
#include <QNEthernet.h>
#include "config_json.h"
#include "config_compare.h"

extern qindesign::network::EthernetServer server;
extern void kickWatchdog();

volatile uint32_t vPhase = 0;
volatile uint32_t vIsrCycles = 0;

// Starvation detector. The reload interrupt should arrive once per carrier period;
// a longer gap means carrier cycles went by unserved, which until now nothing
// reported. The ISR is where the modulation, capture, metering and current-limit
// polling all live, so a stall there is silent but not harmless - and the firmware
// has at least one known interrupt-masked region long enough to cause it (the
// OneWire temperature harvest masks for 65-70us, more than a full period at 20kHz).
//
// Cost on the common path is one subtract and one compare. The divide runs only
// once a miss has already been detected.
volatile uint32_t vMissedIsrCycles = 0;
static volatile uint32_t vIsrGapExpected = 0; // CPU cycles per carrier period
static volatile uint32_t vIsrGapLimit = 0;    // gap above which cycles are counted lost
static uint32_t isrLastEntry = 0;             // ISR-owned
static volatile bool vIsrGapPrimed = false;   // cleared on reconfigure: the baseline moved

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
static uint64_t vIndexAccumQ24 = 0; // ISR-owned; configured only with its IRQ off
static uint64_t vIndexStepQ24 = SoftStartInstant;
static volatile int32_t vDtCompQ15 = 0;

volatile bool vFaultTripped = false;
// Bumped on every mask. releaseOutputInhibit() refuses to commit OUTEN if this
// changed after the last quiet sample — a fault ISR in the gap must win.
static volatile uint32_t vFaultGeneration = 0;
static volatile bool vPwmConfigurationValid = true;
// True from reset until every requested register set has loaded and all protection
// sources have been armed and sampled. It is also asserted for every timing/topology
// transaction, fault and OTA operation.
static volatile bool vOutputInhibited = true;
static volatile bool vRestartInhibited = false;
static volatile bool vHardwareInhibited = false;
static volatile bool vProvisioningInhibited = true;

// Modulation cells map onto FlexPWM2 submodules in this order; cells=2 drives
// Sm20+Sm22 (the original SPWM pair), 3-4 add Sm21/Sm23.
static SubModule *const CellSm[MaxModulationCells] = {&Sm20, &Sm22, &Sm21, &Sm23};
static const uint8_t CellLdokMasks[MaxModulationCells + 1] = {0, 0b0001, 0b0101, 0b0111, 0b1111};

static CellPlan cellPlan[MaxModulationCells]; // written only with the SPWM IRQ disabled

// Cells whose configured pair mode could not be honoured. Their outputs are held at a
// driven-low level and the ISR must not write a duty to them - a duty written once
// from configureModule2() is not a latch, and the ISR would restore the modulated
// waveform on the very next carrier cycle. Bit k corresponds to CellSm[k].
static volatile uint8_t vDeniedCellMask = 0;

// Cells currently running inverted output polarity (OCTRL[POLA]/[POLB] = 1), which is
// how schemes 2, 5 and 4-POD/APOD realise a 180deg opposition. Tracked because MASK
// forces outputs to logic 0 BEFORE polarity is applied (RM 55.8.45.4, p.3191), so
// masking one of these drives its pins HIGH - the gate commanded ON by the mechanism
// meant to shut it down. Bit k corresponds to CellSm[k].
static volatile uint8_t vInvertedCellMask = 0;

// FlexPWM2 submodule index behind each modulation cell. CellSm is {Sm20, Sm22, Sm21,
// Sm23}, so the submodule numbers are not in cell order.
static const uint8_t CellSmIdx[MaxModulationCells] = {0, 2, 1, 3};

static bool faultStateDrivesLowForPolarity(uint16_t octrl, bool channelA,
                                           bool inverted) {
  const uint8_t faultState = static_cast<uint8_t>(
      (octrl >> (channelA ? 4 : 2)) & 0x03);
  return faultState == (inverted ? 1 : 0);
}

bool pwmFaultStatesSafeForConfiguredPolarity() {
  // PWM1 SM3 is fixed HighTrue.  PWM2 can be temporarily un-inverted while
  // global inhibit is asserted, so judge its fault states against the polarity
  // releaseOutputInhibit() will restore from vInvertedCellMask, not the transient
  // OCTRL POL bits visible during a CurrentLimit-only reconfiguration.
  const uint16_t sm13 = IMXRT_FLEXPWM1.SM[3].OCTRL;
  if ((sm13 & (FLEXPWM_SMOCTRL_POLA | FLEXPWM_SMOCTRL_POLB)) != 0 ||
      !faultStateDrivesLowForPolarity(sm13, true, false) ||
      !faultStateDrivesLowForPolarity(sm13, false, false)) {
    return false;
  }

  const uint8_t invertedMask = vInvertedCellMask;
  for (uint8_t k = 0; k < MaxModulationCells; ++k) {
    const bool inverted = (invertedMask & (1u << k)) != 0;
    const uint16_t octrl = IMXRT_FLEXPWM2.SM[CellSmIdx[k]].OCTRL;
    // A non-inverted cell is not touched by restoreCellPolarity(), so stale
    // POL bits there would survive release and invert the supposedly safe state.
    if ((!inverted &&
         (octrl & (FLEXPWM_SMOCTRL_POLA | FLEXPWM_SMOCTRL_POLB)) != 0) ||
        !faultStateDrivesLowForPolarity(octrl, true, inverted) ||
        !faultStateDrivesLowForPolarity(octrl, false, inverted)) {
      return false;
    }
  }
  return true;
}

// Direct OCTRL write: eFlex stages polarity into m_signal and only commits it inside
// updateSetting(), which is far too heavy for a fault path.
static inline void setCellPolarityInverted(uint8_t k, bool inverted) {
  constexpr uint16_t Pol = FLEXPWM_SMOCTRL_POLA | FLEXPWM_SMOCTRL_POLB;
  volatile uint16_t &octrl = IMXRT_FLEXPWM2.SM[CellSmIdx[k]].OCTRL;
  if (inverted) {
    octrl |= Pol;
  } else {
    octrl &= static_cast<uint16_t>(~Pol);
  }
}

// OUTEN is immediate and independent of MASK/polarity. Disconnecting the peripheral
// drivers first prevents the brief wrong-polarity edge that the old shutdown order
// could expose while changing OCTRL. External gate-driver inputs must still have
// hardware pull-downs; firmware cannot guarantee an un-driven PCB net.
FASTRUN static void disconnectAllOutputDrivers() {
  IMXRT_FLEXPWM1.OUTEN = 0;
  IMXRT_FLEXPWM2.OUTEN = 0;
  IMXRT_FLEXPWM3.OUTEN = 0;
  IMXRT_FLEXPWM4.OUTEN = 0;
  asm volatile("dsb");
}

static void connectConfiguredOutputDrivers() {
  IMXRT_FLEXPWM1.OUTEN = FLEXPWM_OUTEN_PWMA_EN(1u << 3) |
                         FLEXPWM_OUTEN_PWMB_EN(1u << 3);
  IMXRT_FLEXPWM2.OUTEN = FLEXPWM_OUTEN_PWMA_EN(0x0Fu) |
                         FLEXPWM_OUTEN_PWMB_EN((1u << 0) | (1u << 2) | (1u << 3));
  IMXRT_FLEXPWM3.OUTEN = FLEXPWM_OUTEN_PWMA_EN(1u << 1) |
                         FLEXPWM_OUTEN_PWMB_EN(1u << 1);
  IMXRT_FLEXPWM4.OUTEN = FLEXPWM_OUTEN_PWMA_EN((1u << 0) | (1u << 1) | (1u << 2)) |
                         FLEXPWM_OUTEN_PWMB_EN(1u << 2);
  asm volatile("dsb");
}

// Mask every output, safely for inverted cells. OUTEN disconnects the pads first,
// so the following immediate OCTRL polarity writes cannot create an observable
// wrong-level edge. External gate-driver inputs still require hardware pull-downs.
FASTRUN void maskAllOutputsSafely() {
  vOutputInhibited = true;
  vFaultGeneration++;
  disconnectAllOutputDrivers();

  uint8_t m = vInvertedCellMask;
  while (m != 0) {
    const uint8_t k = static_cast<uint8_t>(__builtin_ctz(m));
    m &= static_cast<uint8_t>(m - 1);
    setCellPolarityInverted(k, false);
  }
  asm volatile("dsb");

  Tm1.disable();
  Tm2.disable();
  Tm3.disable();
  Tm4.disable();
}

// Put back the polarity maskAllOutputsSafely() removed. Without this a cleared fault
// resumes with the carrier geometry silently wrong - the opposition those schemes
// exist to produce would simply be absent.
static void restoreCellPolarity() {
  uint8_t m = vInvertedCellMask;
  while (m != 0) {
    const uint8_t k = static_cast<uint8_t>(__builtin_ctz(m));
    m &= static_cast<uint8_t>(m - 1);
    setCellPolarityInverted(k, true);
  }
  asm volatile("dsb");
}

static bool protectionLiveOrLatched() {
  const bool gpioActive = config.FaultProtection.Enabled &&
      (digitalReadFast(config.FaultProtection.Pin) ==
       (config.FaultProtection.ActiveHigh ? HIGH : LOW));
  return !acmpProtectionReady() || gpioActive || acmpFaultPinActive() || acmpFaultLatched();
}

static bool releaseOutputInhibit() {
  const uint32_t genAtEntry = vFaultGeneration;
  if (!vPwmConfigurationValid || vFaultTripped || vRestartInhibited || vHardwareInhibited ||
      vProvisioningInhibited || protectionLiveOrLatched() ||
      !thermalAllowsPwmRelease() || !mtpAllowsPwmRelease()) {
    if (protectionLiveOrLatched()) {
      vFaultTripped = true;
      writeLogLevel(EventWarn,
                    "PWM release refused: protection unready, active or latched");
    } else if (!thermalAllowsPwmRelease()) {
      writeLogLevel(EventWarn, "PWM release refused: thermal protection has no valid sample");
    } else if (!mtpAllowsPwmRelease()) {
      writeLogLevel(EventWarn, "PWM release refused: MTP startup timer still armed");
    }
    disconnectAllOutputDrivers();
    return false;
  }

  // OUTEN stays disconnected while polarity and timers change. A fault ISR in
  // this window increments vFaultGeneration and leaves the pads cleared.
  restoreCellPolarity();
  Tm1.enable();
  Tm2.enable();
  Tm3.enable();
  Tm4.enable();
  asm volatile("dsb");

  // GPIO ISR cannot run while IRQs are off; ACMP hardware still gates PWM1/2.
  // Re-sample after the OUTEN write and remask if anything moved.
  noInterrupts();
  if (vFaultTripped || vFaultGeneration != genAtEntry || protectionLiveOrLatched() ||
      !thermalAllowsPwmRelease() || !mtpAllowsPwmRelease()) {
    interrupts();
    maskAllOutputsSafely();
    writeLogLevel(EventWarn, "PWM release aborted: fault during commit");
    return false;
  }
  connectConfiguredOutputDrivers();
  vOutputInhibited = false;
  interrupts();

  if (vFaultTripped || vFaultGeneration != genAtEntry || protectionLiveOrLatched()) {
    maskAllOutputsSafely();
    writeLogLevel(EventWarn, "PWM release aborted: fault after OUTEN");
    return false;
  }
  return true;
}

void setPwmRestartInhibit(bool inhibit) {
  vRestartInhibited = inhibit;
  if (inhibit) {
    maskAllOutputsSafely();
  }
}

void setPwmHardwareInhibit(bool inhibit) {
  vHardwareInhibited = inhibit;
  if (inhibit) {
    maskAllOutputsSafely();
  }
}

void setPwmProvisioningInhibit(bool inhibit) {
  vProvisioningInhibited = inhibit;
  if (inhibit) {
    maskAllOutputsSafely();
  }
}

bool pwmRestartInhibited() {
  return vRestartInhibited;
}

bool pwmHardwareInhibited() {
  return vHardwareInhibited;
}

bool pwmProvisioningInhibited() {
  return vProvisioningInhibited;
}

bool pwmOutputInhibited() {
  return vOutputInhibited;
}

bool pwmConfigurationValid() {
  return vPwmConfigurationValid;
}

bool pwmInterruptRequired() {
  return spwmActive() || captureActive() || acmpCbcEnabled();
}
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

// eFlex hides its PWM_Type pointer (both SubModule::ptr() and Timer::ptr() are
// protected), so the few register bits this file must own are reached through the
// Teensy core's map instead. Timer::index() is 0-3 for FlexPWM1-4.
static IMXRT_FLEXPWM_t *flexpwmBase(uint8_t timerIndex) {
  switch (timerIndex) {
    case 0: return &IMXRT_FLEXPWM1;
    case 1: return &IMXRT_FLEXPWM2;
    case 2: return &IMXRT_FLEXPWM3;
    default: return &IMXRT_FLEXPWM4;
  }
}

struct SubmoduleSettings {
  uint32_t frequency;
  uint32_t deadtimeNs;
  uint16_t dutyA;
  uint16_t dutyB; // UINT16_MAX if no ChanB
  bool hasChanB;
  uint8_t pairMode;      // what we will actually program
  uint8_t pairRequested; // what the operator configured, before any gate
  // Both, because "independent" and "a pair we are refusing to drive" need opposite
  // treatment: the first drives its configured duties, the second must hold its
  // outputs off. Collapsing them is what makes a denied leg conduct both switches.
  // Add phaseShift, etc., for special cases
};

static bool waitForLdok(IMXRT_FLEXPWM_t *base, uint8_t smMask,
                        uint32_t slowestFrequency) {
  const uint32_t periodUs = slowestFrequency == 0
      ? 100000U
      : (1000000U + slowestFrequency - 1U) / slowestFrequency;
  const uint32_t timeoutUs = periodUs > 120000U ? 250000U : (periodUs * 2U + 2000U);
  const uint32_t startUs = micros();
  while ((base->MCTRL & FLEXPWM_MCTRL_LDOK(smMask)) != 0) {
    if (static_cast<uint32_t>(micros() - startUs) > timeoutUs) {
      return false;
    }
    kickWatchdog();
  }
  return true;
}

static bool commitBufferedSettings(eFlex::Timer &timer, IMXRT_FLEXPWM_t *base,
                                   uint8_t smMask, uint32_t slowestFrequency) {
  timer.setPwmLdok(smMask, true);
  if (waitForLdok(base, smMask, slowestFrequency)) {
    return true;
  }
  timer.setPwmLdok(smMask, false);
  disconnectAllOutputDrivers();
  writeLogLevel(EventError, "PWM reload timeout: outputs remain inhibited");
  return false;
}

static bool setupSubmodule(SubModule &sm, const SubmoduleSettings &settings, bool printRegs) {
  constexpr uint32_t NanosecondsUnit = 1000000000;

  // Registers are touched directly rather than through SubModule::configure(), which
  // calls PWM_Init(): that rewrites SMCTRL/SMCTRL2 wholesale from a default-built
  // Config (turning on LDMOD), W1C-clears every fault flag on the module - releasing
  // the latched ACMP over-current disable acmp.cpp preserves - wipes DTSRCSEL
  // module-wide, fires an unrequested FORCE_OUT, and resets the staged output level.
  //
  // Both SubModule::ptr() and Timer::ptr() are protected, so the route is the core's
  // own register map, as acmp.cpp already does for the fault registers.
  IMXRT_FLEXPWM_t *base = flexpwmBase(sm.timer().index());
  const uint8_t smIdx = sm.index();

  // CTRL[COMPMODE] is write-once until reset (RM 55.8.5.3). With it set, "a PWMA
  // output that is high at the end of a period could go low at the start of the
  // next", which breaks the edge geometry a complementary pair relies on. The reset
  // value is 0, but refuse rather than assume - a bench experiment or a library
  // revision could have set it.
  const bool compModeClear = (base->SM[smIdx].CTRL & FLEXPWM_SMCTRL_COMPMODE) == 0;
  const bool complementary = pairIsComplementary(settings.pairMode) && compModeClear;
  if (pairIsComplementary(settings.pairMode) && !compModeClear) {
    writeLogLevel(EventError, "COMPMODE set: refusing complementary pair, staying independent");
  }

  sm.setPwmLdok(false); // clear any pending LDOK so the buffered register writes below take

  // Full-cycle reload, never immediate. eFlex's Config defaults reloadLogic to
  // kPWM_ReloadImmediate (LDMOD=1); with immediate load the ISR's LDOK write can
  // land mid-pulse, and if the new VAL3 is already below the counter the "equal to"
  // comparator never matches, so RM 55.8.5.3 says the output "will maintain this
  // state until a match with VAL3 clears the output in the following period" - a
  // high side latched on across periods. The Teensy core leaves LDMOD clear; assert
  // it so nothing silently changes that underneath us.
  base->SM[smIdx].CTRL = (base->SM[smIdx].CTRL & ~FLEXPWM_SMCTRL_LDMOD) | FLEXPWM_SMCTRL_FULL;

  // Dead time comes from the pure decision: floored for a half-bridge, forced to
  // zero for a differential pair, zero when independent (the hardware ignores it).
  // unit must be passed explicitly: setupDeadtime(value, ChanA) resolves to the
  // all-channels overload with unit=0 and silently divides by zero
  const uint32_t deadtimeNs = pairDeadTimeNs(settings.pairMode, settings.deadtimeNs);
  sm.setupDeadtime(deadtimeNs, NanosecondsUnit);

  // A leg that asked to be complementary but is not going to be must NOT fall back to
  // independent channels carrying their static configured duties. Channel B's default
  // is 32768, and both channels are centre-aligned on the same instant, so the two
  // pulses are concentric: the operator wired a half-bridge and both switches would be
  // commanded on together for up to half of every carrier period. That is the exact
  // defect #33 was rejected for, reintroduced through the fallback path.
  //
  // Zero both duties instead. With INDEP=1, duty 0 and HighTrue polarity, both pins
  // sit inactive. Masking would NOT be safe here: MASKA/MASKB force the output to
  // logic 0 *before* polarity (RM 55.8.45.4), so masking a cell that configureModule2
  // has set LowTrue would drive both its pins HIGH and hold them there.
  const bool pairDenied = pairIsComplementary(settings.pairRequested) && !complementary;
  const uint16_t dutyA = pairDenied ? 0 : settings.dutyA;
  const uint16_t dutyB = pairDenied ? 0 : settings.dutyB;
  if (pairDenied) {
    writeLogLevel(EventError,
                  "Pair mode refused for a submodule: outputs held off. A configured "
                  "half-bridge leg is NOT being driven - check the modulation scheme.");
  }

  sm.setupDutyCycle(ChanA, dutyA);
  if (settings.hasChanB && dutyB != UINT16_MAX) {
    sm.setupDutyCycle(ChanB, dutyB);
  }

  if (!sm.setPwmFrequency(settings.frequency, false, true)) {
    writeLogLevel(EventError, "Failed to set representable PWM frequency; outputs remain inhibited");
    return false;
  }

  // First call only: pin mux + counter start. No-op on reconfiguration.
  if (!sm.begin(true, false, false)) {
    writeLogLevel(EventError, "Failed to start PWM submodule; outputs remain inhibited");
    return false;
  }

  // Programs deadtime, polarity and duty with OUTEN deliberately clear. The desired
  // enable state is restored only in the software shadow; releaseOutputInhibit()
  // reconnects all pins together after a checked module-wide LDOK.
  sm.setupOutputEnable(false);
  // begin() skips this on reconfiguration, so it must be called explicitly every time.
  if (!sm.updateSetting(false)) {
    writeLogLevel(EventError, "Failed to stage PWM settings; outputs remain inhibited");
    return false;
  }
  sm.setupOutputEnable(true);

  // DTCNT1 belt-and-braces. PWM_SetupPwm writes DTCNT0 on the channel-A pass and
  // DTCNT1 on the channel-B pass, and the Teensy core's flexpwm_init() writes only
  // DTCNT0 - so DTCNT1 can still hold its 0x07FF reset value (~13.6us) on any
  // submodule whose B pass never ran. Asserting it here means a pair can never take
  // a garbage falling-edge dead time.
  if (complementary) {
    base->SM[smIdx].DTCNT1 = base->SM[smIdx].DTCNT0;
  }

  // INDEP last, and only when it actually needs to change. Clearing it enables
  // complementary generation AND the dead-time insertion logic together (RM
  // 55.3.2.7), so the gap must already be in DTCNT0/DTCNT1 before this line - the
  // reverse order would run a pair with whatever dead time happened to be there.
  // Guarded so a routine settings apply never touches the bit, which keeps the
  // transition to boot and to an explicit mode change.
  const bool isIndependent = (base->SM[smIdx].CTRL2 & FLEXPWM_SMCTRL2_INDEP) != 0;
  if (isIndependent == complementary) {
    if (complementary) {
      base->SM[smIdx].CTRL2 &= ~FLEXPWM_SMCTRL2_INDEP;
    } else {
      // LEAVING complementary mode: channel B stops being derived from A and starts
      // being generated from VAL4/VAL5. Those are buffered and do not take effect
      // until LDOK loads them at the next reload, so flipping INDEP first publishes
      // the PREVIOUS active VAL4/VAL5 on PWM_B for up to a full carrier period - a
      // stale duty on a pin that was, until this instant, a half-bridge low side.
      //
      // OUTEN is disconnected for the whole transaction, so publishing the old B
      // buffer briefly cannot reach a pin. The caller commits every affected
      // submodule with one LDOK and does not reconnect until that load is observed.
      base->SM[smIdx].CTRL2 |= FLEXPWM_SMCTRL2_INDEP;
    }
  }

  if (printRegs) {
    sm.printRegs();
  }
  return true;
}

void configurePwm() {
  vPwmConfigurationValid = true;
  maskAllOutputsSafely();
  configureModule1();
  configureModule2();
  configureModule3();
  configureModule4();
}

// Reconfigure only the timers whose settings actually changed, so an update to
// one submodule never disturbs the others' outputs or phase.
bool clearFaultTrip(bool operatorRequest) {
  if (vProvisioningInhibited) {
    writeLogLevel(EventWarn,
                  "PWM release refused: configuration/PIN has not been durably provisioned");
    return false;
  }
  if (vHardwareInhibited) {
    writeLogLevel(EventError, "PWM release refused: required PSRAM is unavailable");
    return false;
  }
  if (!vPwmConfigurationValid) {
    writeLogLevel(EventError, "PWM release refused: PWM configuration is invalid");
    return false;
  }
  if (vRestartInhibited && !operatorRequest) {
    return false;
  }
  // Applying unrelated settings is never an implicit acknowledgement of a trip.
  if (vFaultTripped && !operatorRequest) {
    return false;
  }
  // A latched hardware overcurrent only clears safely once the comparator is
  // quiet; a still-asserting pin means the fault condition persists (stuck
  // sensor, DC offset above threshold, real short) - refuse and stay tripped
  if (acmpArmedLatched() && acmpFaultPinActive()) {
    writeLogLevel(EventWarn, "Fault clear refused: current-limit comparator still above threshold");
    return false;
  }
  if (vFaultTripped) {
    acmpClearLatch(); // PWM1 first, IRQ-owning PWM2 last; IRQ stays off for now
    // A fault can reassert between the pre-check and the two W1C writes.  Do
    // not reconnect OUTEN unless both live inputs and both hardware latches
    // remained quiet through the complete clear transaction.
    asm volatile("dsb");
    if (acmpFaultPinActive() || acmpFaultLatched()) {
      vFaultTripped = true;
      writeLogLevel(EventWarn,
                    "Fault clear refused: current limit reasserted during latch clear");
      return false;
    }
  }
  if (operatorRequest) {
    vRestartInhibited = false;
  }
  vFaultTripped = false;
  writeLog("Protection acknowledged; attempting PWM output release");
  // Put back the polarity the mask removed, BEFORE unmasking. Skipping this would
  // resume with the 180deg opposition silently absent - the legs would run in phase.
  if (!releaseOutputInhibit()) {
    vFaultTripped = true;
    writeLogLevel(EventError, "Fault clear refused: PWM configuration is not valid");
    return false;
  }
  captureConfigure(); // unfreeze the flight recorder
  if (pwmInterruptRequired()) {
    attachModule2PwmInterruptVectors();
    enablePwmInterrupts();
  }
  // Last, so a comparator re-assert during this sequence re-trips cleanly
  // through the pending interrupt instead of racing the writes above
  acmpRearmFaultIrq();
  return true;
}

void applyPwmConfig(const MainConfig &previous) {
  const bool pwmChanged =
      !configValuesEqual(previous.Pwm, config.Pwm) ||
      !configValuesEqual(previous.AsymmetricInduction, config.AsymmetricInduction);
  const bool currentLimitChanged =
      !configValuesEqual(previous.CurrentLimit, config.CurrentLimit);
  const bool faultProtectionChanged =
      !configValuesEqual(previous.FaultProtection, config.FaultProtection);
  const bool protectionChanged = currentLimitChanged || faultProtectionChanged;
  if (pwmChanged || protectionChanged) {
    maskAllOutputsSafely();
    NVIC_DISABLE_IRQ(IRQ_FLEXPWM2_0);
  }

  if (faultProtectionChanged) {
    configureFaultProtection();
  }
  // Re-arms capture (and clears a freeze) on every apply - covers the
  // fault-clear path above. Skipped while a trip is still latched (refused
  // clear): the frozen ring is the trip's flight record, keep the evidence.
  if (!vFaultTripped) {
    captureConfigure();
  }
  thermalConfigure();
  if (pwmChanged) {
    // Treat timing/topology as one electrical transaction. This costs a short
    // output gap, but never exposes mixed prescalers, polarity or pair modes.
    configurePwm();
  }
  // Apply the fault map after all PWM setup writes.  Reapply even when the
  // current-limit JSON is unchanged: a simultaneous timer/topology update can
  // rewrite OCTRL/DISMAP/FCTRL, and protection must be verified against the
  // final register state before releaseOutputInhibit() reconnects the pads.
  if (currentLimitChanged || pwmChanged) {
    acmpConfigure();
  }
  // After the module reconfigures (buildSpwmLut resets the increment): the
  // PLL re-steers from its held estimate for a bumpless re-entry
  pllConfigure();
  if (!configValuesEqual(previous.Mppt, config.Mppt) ||
      !configValuesEqual(previous.Feedback, config.Feedback) ||
      !configValuesEqual(previous.Pwm.Tm2, config.Pwm.Tm2)) {
    // Reseed from the (freshly applied) index target - a Tm2 change rewrote
    // it via buildSpwmLut, and stale P&O state would yank the output back
    mpptConfigure();
  }
  if (!configValuesEqual(previous.Mqtt, config.Mqtt)) {
    mqttConfigure(); // reconnect with the new broker settings
  }
  if (!configValuesEqual(previous.PowerMon, config.PowerMon)) {
    powerMonitorConfigure(); // re-probe the INA226 with the new settings
  }
  // A refused clear leaves the trip latched, but the reconfigures above may
  // have re-enabled timers: re-assert the trip's masking so the applied
  // settings take effect only after an explicit successful clear
  if (vFaultTripped || vRestartInhibited || vHardwareInhibited ||
      vProvisioningInhibited || !vPwmConfigurationValid) {
    maskAllOutputsSafely(); // un-inverts polarity first; MASK is pre-polarity
    NVIC_DISABLE_IRQ(IRQ_FLEXPWM2_0);
  } else if (pwmChanged || protectionChanged) {
    clearFaultTrip(false);
  }
}

// The cell count actually used, as opposed to the one configured: clamped to the
// number of submodules, and overridden by the schemes that dictate their own leg
// count. Shared rather than duplicated, because configureModule2() has to ask the
// scheme gate about the SAME count the ISR runs - asking about a different one is how
// a leg ends up driven under a scheme that inverts it.
uint8_t effectiveModulationCells(const Module2Config &tm2) {
  uint8_t cells = tm2.ModulationCells;
  if (cells < 1) cells = 1;
  if (cells > MaxModulationCells) cells = MaxModulationCells;
  if (tm2.ModulationScheme == ModSchemeSvpwm || tm2.ModulationScheme == ModSchemeDpwm) {
    cells = 3; // three phase legs, always
  }
  if (tm2.ModulationScheme == ModSchemeSvpwm3D) {
    cells = 4; // three phase legs + the neutral leg (Sm23, pins 36/37)
  }
  return cells;
}

void buildSpwmLut() {
  const Module2Config &tm2 = config.Pwm.Tm2;

  uint8_t cells = effectiveModulationCells(tm2);

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

  // Starvation threshold, 1.5x the nominal period. Carrier dither shortens and
  // lengthens individual periods by up to MaxDitherPercent (30%), so the longest
  // dithered period is 1.3x nominal - comfortably under the threshold, and no
  // source of false positives. Under dither the missed-cycle ARITHMETIC is
  // approximate for the same reason; the count stays a starvation indicator rather
  // than an exact tally. Priming is cleared here because the baseline just moved.
  vIsrGapExpected = carrier ? (F_CPU_ACTUAL / carrier) : 0;
  vIsrGapLimit = vIsrGapExpected + (vIsrGapExpected >> 1);
  vIsrGapPrimed = false;
  vMissedIsrCycles = 0;
  // While PLL-locked, keep the accumulator: zeroing it would inject a phase
  // discontinuity into an output that is aligned to an external reference.
  // pllConfigure() (end of applyPwmConfig) re-steers the increment.
  if (!pllLocked()) {
    vPhase = 0;
  }

  // Amplitude: the ISR ramps the current index toward the target. The
  // current value persists across reconfigures, so soft-start ramps from
  // wherever the output already is, and from zero on a cold start.
  const uint32_t targetQ15 = indexMilliToQ15(indexMilli);
  vIndexStepQ24 = softStartStepQ24(targetQ15, tm2.SoftStartMs, carrier);
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

  // Apply it now, rather than waiting for someone to write the index.
  //
  // The cap lives in setModulationIndexTargetQ15(), so a tightening derate only ever
  // reached the output when some controller happened to call that setter. thermalTask
  // deliberately does not re-push the index while feedback or MPPT is enabled - it
  // would fight the tracker - and MPPT itself early-returns whenever the meter reading
  // is invalid or its interval has not elapsed. So with MPPT enabled and the meter
  // unhappy, the derate factor could climb with the temperature while the output sat
  // at its old, un-derated level indefinitely: the only over-temperature protection,
  // silently inert.
  //
  // Clamping DOWN only. This never raises a target, so it cannot fight a controller
  // upward, and the ISR's slew limit still turns it into a ramp rather than a step.
  const uint32_t capQ15 = (indexMilliToQ15(MaxModulationIndexMilli) * vThermalDerateMilli) / 1000U;
  if (vIndexTargetQ15 > capQ15) {
    vIndexTargetQ15 = capQ15;
  }
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

void modulationSetPhaseIncrement(uint32_t increment) {
  vPhaseIncrement = increment;
}

uint32_t modulationPhaseIncrement() {
  return vPhaseIncrement;
}

uint32_t modulationPhaseNow() {
  return vPhase;
}

bool modulationDdsDriven() {
  return !(vRefSequence || vSampleStep || vStreamPlayback);
}

void configureModule1() {
  SubmoduleSettings settings = {
    .frequency = config.Pwm.Tm1.Sm13.PwmFrequency,
    .deadtimeNs = config.Pwm.Tm1.Sm13.DeadTime,
    .dutyA = config.Pwm.Tm1.Sm13.ChannelA.DutyCycle,
    .dutyB = config.Pwm.Tm1.Sm13.ChannelB.DutyCycle,
    .hasChanB = true,
    .pairMode = config.Pwm.Tm1.Sm13.Pair,
    .pairRequested = config.Pwm.Tm1.Sm13.Pair
  };
  bool ok = setupSubmodule(Sm13, settings, config.Pwm.PrintRegs);
  if (ok) {
    ok = commitBufferedSettings(Tm1, &IMXRT_FLEXPWM1, 1u << 3, settings.frequency);
  }
  vPwmConfigurationValid = vPwmConfigurationValid && ok;
  writeLog(ok ? "Configured TM1" : "TM1 configuration failed; outputs inhibited");
}

void configureModule2() {
  // Precompute the reference table and cell plans if modulation is enabled
  if (spwmActive()) {
    buildSpwmLut();
  }

  // Carrier geometry: 180deg-shifted cells run with inverted output polarity
  // (the ISR complements their duty). Everything else is explicitly restored
  // to normal polarity so scheme changes never leave a leg inverted.
  // The scheme gate, applied here rather than in validateConfig. A complementary pair
  // cannot realise a cell whose plan inverts polarity, but that depends on the
  // modulation scheme, which changes - so rewriting the stored Pair would discard the
  // operator's intent permanently. Deciding it here keeps config truthful and lets
  // setupSubmodule() tell "independent" from "a pair being refused", which get
  // opposite treatment.
  //
  // effectiveModulationCells(), not tm2.ModulationCells: the gate must ask about the
  // SAME cell count the ISR runs, or a leg can be driven under a scheme that inverts it.
  const bool needsInversion =
      config.Pwm.Tm2.UseSpwm &&
      schemeRequiresPolarityInversion(config.Pwm.Tm2.ModulationScheme,
                                      config.Pwm.Tm2.CarrierDisposition,
                                      effectiveModulationCells(config.Pwm.Tm2));

  // Which cells asked to be a pair and are not going to be. Computed here because the
  // scheme gate lives here, and needed by the polarity loop below.
  static const uint8_t CellSmIndex[MaxModulationCells] = {PairSm20, PairSm22, PairSm21, PairSm23};
  const uint8_t cellPairCfg[MaxModulationCells] = {
      config.Pwm.Tm2.Sm20.Pair, config.Pwm.Tm2.Sm22.Pair,
      config.Pwm.Tm2.Sm21.Pair, config.Pwm.Tm2.Sm23.Pair};
  uint8_t deniedMask = 0;
  for (uint8_t k = 0; k < MaxModulationCells; k++) {
    const uint8_t eff = pairModeSanitisedForScheme(cellPairCfg[k], CellSmIndex[k], needsInversion);
    if (pairIsComplementary(cellPairCfg[k]) && !pairIsComplementary(eff)) {
      deniedMask |= static_cast<uint8_t>(1u << k);
    }
  }
  vDeniedCellMask = deniedMask;

  // Carrier geometry: a 180deg-opposed cell inverts its output polarity.
  //
  // A DENIED cell is forced HighTrue regardless of its plan. Its duties are zeroed in
  // setupSubmodule(), and duty 0 makes VAL2 == VAL3, which the hardware resolves to
  // logic 0 (RM 55.3.2.4, p.3108: "if both the set and reset of the flip-flop are
  // asserted, then the flop output goes to 0"). Logic 0 through POLA=1 would be a HIGH
  // pin (RM 55.8.18.3, p.3157) - so leaving an inverted cell inverted while zeroing its
  // duty drives BOTH pins of the leg permanently high, which is worse than the overlap
  // it was meant to prevent. HighTrue + duty 0 is a driven LOW pin, which is the state
  // a gate driver needs. Clearing OUTEN instead would leave the pin undriven.
  uint8_t invertedMask = 0;
  for (uint8_t k = 0; k < MaxModulationCells; k++) {
    const bool denied = (deniedMask & (1u << k)) != 0;
    const bool inverted =
        !denied && spwmActive() && k < vModCells && cellPlan[k].polarityInverted;
    CellSm[k]->setupLevel(inverted ? kPWM_LowTrue : kPWM_HighTrue);

    // The fault state must be the level that leaves the PIN inactive, which depends
    // on that cell's polarity. OCTRL[PWMAFS]/[PWMBFS] select what the output is forced
    // to during a fault, and RM 55.8.18.3 p.3157 specifies it as applied "prior to
    // consideration of output polarity control" - so on a LowTrue cell the default
    // state 0 is driven HIGH at the pin, i.e. the gate is commanded ON by the fault
    // response. eFlex defaults every channel to kPWM_PwmFaultState0 and nothing in
    // src/ ever changed it.
    //
    // State 1 is logic 1 pre-polarity, which an inverted cell renders as a low pin.
    const pwm_fault_state_t fs = inverted ? kPWM_PwmFaultState1 : kPWM_PwmFaultState0;
    CellSm[k]->setupFaultState(ChanA, fs);
    CellSm[k]->setupFaultState(ChanB, fs);

    // Recorded so the mask paths can un-invert before forcing outputs off.
    if (inverted) {
      invertedMask |= static_cast<uint8_t>(1u << k);
    }
  }
  vInvertedCellMask = invertedMask;

  // Configure Sm20 (Channels A and B)
  SubmoduleSettings sm20Settings = {
    .frequency = config.Pwm.Tm2.Sm20.PwmFrequency,
    .deadtimeNs = config.Pwm.Tm2.Sm20.DeadTime,
    .dutyA = config.Pwm.Tm2.Sm20.ChannelA.DutyCycle,
    .dutyB = config.Pwm.Tm2.Sm20.ChannelB.DutyCycle,
    .hasChanB = true,
    .pairMode = pairModeSanitisedForScheme(config.Pwm.Tm2.Sm20.Pair, PairSm20, needsInversion),
    .pairRequested = config.Pwm.Tm2.Sm20.Pair
  };
  bool ok = setupSubmodule(Sm20, sm20Settings, config.Pwm.PrintRegs);

  // Configure Sm21 (Channel A only)
  SubmoduleSettings sm21Settings = {
    .frequency = config.Pwm.Tm2.Sm21.PwmFrequency,
    .deadtimeNs = config.Pwm.Tm2.Sm21.DeadTime,
    .dutyA = config.Pwm.Tm2.Sm21.ChannelA.DutyCycle,
    .dutyB = UINT16_MAX, // No Channel B
    .hasChanB = false,
    .pairMode = pairModeSanitisedForScheme(config.Pwm.Tm2.Sm21.Pair, PairSm21, needsInversion),
    .pairRequested = config.Pwm.Tm2.Sm21.Pair // validation forces Independent: no B pin
  };
  ok = setupSubmodule(Sm21, sm21Settings, config.Pwm.PrintRegs) && ok;

  // Configure Sm22 (Channels A and B)
  SubmoduleSettings sm22Settings = {
    .frequency = config.Pwm.Tm2.Sm22.PwmFrequency,
    .deadtimeNs = config.Pwm.Tm2.Sm22.DeadTime,
    .dutyA = config.Pwm.Tm2.Sm22.ChannelA.DutyCycle,
    .dutyB = config.Pwm.Tm2.Sm22.ChannelB.DutyCycle,
    .hasChanB = true,
    .pairMode = pairModeSanitisedForScheme(config.Pwm.Tm2.Sm22.Pair, PairSm22, needsInversion),
    .pairRequested = config.Pwm.Tm2.Sm22.Pair
  };
  ok = setupSubmodule(Sm22, sm22Settings, config.Pwm.PrintRegs) && ok;

  // Configure Sm23 (Channels A and B)
  SubmoduleSettings sm23Settings = {
    .frequency = config.Pwm.Tm2.Sm23.PwmFrequency,
    .deadtimeNs = config.Pwm.Tm2.Sm23.DeadTime,
    .dutyA = config.Pwm.Tm2.Sm23.ChannelA.DutyCycle,
    .dutyB = config.Pwm.Tm2.Sm23.ChannelB.DutyCycle,
    .hasChanB = true,
    .pairMode = pairModeSanitisedForScheme(config.Pwm.Tm2.Sm23.Pair, PairSm23, needsInversion),
    .pairRequested = config.Pwm.Tm2.Sm23.Pair
  };
  ok = setupSubmodule(Sm23, sm23Settings, config.Pwm.PrintRegs) && ok;

  if (ok) {
    ok = commitBufferedSettings(Tm2, &IMXRT_FLEXPWM2, 0x0F,
                                config.Pwm.Tm2.SpwmCarrierFrequency);
  }
  vPwmConfigurationValid = vPwmConfigurationValid && ok;

  // Sampling/CBC can use this same reload IRQ in fixed-duty mode. Keep its
  // starvation baseline valid even when buildSpwmLut() was not called.
  const uint32_t carrier = config.Pwm.Tm2.SpwmCarrierFrequency;
  vIsrGapExpected = carrier ? (F_CPU_ACTUAL / carrier) : 0;
  vIsrGapLimit = vIsrGapExpected + (vIsrGapExpected >> 1);
  vIsrGapPrimed = false;

  // Dither tables need the final prescaler, so build them after submodule setup
  if (ok) {
    buildDitherState();
  } else {
    vDitherMode = DitherOff;
  }

  writeLog(ok ? "Configured TM2" : "TM2 configuration failed; outputs inhibited");
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
    .hasChanB = true,
    .pairMode = config.Pwm.Tm3.Sm31.Pair,
    .pairRequested = config.Pwm.Tm3.Sm31.Pair
  };
  bool ok = setupSubmodule(Sm31, settings, config.Pwm.PrintRegs);
  if (ok) {
    ok = commitBufferedSettings(Tm3, &IMXRT_FLEXPWM3, 1u << 1, settings.frequency);
  }
  vPwmConfigurationValid = vPwmConfigurationValid && ok;
  writeLog(ok ? "Configured TM3" : "TM3 configuration failed; outputs inhibited");
}

void configureModule4() {
  // Configure Sm40 (Channel A only)
  SubmoduleSettings sm40Settings = {
    .frequency = config.Pwm.Tm4.Sm40.PwmFrequency,
    .deadtimeNs = config.Pwm.Tm4.Sm40.DeadTime,
    .dutyA = config.Pwm.Tm4.Sm40.ChannelA.DutyCycle,
    .dutyB = UINT16_MAX, // No Channel B
    .hasChanB = false,
    .pairMode = config.Pwm.Tm4.Sm40.Pair, // validation forces Independent: no B pin
    .pairRequested = config.Pwm.Tm4.Sm40.Pair
  };
  bool ok = setupSubmodule(Sm40, sm40Settings, config.Pwm.PrintRegs);

  // Configure Sm41 (Channel A only, with phase shift handling)
  SubmoduleSettings sm41Settings = {
    .frequency = config.Pwm.Tm4.Sm41.PwmFrequency,
    .deadtimeNs = config.Pwm.Tm4.Sm41.DeadTime,
    .dutyA = config.Pwm.Tm4.Sm41.ChannelA.DutyCycle,
    .dutyB = UINT16_MAX, // No Channel B
    .hasChanB = false,
    .pairMode = config.Pwm.Tm4.Sm41.Pair, // validation forces Independent: no B pin
    .pairRequested = config.Pwm.Tm4.Sm41.Pair
  };
  ok = setupSubmodule(Sm41, sm41Settings, config.Pwm.PrintRegs) && ok;

  // Configure Sm42 (Channels A and B, with phase shift and Asymmetric Induction)
  if (config.AsymmetricInduction.IsEnabled) {
    // Asymmetric Induction mode: Custom timing for Sm42
    const AsymmetricTimings t = computeAsymmetricTimings(
      F_BUS_ACTUAL, config.Pwm.Tm4.Sm42.PwmFrequency, config.Pwm.Tm4.Sm42.ChannelA.DutyCycle,
      config.AsymmetricInduction.PreShiftNanos, config.AsymmetricInduction.PostShiftNanos,
      MAX_COUNTER_VALUE);

    if (!t.valid) {
      writeLogLevel(EventError, "Asymmetric edge lies outside the PWM period; outputs inhibited");
      ok = false;
    }

    Sm42.setPwmLdok(false);
    Sm42.setPrescaler(static_cast<pwm_clock_prescale_t>(t.prescalerIndex));
    Sm42.setupDeadtime(config.Pwm.Tm4.Sm42.DeadTime, 1000000000);

    if (ok && !Sm42.setPwmFrequency(config.Pwm.Tm4.Sm42.PwmFrequency, false, true)) {
      writeLogLevel(EventError, "Failed to set SM42 PWM frequency");
      ok = false;
    }

    // First call only: pin mux + counter start. No-op on reconfiguration.
    if (ok && !Sm42.begin(true, false, false)) {
      writeLogLevel(EventError, "Failed to start SM42");
      ok = false;
    }

    // Programs deadtime/output-enable/polarity plus standard VALx values...
    Sm42.setupOutputEnable(false);
    if (ok && !Sm42.updateSetting(false)) {
      writeLogLevel(EventError, "Failed to stage SM42 settings");
      ok = false;
    }
    Sm42.setupOutputEnable(true);

    // ...which the custom asymmetric edge timings then overwrite
    if (ok) {
      Sm42.setInitValue(t.periodStart);
      Sm42.setVal0Value(t.periodStart);
      Sm42.setVal1Value(t.periodEnd);
      Sm42.setVal2Value(t.startChanA);
      Sm42.setVal3Value(t.stopChanA);
      Sm42.setVal4Value(t.startChanB);
      Sm42.setVal5Value(t.stopChanB);
    }
  } else {
    // Standard mode for Sm42
    SubmoduleSettings sm42Settings = {
      .frequency = config.Pwm.Tm4.Sm42.PwmFrequency,
      .deadtimeNs = config.Pwm.Tm4.Sm42.DeadTime,
      .dutyA = config.Pwm.Tm4.Sm42.ChannelA.DutyCycle,
      .dutyB = config.Pwm.Tm4.Sm42.ChannelB.DutyCycle,
      .hasChanB = true,
      // Asymmetric induction mode drives A and B from independent VAL2..VAL5
      // start/stop values, which complementary generation would overwrite.
      // Validation forces Independent for Sm42 regardless of what is configured.
      .pairMode = config.Pwm.Tm4.Sm42.Pair,
    .pairRequested = config.Pwm.Tm4.Sm42.Pair
    };
    ok = setupSubmodule(Sm42, sm42Settings, config.Pwm.PrintRegs) && ok;
  }

  if (ok) {
    uint32_t slowest = config.Pwm.Tm4.Sm40.PwmFrequency;
    if (config.Pwm.Tm4.Sm41.PwmFrequency < slowest) slowest = config.Pwm.Tm4.Sm41.PwmFrequency;
    if (config.Pwm.Tm4.Sm42.PwmFrequency < slowest) slowest = config.Pwm.Tm4.Sm42.PwmFrequency;
    ok = commitBufferedSettings(Tm4, &IMXRT_FLEXPWM4, 0x07, slowest);
  }
  vPwmConfigurationValid = vPwmConfigurationValid && ok;

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

  writeLog(ok ? "Configured TM4" : "TM4 configuration failed; outputs inhibited");
}

uint8_t calculateBestPrescaler(uint32_t pwmFrequency) {
  return bestPrescalerIndex(F_BUS_ACTUAL, pwmFrequency, MAX_COUNTER_VALUE);
}

void attachInterruptVectors() {
  Serial.println(F("Attaching PWM interrupt vectors"));

  if (pwmInterruptRequired()) {
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

  // Count carrier cycles that went by unserved. Unsigned subtraction handles the
  // DWT counter's ~7.2s wrap correctly, since the gap is microseconds.
  if (vIsrGapPrimed) {
    const uint32_t gap = t0 - isrLastEntry;
    if (gap > vIsrGapLimit) {
      vMissedIsrCycles += (gap / vIsrGapExpected) - 1;
    }
  }
  isrLastEntry = t0;
  vIsrGapPrimed = vIsrGapLimit != 0;

  digitalToggleFast(TriggerPin);

  // Fixed-duty PWM still needs a carrier-synchronous interrupt when capture,
  // dual-channel metering, scope triggering or CBC telemetry is active. Do no
  // duty/phase work in that case; sample and clear the source only.
  if (!spwmActive()) {
    Sm20.clearStatusFlags(kPWM_CompareVal1Flag);
    captureTick();
    acmpCbcTick();
    vIsrCycles = ARM_DWT_CYCCNT - t0;
    asm volatile("dsb");
    return;
  }

  const uint32_t phase = vPhase;

  // Slew-limited amplitude: soft-start ramp and closed-loop target tracking
  const uint64_t accum = rampIndexQ24(vIndexAccumQ24, vIndexTargetQ15, vIndexStepQ24);
  vIndexAccumQ24 = accum;
  const uint32_t idx = static_cast<uint32_t>(accum >> SoftStartFractionBits);
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

  if (!stepped) {
    // LUT-driven path: the whole per-cycle pipeline lives in modulation.h
    // (modulationCycleDuties) so the native spectral tests exercise it as-is
    uint16_t duties[MaxModulationCells];
    ModCycleConfig mc;
    mc.scheme = scheme;
    mc.cells = cells;
    mc.dpwmVariant = vDpwmVariant;
    mc.dpwmClampPhase = vDpwmClampPhase;
    mc.nearestLevel = vNearestLevel;
    mc.dtCompQ15 = comp;
    modulationCycleDuties(spwmLut, phase, idx, mc, cellPlan, duties);
    const uint8_t n = modulationCellCount(scheme, cells);
    const uint8_t denied = vDeniedCellMask;
    for (uint8_t k = 0; k < n; k++) {
      // Skip a cell whose pair mode was refused. Its duty was zeroed at configure
      // time to hold the leg off, and writing a modulated duty here would restore the
      // waveform on the next carrier cycle - the protection is only as good as the
      // ISR's willingness to leave it alone.
      if ((denied & (1u << k)) == 0) {
        CellSm[k]->updateDutyCycle(duties[k]);
      }
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
    const uint16_t ref = refToDuty(s, idx, scheme == ModSchemeLevelShifted ? 0 : comp);
    const bool nlm = vNearestLevel;
    const uint8_t deniedStepped = vDeniedCellMask;
    for (uint8_t k = 0; k < cells; k++) {
      if ((deniedStepped & (1u << k)) != 0) {
        continue; // refused pair: leave the zeroed duty alone (see the LUT path above)
      }
      const uint16_t duty =
        modulationFinalDuty(modulationCellDuty(scheme, ref, k, cells, nlm), cellPlan[k]);
      CellSm[k]->updateDutyCycle(duty);
    }
  }

  Sm20.clearStatusFlags(kPWM_CompareVal1Flag);

  Tm2.setPwmLdok(mask, true);

  captureTick(); // reload point = average-current instant for centre-aligned PWM

  acmpCbcTick(); // count cycle-by-cycle current-limit trips (FFLAG0 poll)

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
  const uint32_t loopHz = config.Feedback.LoopHz > 0 ? config.Feedback.LoopHz : 250;
  const uint32_t periodUs = 1000000UL / loopHz;
  if (sinceLastRun < periodUs) {
    return;
  }
  const float dt = static_cast<float>(static_cast<uint32_t>(sinceLastRun)) / 1000000.0f;
  sinceLastRun = 0;

  // Prefer the PWM-synchronous samples (taken at the reload point) when capture is
  // running; fall back to a plain analogRead otherwise. BOTH are 12-bit: capture
  // programs the ADC to 12 bits and setup() sets the core's read resolution to match,
  // so the same scale applies either way. The fallback used to divide by 1023 while
  // reading 12-bit data - four times high, which drove the regulator down until the
  // real output sat at a quarter of the setpoint, every time the capture ring was
  // frozen by a fault or an armed scope trigger.
  float measuredMv;
  const float mvPerCount =
      static_cast<float>(config.Feedback.FullScaleMillivolts) / static_cast<float>(AdcCountFullScale);
  if (captureActive() && !captureIsFrozen()) {
    measuredMv = static_cast<float>(captureMeanRaw(64)) * mvPerCount;
  } else {
    measuredMv = static_cast<float>(analogRead(config.Feedback.AnalogPin)) * mvPerCount;
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
// interrupt (~1us pin-to-off). Latched until an authenticated explicit clear.
// A true hardware fault path (XBAR -> FlexPWM FAULT0, <10ns, zero software)
// can reuse the same pin; see docs/RT1170_PSPWM.md for the bring-up notes.
// ---------------------------------------------------------------------------
FASTRUN static void faultTripIsr() {
  // Mask all outputs on all four timers (MASK register + local FORCE)
  maskAllOutputsSafely(); // un-inverts polarity first; MASK is pre-polarity
  NVIC_DISABLE_IRQ(IRQ_FLEXPWM2_0);
  vFaultTripped = true;
  asm volatile("dsb");
}

// OTA safe state: mask every output and silence the modulation interrupt at
// the source, exactly like the fault trip, but with no clear path - flash
// operations follow, and only the post-update reboot re-enters the verified
// setup() bring-up. Idempotent; safe to call while fault-tripped (flashing
// a fix for a faulting build is a primary use case).
void enterOtaSafeState() {
  Sm20.disableInterrupts(kPWM_CompareVal1InterruptEnable);
  NVIC_DISABLE_IRQ(IRQ_FLEXPWM2_0);
  maskAllOutputsSafely(); // un-inverts polarity first; MASK is pre-polarity
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

  // Edge IRQs do not report a level that was already active before attach.
  // Sample after the pull and vector are established; the same ISR-safe path
  // handles both boot-active and transition-active faults.
  const bool activeNow = digitalReadFast(pin) ==
                         (config.FaultProtection.ActiveHigh ? HIGH : LOW);
  if (activeNow) {
    faultTripIsr();
  }

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
