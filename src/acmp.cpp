// Hardware overcurrent protection (item: ACMP fault + cycle-by-cycle limit).
//
// Signal path: current-sense pin -> CMPn plus input; CMPn minus input = its
// internal 6-bit DAC programmed to the threshold; CMPn output -> XBARA1
// input (26+n-1) -> XBARA1 output 49 = FlexPWM2's PRIVATE FAULT0 input ->
// combinational disable of Sm20-23 PWM_A/B (DISMAP), no software in the loop.
//
// FAULT0 is private to FlexPWM2 (unlike the FAULT2/3 lines, which XBAR fans
// out to all four FlexPWM modules at once), so Tm1/Tm3/Tm4 outputs are
// untouched by design - important because those modules power up with
// FSTS0.FFULL=FHALF=0, a state in which a latched fault can never be cleared
// at runtime (RM 55.8.50).
//
// Two modes (FlexPWM-side only; the comparator config is identical):
//  - Latched (FAUTO0=0, FSAFE0=1): outputs stay off until the fault flag is
//    cleared AND the comparator is quiet at a full-cycle boundary. The fault
//    IRQ sets vFaultTripped so the capture flight recorder freezes and the
//    UI banner shows, mirroring the software GPIO trip.
//  - Cycle-by-cycle (FAUTO0=1): the combinational path chops the outputs the
//    instant the comparator trips; the latch holds them off until the next
//    full- or half-cycle boundary where the comparator reads quiet, then
//    re-enables automatically. Classic per-cycle current limiting - not a
//    fault, so vFaultTripped is never set; trips are counted instead.
//
// Teensy core context this relies on: startup's flexpwm_init already set
// FCTRL0.FLVL=0xF (fault = logic high) on all modules and cleared the
// power-up fault latches, so the XBAR reset state (LOGIC_LOW) is inactive
// and an active-high comparator output drops straight in.
//
// BENCH-VERIFY before trusting: absolute DAC threshold (feed a known DC
// level, sweep the threshold, watch FSTS0 FFPIN0), OPE=0 output-to-XBAR
// routing, and cycle-by-cycle release behavior on the triggered scope.

#include "acmp.h"
#include "acmp_math.h"
#include "config_json.h"
#include "pwm_utils.h" // vFaultTripped, spwmActive
#include "utils.h"
#include <Arduino.h>

extern MainConfig config;

static volatile bool vCbcEnabled = false;
static volatile bool vLatchedArmed = false;
static volatile uint32_t vCbcTrips = 0;
static uint32_t tripsAtLastSecond = 0;
static uint32_t tripsPerSec = 0;
static uint8_t activeCmp = 0; // 1-4 while armed, 0 idle
static uint16_t actualThresholdMv = 0;

// The four CMP units are consecutive 8-register blocks from CMP1
static volatile IMXRT_REGISTER8_t *cmpBlock(uint8_t cmp) {
  switch (cmp) {
    case 2: return &IMXRT_CMP2;
    case 3: return &IMXRT_CMP3;
    case 4: return &IMXRT_CMP4;
    default: return &IMXRT_CMP1;
  }
}

// Latched-mode trip: the hardware has already disabled Sm20-23; mirror the
// software GPIO trip so the rest of the system reacts identically (capture
// freeze, UI banner, modulation halt). Integer-only, no FPU stacking.
FASTRUN static void acmpFaultIsr() {
  Tm1.disable();
  Tm2.disable();
  Tm3.disable();
  Tm4.disable();
  NVIC_DISABLE_IRQ(IRQ_FLEXPWM2_0);
  // FFLAG0 stays latched (it gates output recovery); silence our own vector
  // instead - acmpClearLatch() re-arms it
  NVIC_DISABLE_IRQ(IRQ_FLEXPWM2_FAULT);
  vFaultTripped = true;
  asm volatile("dsb");
}

// W1C hazard: FFLAG is write-1-to-clear, so any plain RMW of FSTS0 wipes
// every pending flag. Always mask the FFLAG nibble out and set only the
// bits meant to clear; only fault 0's recovery bits are ours to change.
static void writeFsts(uint16_t recoveryBits, uint16_t clearFlags) {
  uint16_t s = IMXRT_FLEXPWM2.FSTS0;
  s &= ~FLEXPWM_FSTS0_FFLAG(0xF);
  s &= ~(FLEXPWM_FSTS0_FFULL(0x1) | FLEXPWM_FSTS0_FHALF(0x1));
  s |= recoveryBits | FLEXPWM_FSTS0_FFLAG(clearFlags);
  IMXRT_FLEXPWM2.FSTS0 = s;
}

