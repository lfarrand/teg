// Hardware overcurrent protection (ACMP fault + cycle-by-cycle limit).
//
// Signal path: current-sense pin -> CMPn plus input; CMPn minus input = its
// internal 6-bit DAC programmed to the threshold; CMPn output -> one XBARA1
// input (26+n-1), fanned out to two independent private FAULT0 selectors:
//
//   XBARA1 output 35 -> FlexPWM1 FAULT0 -> SM3 PWM_A/B (Teensy pins 8/7)
//   XBARA1 output 49 -> FlexPWM2 FAULT0 -> SM0-3 PWM_A/B (modulation cells)
//
// The trip itself is combinational: no software is in the disable path.  PWM2
// alone owns the latched-mode interrupt because both selectors carry the same
// comparator source; its ISR masks every timer after the hardware has already
// disabled both protected groups.  PWM1's unrelated submodules and fault1-3
// register fields are preserved.  This implementation exclusively owns the
// module-shared PWM2 fault vector; no other code in this repository enables
// FIE1-3, and adding such a user requires an explicit dispatcher/ownership gate.
//
// Two modes (FlexPWM-side only; comparator configuration is identical):
//  - Latched (FAUTO0=0, FSAFE0=1): outputs stay off until both fault flags are
//    cleared and the comparator is quiet at a full-cycle boundary.  PWM2's
//    fault IRQ mirrors the software GPIO trip into vFaultTripped.
//  - Cycle-by-cycle (FAUTO0=1): both modules chop immediately and recover at
//    their next full/half-cycle boundary while the comparator is quiet.  The
//    PWM2 carrier ISR samples both flags and counts at most one event per poll;
//    it is telemetry, not an exact PWM1-cycle counter if their rates differ.
//
// BENCH-VERIFY before trusting: absolute DAC threshold, comparator-to-both-PWM
// latency, pins 8/7 faulting low, both cycle-boundary recovery paths, and the
// two-module clear/re-trip race.  External gate-driver input pull-downs remain
// mandatory because the latched ISR subsequently disconnects OUTEN.

#include "acmp.h"
#include "acmp_math.h"
#include "config_json.h"
#include "pwm_utils.h" // vFaultTripped, maskAllOutputsSafely
#include "utils.h"
#include <Arduino.h>

extern MainConfig config;

static_assert(AcmpPwmTargetCount == 2, "ACMP must protect both PWM modules");
static_assert(AcmpPwmTargets[0].xbarFault0Output == XBARA1_OUT_FLEXPWM1_FAULT0,
              "FlexPWM1 FAULT0 XBAR selector drifted");
static_assert(AcmpPwmTargets[1].xbarFault0Output == XBARA1_OUT_FLEXPWM2_FAULT0,
              "FlexPWM2 FAULT0 XBAR selector drifted");
static_assert(AcmpFault0DisA == FLEXPWM_SMDISMAP0_DIS0A(0x1),
              "DISMAP0 A encoding drifted");
static_assert(AcmpFault0DisB == FLEXPWM_SMDISMAP0_DIS0B(0x1),
              "DISMAP0 B encoding drifted");
static_assert(AcmpFault0DisX == FLEXPWM_SMDISMAP0_DIS0X(0x1),
              "DISMAP0 X encoding drifted");
static_assert(AcmpFault0Fie == FLEXPWM_FCTRL0_FIE(0x1),
              "FCTRL0 FIE encoding drifted");
static_assert(AcmpFault0Fsafe == FLEXPWM_FCTRL0_FSAFE(0x1),
              "FCTRL0 FSAFE encoding drifted");
static_assert(AcmpFault0Fauto == FLEXPWM_FCTRL0_FAUTO(0x1),
              "FCTRL0 FAUTO encoding drifted");
static_assert(AcmpFault0Flvl == FLEXPWM_FCTRL0_FLVL(0x1),
              "FCTRL0 FLVL encoding drifted");
static_assert(AcmpFaultGlitchStretch == FLEXPWM_FFILT0_GSTR,
              "FFILT0 GSTR encoding drifted");

