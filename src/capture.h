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

// Copy the most recent n samples in chronological order (unwrapping the
// ring). Returns n on success, 0 if fewer samples are available.
uint32_t captureCopyRecent(int16_t *out, uint32_t n);

// ---------------------------------------------------------------------------
// Dual-channel power metering (Meter config): the ISR also samples a current
// sensor on the second ADC module and accumulates zero-corrected V*I, V^2,
// I^2 into double-buffered banks the meter task drains.
// ---------------------------------------------------------------------------

struct MeterBank {
  int64_t sumP = 0;
  uint64_t sumVsq = 0;
  uint64_t sumIsq = 0;
  uint32_t n = 0;
};

bool captureMeterActive();
void captureMeterFlip();                  // switch the ISR to the other bank
uint8_t captureMeterIdleBank();           // the bank the ISR is NOT writing
MeterBank captureMeterTake(uint8_t bank); // copy + clear (call only on the idle bank)
// Envelope-decimate the current channel (smaller ring than voltage)
uint32_t captureDecimateCurrent(uint32_t count, uint32_t bins, uint16_t *outMin, uint16_t *outMax);

#endif
