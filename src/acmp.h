#ifndef ACMP_H
#define ACMP_H

// Hardware overcurrent path: on-chip analog comparator -> XBARA1 -> the
// private FAULT0 inputs of FlexPWM1 (SM3, pins 8/7) and FlexPWM2 (SM0-3).
// See acmp.cpp for the register walkthrough.

#include <stdint.h>

void acmpConfigure();       // (re)apply CurrentLimit config while outputs are inhibited
void acmpTask();            // loop(): 1s trip-rate bookkeeping + fallback polling
void acmpCbcTick();         // modulation ISR: per-carrier-cycle trip counting
void acmpClearLatch();      // clear the latched FlexPWM fault flag (IRQ stays off)
void acmpRearmFaultIrq();   // re-enable the fault IRQ; call LAST in the clear flow
bool acmpFaultPinActive();  // either module's live comparator state (FSTS0 FFPIN0)
bool acmpFaultLatched();    // either module's FFLAG0 is latched in latched mode
bool acmpProtectionReady(); // route/config readback passed (also when safely disabled)
bool acmpArmedLatched();    // latched mode armed (clear flow needs to know)
bool acmpCbcEnabled();      // true when per-carrier trip polling is required
uint32_t acmpCbcTripCount();    // cumulative cycle-by-cycle limited cycles
uint32_t acmpCbcTripsPerSec();  // last-second rate
uint16_t acmpActualThresholdMv(); // DAC-quantized threshold actually programmed

#endif
