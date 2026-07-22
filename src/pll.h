#ifndef PLL_H
#define PLL_H

// Grid/reference PLL task: drains capture samples and steers the DDS phase
// increment so the modulation fundamental locks to the external reference.

#include <stdint.h>

void pllConfigure(); // (re)apply Pll config; bumpless while the estimate is valid
void pllTask();      // call from loop() every pass (control task, not 1s cadence)

const char *pllStateStr();      // "off","noCapture","excluded","acquiring","locked","coasting","noRef"
bool pllLocked();
uint64_t pllRefMilliHz();       // PLL frequency estimate
int32_t pllPhaseErrCentiDeg();  // signed, after the configured offset
uint32_t pllRefMillivolts();    // reference amplitude estimate at the pin
uint32_t pllResyncCount();      // drain resyncs (overrun / reconfigure)

#endif
