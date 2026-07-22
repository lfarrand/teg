#ifndef METER_H
#define METER_H

// Power/energy metering task over the capture ISR's accumulator banks; call
// meterTask() from loop(). Readings refresh once per second.

#include <stdint.h>
#include "meter_math.h"

void meterTask();

MeterReadings meterReadings(); // last completed window (valid=false when off)
uint64_t meterEnergyMwh();     // accumulated since boot

#endif