void acmpConfigure() {
  // Tear down first so a reconfigure never leaves a half-armed path
  vCbcEnabled = false;
  vLatchedArmed = false;
  NVIC_DISABLE_IRQ(IRQ_FLEXPWM2_FAULT);
  if (activeCmp != 0) {
    cmpBlock(activeCmp)->offset01 = 0x00; // CR1: comparator off
    activeCmp = 0;
  }
  // Park FlexPWM2 FAULT0 on logic low (inactive, since FLVL=1) and drop FIE0
  CCM_CCGR2 |= CCM_CCGR2_XBAR1(CCM_CCGR_ON);
  xbarConnect(XBARA1_IN_LOGIC_LOW, XBARA1_OUT_FLEXPWM2_FAULT0);
  IMXRT_FLEXPWM2.FCTRL0 &= ~FLEXPWM_FCTRL0_FIE(0x1);
  // Clear a stale FFLAG0 but KEEP full-cycle recovery armed: with FFULL and
  // FHALF both 0 a pending fault-disable can never release (RM 55.8.51), so
  // a teardown while latched would leave Sm20-23 permanently dead
  writeFsts(FLEXPWM_FSTS0_FFULL(0x1), 0x1);

  if (!config.CurrentLimit.Enabled) {
    return;
  }

  const AcmpRoute route = acmpRouteForPin(config.CurrentLimit.Pin);
  if (route.cmp == 0) {
    writeLog("CurrentLimit: pin is not comparator-capable; disabled");
    return;
  }

  // --- FlexPWM2 fault channel, fault input 0, BEFORE routing the XBAR ---

  // Sm20-23: map fault0 to disable PWM_A/B only (RMW keeps reserved bits;
  // clearing the fault1-3 bits is defense in depth - those inputs are held
  // inactive by the core's FLVL=1 + XBAR LOGIC_LOW defaults)
  for (int i = 0; i < 4; i++) {
    uint16_t d = IMXRT_FLEXPWM2.SM[i].DISMAP0;
    d &= ~(FLEXPWM_SMDISMAP0_DIS0A(0xF) | FLEXPWM_SMDISMAP0_DIS0B(0xF) |
           FLEXPWM_SMDISMAP0_DIS0X(0xF));
    d |= FLEXPWM_SMDISMAP0_DIS0A(0x1) | FLEXPWM_SMDISMAP0_DIS0B(0x1);
    IMXRT_FLEXPWM2.SM[i].DISMAP0 = d;
  }

  uint16_t f = IMXRT_FLEXPWM2.FCTRL0;
  f |= FLEXPWM_FCTRL0_FLVL(0x1); // fault = logic high (comparator above threshold)
  if (config.CurrentLimit.CycleByCycle) {
    f |= FLEXPWM_FCTRL0_FAUTO(0x1);  // automatic clearing
    f &= ~FLEXPWM_FCTRL0_FIE(0x1);   // no per-event interrupt
    IMXRT_FLEXPWM2.FCTRL0 = f;
    // Recover at either boundary: full cycle (rollover to INIT) or the VAL0
    // half-cycle point (this firmware programs VAL0 = period/2)
    writeFsts(FLEXPWM_FSTS0_FFULL(0x1) | FLEXPWM_FSTS0_FHALF(0x1), 0x1);
  } else {
    f &= ~FLEXPWM_FCTRL0_FAUTO(0x1); // manual = latched
    f |= FLEXPWM_FCTRL0_FSAFE(0x1);  // recovery also requires the pin quiet
    f |= FLEXPWM_FCTRL0_FIE(0x1);    // interrupt tells software about the trip
    IMXRT_FLEXPWM2.FCTRL0 = f;
    writeFsts(FLEXPWM_FSTS0_FFULL(0x1), 0x1);
    attachInterruptVector(IRQ_FLEXPWM2_FAULT, acmpFaultIsr);
    NVIC_SET_PRIORITY(IRQ_FLEXPWM2_FAULT, 16); // preempts the modulation ISR
    NVIC_ENABLE_IRQ(IRQ_FLEXPWM2_FAULT);
  }

  // --- Comparator ---

  // Analog input: the pad's analog net is hardwired (no IOMUXC alt needed);
  // INPUT_DISABLE turns the pad keeper off, which corrupts high-impedance
  // analog sources if left on
  pinMode(config.CurrentLimit.Pin, INPUT_DISABLE);

  switch (route.cmp) { // clock gate per unit
    case 1: CCM_CCGR3 |= CCM_CCGR3_ACMP1(CCM_CCGR_ON); break;
    case 2: CCM_CCGR3 |= CCM_CCGR3_ACMP2(CCM_CCGR_ON); break;
    case 3: CCM_CCGR3 |= CCM_CCGR3_ACMP3(CCM_CCGR_ON); break;
    case 4: CCM_CCGR3 |= CCM_CCGR3_ACMP4(CCM_CCGR_ON); break;
  }

  volatile IMXRT_REGISTER8_t *cmp = cmpBlock(route.cmp);
  cmp->offset01 = 0x00; // CR1: disabled while reconfiguring (RM 65.4.4.1)
  cmp->offset00 = 0x00; // CR0: filter reset
  cmp->offset02 = 0x00; // FPR

  const uint8_t code = acmpThresholdToDacCode(config.CurrentLimit.ThresholdMillivolts);
  actualThresholdMv = acmpDacCodeToMv(code);
  cmp->offset04 = static_cast<uint8_t>(0x80 | code);                 // DACCR: DACEN | VOSEL
  cmp->offset05 = static_cast<uint8_t>((route.psel << 3) | 0x07);    // MUXCR: pin vs DAC (ch 7)

  // CR0: optional sampled filter (latching only - the FlexPWM disable path
  // stays combinational either way) + 21mV hysteresis against chatter
  const uint8_t filtCnt = config.CurrentLimit.FilterCount & 0x07;
  const uint8_t filtPer = config.CurrentLimit.FilterPeriod;
  cmp->offset02 = filtPer;                                           // FPR
  cmp->offset00 = static_cast<uint8_t>((filtPer ? filtCnt : 0) << 4 | 0x01); // CR0
  cmp->offset01 = 0x11; // CR1: PMODE (high speed, ~25ns) | EN; OPE=0, INV=0
  delayMicroseconds(5); // comparator + filter pipeline settle
  cmp->offset03 = 0x06; // SCR: clear spurious rising/falling flags (w1c)

  // --- Route last, with everything armed ---
  xbarConnect(acmpXbarInputForCmp(route.cmp), XBARA1_OUT_FLEXPWM2_FAULT0);

  activeCmp = route.cmp;
  vLatchedArmed = !config.CurrentLimit.CycleByCycle;
  vCbcEnabled = config.CurrentLimit.CycleByCycle;
  vCbcTrips = 0;
  tripsAtLastSecond = 0;
  tripsPerSec = 0;

  char strBuf[LOG_BUF_SIZE];
  snprintf(strBuf, sizeof(strBuf),
           "CurrentLimit armed: pin %u -> CMP%u, threshold %umV (DAC %u), %s, filter %luns",
           config.CurrentLimit.Pin, route.cmp, actualThresholdMv, code,
           config.CurrentLimit.CycleByCycle ? "cycle-by-cycle" : "latched",
           static_cast<unsigned long>(acmpFilterGlitchNanos(filtCnt, filtPer, F_BUS_ACTUAL)));
  writeLog(strBuf);
}