static volatile bool vCbcEnabled = false;
static volatile bool vLatchedArmed = false;
static volatile bool vProtectionReady = false;
static volatile uint32_t vCbcTrips = 0;
static uint32_t tripsAtLastSecond = 0;
static uint32_t tripsPerSec = 0;
static uint8_t activeCmp = 0; // 1-4 while armed, 0 idle
static uint16_t actualThresholdMv = 0;

// The four CMP units are consecutive 8-register blocks from CMP1.
static volatile IMXRT_REGISTER8_t *cmpBlock(uint8_t cmp) {
  switch (cmp) {
    case 2: return &IMXRT_CMP2;
    case 3: return &IMXRT_CMP3;
    case 4: return &IMXRT_CMP4;
    default: return &IMXRT_CMP1;
  }
}

static IMXRT_FLEXPWM_t *pwmBlock(uint8_t module) {
  switch (module) {
    case 1: return &IMXRT_FLEXPWM1;
    case 2: return &IMXRT_FLEXPWM2;
    default: return nullptr;
  }
}

// Latched-mode trip: the hardware has already disabled PWM1 SM3 and PWM2
// SM0-3.  Mirror the GPIO trip so capture, UI and the remaining outputs react
// identically.  Integer-only, no FPU stacking.
FASTRUN static void acmpFaultIsr() {
  // MASK acts before polarity, so the common helper disconnects OUTEN and
  // un-inverts cells before disabling timers.
  maskAllOutputsSafely();
  NVIC_DISABLE_IRQ(IRQ_FLEXPWM2_0);
  // Both FFLAG0 values stay latched; acmpClearLatch() clears PWM1 first and
  // this IRQ-owning PWM2 module last.
  NVIC_DISABLE_IRQ(IRQ_FLEXPWM2_FAULT);
  vFaultTripped = true;
  asm volatile("dsb");
}

// W1C hazard: FFLAG is write-1-to-clear, so a plain RMW of FSTS0 wipes every
// pending flag.  Mask the complete flag nibble and explicitly clear only the
// requested flags; only fault 0's recovery bits are ours to change.
static void writeFsts(IMXRT_FLEXPWM_t &pwm, uint16_t recoveryBits,
                      uint16_t clearFlags) {
  uint16_t s = pwm.FSTS0;
  s &= ~FLEXPWM_FSTS0_FFLAG(0xF);
  s &= ~(FLEXPWM_FSTS0_FFULL(0x1) | FLEXPWM_FSTS0_FHALF(0x1));
  s |= recoveryBits | FLEXPWM_FSTS0_FFLAG(clearFlags);
  pwm.FSTS0 = s;
}

static bool xbarSelectionMatches(uint8_t input, uint8_t output) {
  if (input >= 88 || output >= 132) {
    return false;
  }
  volatile uint16_t *reg = &XBARA1_SEL0 + (output / 2);
  const uint16_t value = *reg;
  const uint8_t selected = output & 1
      ? static_cast<uint8_t>((value >> 8) & 0x7F)
      : static_cast<uint8_t>(value & 0x7F);
  return selected == input;
}

static bool connectXbarVerified(uint8_t input, uint8_t output) {
  if (!xbarConnect(input, output)) {
    return false;
  }
  asm volatile("dsb");
  return xbarSelectionMatches(input, output);
}

static bool parkFaultRoutes() {
  bool ok = true;
  for (uint8_t i = 0; i < AcmpPwmTargetCount; ++i) {
    const bool parked = connectXbarVerified(XBARA1_IN_LOGIC_LOW,
                                            AcmpPwmTargets[i].xbarFault0Output);
    ok = parked && ok; // do not short-circuit: always park both selectors
  }
  return ok;
}

static void prepareParkedFaultControls() {
  for (uint8_t i = 0; i < AcmpPwmTargetCount; ++i) {
    IMXRT_FLEXPWM_t *pwm = pwmBlock(AcmpPwmTargets[i].module);
    if (pwm != nullptr) {
      uint16_t control = pwm->FCTRL0;
      control |= FLEXPWM_FCTRL0_FLVL(0x1); // LOGIC_LOW park must be inactive
      control &= static_cast<uint16_t>(~FLEXPWM_FCTRL0_FIE(0x1));
      pwm->FCTRL0 = control;
    }
  }
}

