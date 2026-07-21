#ifndef CAPTURE_H
#define CAPTURE_H

// PWM-synchronous waveform capture: the modulation ISR starts one ADC
// conversion per carrier cycle at the reload point (the average-current
// sampling instant for centre-aligned PWM) and stores results in a PSRAM ring
// deep enough for ~52s at a 20kHz carrier. On a fault trip the ring freezes,
// preserving the pre-fault history as a flight recorder.

#include <stdint.h>

void captureConfigure(); // (re)apply config: enable/disable, pin; also unfreezes
void captureTick();      // called from the modulation ISR each carrier cycle
bool captureActive();
bool captureIsFrozen();
uint32_t captureSampleCount(); // total samples written since (re)configure
uint16_t captureLatestRaw();   // 12-bit
uint32_t captureMeanRaw(uint32_t n);
uint32_t captureRingSamples();
// Envelope-decimate the most recent `count` samples into `bins` min/max pairs.
// Returns the sample count actually used (clamped to what is available).
uint32_t captureDecimate(uint32_t count, uint32_t bins, uint16_t *outMin, uint16_t *outMax);

#endif
