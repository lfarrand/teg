#ifndef THERMAL_H
#define THERMAL_H

// Temperature monitoring and thermal derating: DS18B20 probes on a OneWire
// bus (stable ROM-sorted probe 1/2 slots) plus the RT1062 die temperature. The
// hottest valid reading drives a linear derate factor that caps the modulation
// index; sensors are read with a non-blocking state machine from loop().

#include <stdint.h>

void thermalConfigure(); // (re)apply config; safe to call on every settings apply
void thermalTask();      // call from loop()

uint16_t thermalDerateMilliNow(); // 1000 = no derating
// Temperatures in deci-degrees C; INT16_MIN when the reading is unavailable
int16_t thermalHotDeciC();
int16_t thermalColdDeciC();
int16_t thermalChipDeciC();
// Carrier cycles the modulation ISR lost to the last OneWire harvest. OneWire masks
// interrupts around each bit slot, so probe reads and carrier cycles compete; this
// makes the cost measurable rather than inferred.
uint32_t thermalHarvestMissedCycles();

#endif