static bool parkedFaultControlsMatch() {
  const uint16_t mask = FLEXPWM_FCTRL0_FLVL(0x1) |
                        FLEXPWM_FCTRL0_FIE(0x1);
  const uint16_t expected = FLEXPWM_FCTRL0_FLVL(0x1);
  return (IMXRT_FLEXPWM1.FCTRL0 & mask) == expected &&
         (IMXRT_FLEXPWM2.FCTRL0 & mask) == expected;
}

static uint16_t recoveryBits(bool cycleByCycle) {
  uint16_t bits = FLEXPWM_FSTS0_FFULL(0x1);
  if (cycleByCycle) {
    bits |= FLEXPWM_FSTS0_FHALF(0x1);
  }
  return bits;
}

static bool configureFaultTarget(const AcmpPwmTarget &target,
                                 bool cycleByCycle) {
  IMXRT_FLEXPWM_t *pwm = pwmBlock(target.module);
  if (pwm == nullptr) {
    return false;
  }

  // FFILT0 is module-wide.  A non-zero period/count belongs to another fault
  // policy and would make a narrow ACMP trip bypass the registered FFLAG path,
  // so fail dark instead of silently taking ownership.  With the filter
  // bypassed, RM 55.8.52 requires GSTR to guarantee that a narrow fault which
  // takes the combinational shutdown path is also captured in FFLAG.
  const uint16_t faultFilter = pwm->FFILT0;
  if (!acmpFaultFilterAvailable(faultFilter)) {
    return false;
  }
  pwm->FFILT0 = acmpFaultFilterWithGlitchStretch(faultFilter);

  constexpr uint16_t Fault0MapMask =
      AcmpFault0DisA | AcmpFault0DisB | AcmpFault0DisX;
  constexpr uint16_t ExpectedFault0Map = AcmpFault0DisA | AcmpFault0DisB;
  for (uint8_t sm = 0; sm < 4; ++sm) {
    if ((target.submoduleMask & (1u << sm)) == 0) {
      // FAULT0 selection/control is module-wide.  An existing fault0 map on
      // an untargeted PWM1 submodule would be silently hijacked by this ACMP
      // source, so preserve the register and refuse to arm instead.
      if ((pwm->SM[sm].DISMAP0 & Fault0MapMask) != 0) {
        return false;
      }
      continue;
    }
    pwm->SM[sm].DISMAP0 = acmpMapFault0ToAb(pwm->SM[sm].DISMAP0);
  }

  // NOCOMB0=0 guarantees the asynchronous/combinational disable path.  Other
  // fault inputs and every unrelated FCTRL bit are preserved.
  pwm->FCTRL20 &= static_cast<uint16_t>(~FLEXPWM_FCTRL20_NOCOMB(0x1));
  pwm->FCTRL0 = acmpFault0Control(
      pwm->FCTRL0, cycleByCycle, target.irqOwner && !cycleByCycle);
  writeFsts(*pwm, recoveryBits(cycleByCycle), 0x1);
  asm volatile("dsb");

  const uint16_t expectedControl = acmpFault0Control(
      0, cycleByCycle, target.irqOwner && !cycleByCycle);
  const uint16_t recoveryMask = FLEXPWM_FSTS0_FFULL(0x1) |
                                FLEXPWM_FSTS0_FHALF(0x1);
  if ((pwm->FFILT0 & AcmpFaultGlitchStretch) == 0 ||
      !acmpFaultFilterAvailable(pwm->FFILT0) ||
      (pwm->FCTRL20 & FLEXPWM_FCTRL20_NOCOMB(0x1)) != 0 ||
      (pwm->FCTRL0 & AcmpFault0ControlMask) != expectedControl ||
      (pwm->FSTS0 & recoveryMask) != recoveryBits(cycleByCycle)) {
    return false;
  }

  for (uint8_t sm = 0; sm < 4; ++sm) {
    if ((target.submoduleMask & (1u << sm)) == 0) {
      continue;
    }
    if ((pwm->SM[sm].DISMAP0 & Fault0MapMask) != ExpectedFault0Map) {
      return false;
    }
  }
  return true;
}

