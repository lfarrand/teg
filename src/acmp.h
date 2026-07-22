#ifndef ACMP_H
#define ACMP_H

// Hardware overcurrent path: on-chip analog comparator -> XBARA1 ->
// FlexPWM2 private FAULT0 input. See acmp.cpp for the register walkthrough.

#include <stdint.h>

void acmpConfigure();       // (re)apply CurrentLimit config; safe to call anytime
void acmpTask();            // loop(): 1s trip-rate bookkeeping + fallback polling
void acmpCbcTick();         // modulation ISR: per-carrier-cycle trip counting
void acmpClearLatch();      // clear the latched FlexPWM fault flag (IRQ stays off)
void acmpRearmFaultIrq();   // re-enable the fault IRQ; call LAST in the clear flow
bool acmpFaultPinActive();  // live filtered comparator state (FSTS0 FFPIN0)
bool acmpArmedLatched();    // latched mode armed (clear flow needs to know)
uint32_t acmpCbcTripCount();    // cumulative cycle-by-cycle limited cycles
uint32_t acmpCbcTripsPerSec();  // last-second rate
uint16_t acmpActualThresholdMv(); // DAC-quantized threshold actually programmed

#endif
