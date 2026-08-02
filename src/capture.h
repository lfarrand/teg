#ifndef CAPTURE_H
#define CAPTURE_H

// PWM-synchronous waveform capture: the modulation ISR starts one ADC
// conversion per carrier cycle at the reload point (the average-current
// sampling instant for centre-aligned PWM) and stores results in a PSRAM ring
// deep enough for ~52s at a 20kHz carrier. On a fault trip the ring freezes,
// preserving the pre-fault history as a flight recorder.

#include <stdint.h>

// The ADC count at full scale, for every reader of an analog pin on this board.
//
// captureConfigure() programs both ADC modules to 12 bits via the Teensy_ADC library,
// which writes the peripheral registers directly. Anything calling the core's
// analogRead() therefore gets 12-bit data whether it expects it or not - and two
// callers did not: the closed-loop feedback fallback and the /api/status readout both
// scaled by 1023, reading four times high. In the feedback case that drove the
// regulator down until the real output sat at a quarter of its setpoint.
//
// setup() now calls analogReadResolution(12) so the core agrees with the hardware
// rather than the two being incidentally consistent, and every scaling site uses this
// constant instead of a literal.
constexpr uint32_t AdcCountFullScale = 4095;

void captureConfigure(); // (re)apply config: enable/disable, pin; also unfreezes
void captureTick();      // called from the modulation ISR each carrier cycle
bool captureActive();
bool captureIsFrozen();
uint32_t captureSampleCount(); // total samples written since (re)configure
uint16_t captureLatestRaw();   // 12-bit
uint32_t captureVoltageMissCount(); // carrier ticks where ADC0/1 was not ready
uint32_t captureCurrentMissCount(); // paired-current ticks not ready
uint32_t captureMeanRaw(uint32_t n);
uint32_t captureRingSamples();
// Envelope-decimate the most recent `count` samples into `bins` min/max pairs.
// Returns the sample count actually used (clamped to what is available).
uint32_t captureDecimate(uint32_t count, uint32_t bins, uint16_t *outMin, uint16_t *outMax);

// Copy the most recent n samples in chronological order (unwrapping the
// ring). Returns n on success, 0 if fewer samples are available.
uint32_t captureCopyRecent(int16_t *out, uint32_t n);

// Lossless incremental drain: copy up to maxN samples the ISR has written
// since *cursorTotal (a captureSampleCount() watermark the caller owns) and
// advance the cursor. On ring overrun (caller stalled) or a reconfigure the
// cursor snaps forward to the oldest safely-readable sample. Returns the
// number of samples copied (uniformly spaced at the carrier period).
uint32_t captureReadSince(uint32_t *cursorTotal, uint16_t *dst, uint32_t maxN);

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

// ---------------------------------------------------------------------------
// Triggered scope: arm a level/edge trigger on either channel; the ring
// freezes postSamples after the crossing, preserving pre-trigger history.
// ---------------------------------------------------------------------------

// Returns false if the requested source channel is not being sampled
bool captureScopeArm(bool currentSource, uint16_t levelCounts, bool fallingEdge,
                     uint32_t postSamples);
void captureScopeRelease(); // disarm + unfreeze (fault trips re-freeze)
uint8_t captureScopeState(); // ScopeState from scope_math.h
bool captureScopeSourceIsCurrent();
uint16_t captureScopeLevelCounts();
bool captureScopeFalling();
uint32_t captureScopePostSamples();
uint32_t captureScopeTrigSample(); // captureSampleCount() value at the trigger

// Raw sample access for download: available = samples currently in the ring.
// Copy `n` samples starting `offset` into the window of the most recent
// `windowN` samples (chronological order).
uint32_t captureRawAvailable(bool currentChannel);
void captureRawCopy(bool currentChannel, uint32_t windowN, uint32_t offset,
                    uint16_t *dst, uint32_t n);

#endif