static void disableActiveComparator() {
  if (activeCmp != 0) {
    cmpBlock(activeCmp)->offset01 = 0x00;
    activeCmp = 0;
  }
}

static void failCurrentLimitArm(const char *reason) {
  vCbcEnabled = false;
  vLatchedArmed = false;
  vProtectionReady = false;
  NVIC_DISABLE_IRQ(IRQ_FLEXPWM2_FAULT);
  disableActiveComparator();
  prepareParkedFaultControls();
  // Best effort only: if selector readback is what failed, the safe state is
  // OUTEN disconnected and all timers disabled, not faith in another write.
  (void)parkFaultRoutes();
  maskAllOutputsSafely();
  vFaultTripped = true;
  writeLogLevel(EventError, reason);
}

void acmpConfigure() {
  // Tear down first so a reconfigure never leaves a half-armed path.
  vCbcEnabled = false;
  vLatchedArmed = false;
  vProtectionReady = false;
  NVIC_DISABLE_IRQ(IRQ_FLEXPWM2_FAULT);
  disableActiveComparator();

  CCM_CCGR2 |= CCM_CCGR2_XBAR1(CCM_CCGR_ON);
  prepareParkedFaultControls();
  const bool parked = parkFaultRoutes();
  // Clear stale flags on both modules, retaining full-cycle recovery.  With
  // FFULL0=FHALF0=0 a pending disable cannot release (RM 55.8.51).
  writeFsts(IMXRT_FLEXPWM1, FLEXPWM_FSTS0_FFULL(0x1), 0x1);
  writeFsts(IMXRT_FLEXPWM2, FLEXPWM_FSTS0_FFULL(0x1), 0x1);
  asm volatile("dsb");
  // A peripheral flag can be cleared while its already-latched NVIC pending
  // state remains set.  Remove that stale notification while the comparator is
  // disabled and both selectors are parked; any later real fault pends anew.
  NVIC_CLEAR_PENDING(IRQ_FLEXPWM2_FAULT);
  const uint16_t recoveryMask = FLEXPWM_FSTS0_FFULL(0x1) |
                                FLEXPWM_FSTS0_FHALF(0x1);
  const bool recoveryReady =
      (IMXRT_FLEXPWM1.FSTS0 & recoveryMask) == FLEXPWM_FSTS0_FFULL(0x1) &&
      (IMXRT_FLEXPWM2.FSTS0 & recoveryMask) == FLEXPWM_FSTS0_FFULL(0x1);
  if (!parked || !parkedFaultControlsMatch() || !recoveryReady) {
    failCurrentLimitArm(
        "CurrentLimit: safe park/control/recovery readback failed; outputs inhibited");
    return;
  }

  if (!config.CurrentLimit.Enabled) {
    vProtectionReady = true;
    return;
  }

  const AcmpRoute route = acmpRouteForPin(config.CurrentLimit.Pin);
  if (route.cmp == 0) {
    failCurrentLimitArm("CurrentLimit: pin is not comparator-capable; outputs inhibited");
    return;
  }

  const bool cycleByCycle = config.CurrentLimit.CycleByCycle;
  if (!cycleByCycle) {
    // Install the sole software notifier before enabling PWM2 FIE0.  The NVIC
    // line stays disabled until both live XBAR routes have verified.
    attachInterruptVector(IRQ_FLEXPWM2_FAULT, acmpFaultIsr);
    NVIC_SET_PRIORITY(IRQ_FLEXPWM2_FAULT, 16);
  }

  bool pwmConfigured = true;
  for (uint8_t i = 0; i < AcmpPwmTargetCount; ++i) {
    const bool configured = configureFaultTarget(AcmpPwmTargets[i], cycleByCycle);
    pwmConfigured = configured && pwmConfigured;
  }
  // Fault states are pre-polarity values.  Check them against the steady-state
  // polarity that the release path will restore, not the temporary all-HighTrue
  // state used while global inhibit is asserted.
  if (!pwmConfigured || !pwmFaultStatesSafeForConfiguredPolarity()) {
    failCurrentLimitArm(
        "CurrentLimit: FlexPWM FAULT0 config or safe-low state readback failed; outputs inhibited");
    return;
  }

  // Analog input: the pad's analog net is hardwired (no IOMUXC alt needed).
  // INPUT_DISABLE turns off the pad keeper, which otherwise corrupts a
  // high-impedance analog source.
  pinMode(config.CurrentLimit.Pin, INPUT_DISABLE);
  switch (route.cmp) {
    case 1: CCM_CCGR3 |= CCM_CCGR3_ACMP1(CCM_CCGR_ON); break;
    case 2: CCM_CCGR3 |= CCM_CCGR3_ACMP2(CCM_CCGR_ON); break;
    case 3: CCM_CCGR3 |= CCM_CCGR3_ACMP3(CCM_CCGR_ON); break;
    case 4: CCM_CCGR3 |= CCM_CCGR3_ACMP4(CCM_CCGR_ON); break;
  }

  volatile IMXRT_REGISTER8_t *cmp = cmpBlock(route.cmp);
  cmp->offset01 = 0x00; // CR1: disabled while reconfiguring (RM 65.4.4.1)
  cmp->offset00 = 0x00; // CR0: filter reset
  cmp->offset02 = 0x00; // FPR

  const uint8_t code = acmpThresholdToDacCode(
      config.CurrentLimit.ThresholdMillivolts);
  actualThresholdMv = acmpDacCodeToMv(code);
  cmp->offset04 = static_cast<uint8_t>(0x80 | code);              // DACEN | VOSEL
  cmp->offset05 = static_cast<uint8_t>((route.psel << 3) | 0x07); // pin vs DAC ch7

  // Validation currently forces the filter to bypass.  Keep the register
  // handling explicit so a future qualified filter cannot inherit stale bits.
  const uint8_t filtCnt = config.CurrentLimit.FilterCount & 0x07;
  const uint8_t filtPer = config.CurrentLimit.FilterPeriod;
  cmp->offset02 = filtPer;
  cmp->offset00 = static_cast<uint8_t>(((filtPer ? filtCnt : 0) << 4) | 0x01);
  cmp->offset01 = 0x11; // CR1: high-speed + enabled; OPE=0, INV=0
  activeCmp = route.cmp;
  delayMicroseconds(5);
  cmp->offset03 = 0x06; // SCR: clear spurious edge flags (W1C)

  // Route last.  Each XBARA1 output owns its selector, so a single ACMP input
  // may legitimately feed both private FAULT0 outputs.
  const uint8_t input = acmpXbarInputForCmp(route.cmp);
  bool routed = true;
  for (uint8_t i = 0; i < AcmpPwmTargetCount; ++i) {
    const bool connected = connectXbarVerified(input,
                                               AcmpPwmTargets[i].xbarFault0Output);
    routed = connected && routed;
  }
  if (!routed) {
    failCurrentLimitArm("CurrentLimit: dual FAULT0 XBAR route/readback failed; outputs inhibited");
    return;
  }

  vLatchedArmed = !cycleByCycle;
  vCbcEnabled = cycleByCycle;
  vProtectionReady = true;
  vCbcTrips = 0;
  tripsAtLastSecond = 0;
  tripsPerSec = 0;
  if (vLatchedArmed) {
    // A narrow pulse can occur after PWM1's selector is connected but before
    // PWM2's IRQ-owning selector is live.  PWM1 still latched it; surface that
    // state explicitly instead of relying on the later release gate to notice.
    if (acmpFaultLatched()) {
      vFaultTripped = true;
      maskAllOutputsSafely();
      NVIC_DISABLE_IRQ(IRQ_FLEXPWM2_0);
    } else {
      // If the source asserts after the check, PWM2 FFLAG0 invokes the ISR.
      // Both hardware paths were live before this point.
      NVIC_ENABLE_IRQ(IRQ_FLEXPWM2_FAULT);
    }
  }

  char strBuf[LOG_BUF_SIZE];
  snprintf(strBuf, sizeof(strBuf),
           "CurrentLimit armed: pin %u -> CMP%u -> PWM1.SM3 + PWM2.SM0-3, "
           "threshold %umV (DAC %u), %s, filter %luns",
           config.CurrentLimit.Pin, route.cmp, actualThresholdMv, code,
           cycleByCycle ? "cycle-by-cycle" : "latched",
           static_cast<unsigned long>(
               acmpFilterGlitchNanos(filtCnt, filtPer, F_BUS_ACTUAL)));
  writeLog(strBuf);
}

