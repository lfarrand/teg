#ifndef MPPT_H
#define MPPT_H

// MPPT task: perturb & observe on the modulation index target using the
// meter's real-power measurement.

#include <stdint.h>

void mpptConfigure(); // (re)apply config; reseeds from the current index target
void mpptTask();      // call from loop(); self-paces on Mppt.IntervalMs

bool mpptActive();               // enabled AND metering valid AND stepping
uint16_t mpptIndexMilli();       // current commanded index
uint16_t mpptStepMilli();        // current adaptive step size
int8_t mpptDirection();          // +1 climbing, -1 descending
int32_t mpptLastDeltaMw();       // power change seen at the last evaluation

#endif