// Called from the modulation ISR once per carrier cycle: FFLAG0 latches on
// each comparator trip, so poll+clear counts "carrier cycles that were
// limited" - exactly the right unit. Integer-only.
FASTRUN void acmpCbcTick() {
  if (!vCbcEnabled) {
    return;
  }
  const uint16_t s = IMXRT_FLEXPWM2.FSTS0;
  if (s & FLEXPWM_FSTS0_FFLAG(0x1)) {
    vCbcTrips = vCbcTrips + 1;
    // Masked w1c: clear only FFLAG0, keep the other flags and recovery bits
    IMXRT_FLEXPWM2.FSTS0 = (s & ~FLEXPWM_FSTS0_FFLAG(0xF)) | FLEXPWM_FSTS0_FFLAG(0x1);
  }
}

void acmpTask() {
  if (!vCbcEnabled) {
    return;
  }
  // Fixed-duty mode has no per-cycle ISR: poll here instead. The count is
  // then a lower bound ("loop passes that observed limiting"), not cycles -
  // and a trip landing between this read/W1C pair and a concurrent ISR tick
  // can go uncounted. Telemetry-only imprecision; the hardware chopping is
  // combinational and unaffected.
  if (!spwmActive()) {
    acmpCbcTick();
  }
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
  writeFsts(FLEXPWM_FSTS0_FFULL(0x1), 0x1); // clear FFLAG0, keep full-cycle recovery
}

// Re-arm the vector the trip ISR silenced - called LAST in the clear flow.
// If the comparator re-latched FFLAG0 while the clear was in flight, the
// pending level interrupt fires the moment this enables and re-trips
// properly (timers masked, vFaultTripped set) instead of being swallowed.
void acmpRearmFaultIrq() {
  if (!vLatchedArmed) {
    return;
  }
  NVIC_ENABLE_IRQ(IRQ_FLEXPWM2_FAULT);
}

bool acmpFaultPinActive() {
  if (activeCmp == 0) {
    return false;
  }
  return (IMXRT_FLEXPWM2.FSTS0 & FLEXPWM_FSTS0_FFPIN(0x1)) != 0;
}

bool acmpArmedLatched() {
  return vLatchedArmed;
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