// Called from the PWM2 modulation ISR once per carrier cycle.  Count at most
// once if either module observed the shared comparator event, then clear each
// module's flag independently without touching fault1-3 flags.
FASTRUN void acmpCbcTick() {
  if (!vCbcEnabled) {
    return;
  }
  const uint16_t s1 = IMXRT_FLEXPWM1.FSTS0;
  const uint16_t s2 = IMXRT_FLEXPWM2.FSTS0;
  const uint16_t flag0 = FLEXPWM_FSTS0_FFLAG(0x1);
  if (((s1 | s2) & flag0) == 0) {
    return;
  }

  vCbcTrips = vCbcTrips + 1;
  if ((s1 & flag0) != 0) {
    IMXRT_FLEXPWM1.FSTS0 =
        (s1 & ~FLEXPWM_FSTS0_FFLAG(0xF)) | flag0;
  }
  if ((s2 & flag0) != 0) {
    IMXRT_FLEXPWM2.FSTS0 =
        (s2 & ~FLEXPWM_FSTS0_FFLAG(0xF)) | flag0;
  }
}

void acmpTask() {
  if (!vCbcEnabled) {
    return;
  }
  // Sampled by PWM2's reload ISR.  This remains a useful event rate but is
  // not an exact count of PWM1 cycles if PWM1 and PWM2 use different carriers.
  static elapsedMillis sinceRate;
  if (sinceRate >= 1000) {
    sinceRate = 0;
    const uint32_t now = vCbcTrips;
    tripsPerSec = now - tripsAtLastSecond;
    tripsAtLastSecond = now;
  }
}

void acmpClearLatch() {
  if (!vLatchedArmed) {
    return;
  }
  // The IRQ-owning PWM2 module is deliberately last.  A source that reasserts
  // during the sequence remains visible to acmpFaultLatched() and will pend the
  // PWM2 interrupt again when the caller re-arms it.
  writeFsts(IMXRT_FLEXPWM1, FLEXPWM_FSTS0_FFULL(0x1), 0x1);
  writeFsts(IMXRT_FLEXPWM2, FLEXPWM_FSTS0_FFULL(0x1), 0x1);
  asm volatile("dsb");
  // The caller immediately proves both live pins and latches stayed quiet.
  // Clearing the stale NVIC state here prevents an old notification from
  // conservatively re-tripping an otherwise clean operator acknowledgement.
  NVIC_CLEAR_PENDING(IRQ_FLEXPWM2_FAULT);
}

// Re-arm the vector the trip ISR silenced - called LAST in the clear flow.
void acmpRearmFaultIrq() {
  if (vLatchedArmed) {
    NVIC_ENABLE_IRQ(IRQ_FLEXPWM2_FAULT);
  }
}

bool acmpFaultPinActive() {
  if (activeCmp == 0) {
    return false;
  }
  const uint16_t pin0 = FLEXPWM_FSTS0_FFPIN(0x1);
  return ((IMXRT_FLEXPWM1.FSTS0 | IMXRT_FLEXPWM2.FSTS0) & pin0) != 0;
}

bool acmpFaultLatched() {
  if (activeCmp == 0 || !vLatchedArmed) {
    return false;
  }
  const uint16_t flag0 = FLEXPWM_FSTS0_FFLAG(0x1);
  return ((IMXRT_FLEXPWM1.FSTS0 | IMXRT_FLEXPWM2.FSTS0) & flag0) != 0;
}

bool acmpProtectionReady() {
  return vProtectionReady;
}

bool acmpArmedLatched() {
  return vLatchedArmed;
}

bool acmpCbcEnabled() {
  return vCbcEnabled;
}

uint32_t acmpCbcTripCount() {
  return vCbcTrips;
}

uint32_t acmpCbcTripsPerSec() {
  return tripsPerSec;
}

uint16_t acmpActualThresholdMv() {
  return activeCmp != 0 ? actualThresholdMv : 0;
}
