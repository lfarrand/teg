#ifndef STREAM_RING_H
#define STREAM_RING_H

// Double-buffer scheduling core for SD-streamed waveform playback: the ISR
// consumes samples with next(); when a buffer empties it flips to the other
// and flags the empty one for refill by the main loop. Pure logic (buffers
// are owned by the caller) - unit-tested natively.

#include <stdint.h>

struct StreamRing {
  volatile uint16_t valid[2] = {0, 0};   // samples available per buffer (loop writes)
  volatile bool refillNeeded[2] = {false, false};
  uint8_t active = 0;                    // ISR-owned
  uint16_t index = 0;                    // ISR-owned
  int16_t lastSample = 0;                // held on underrun
  volatile uint32_t underruns = 0;

  void reset() {
    valid[0] = valid[1] = 0;
    refillNeeded[0] = refillNeeded[1] = false;
    active = 0;
    index = 0;
    lastSample = 0;
    underruns = 0;
  }

  // ISR side: returns the next sample, or holds the last one on underrun
  int16_t next(const int16_t *const buffers[2]) {
    if (index >= valid[active]) {
      // Exhausted: hand this buffer back for refill and flip
      valid[active] = 0;
      refillNeeded[active] = true;
      active ^= 1;
      index = 0;
      if (valid[active] == 0) {
        underruns = underruns + 1;
        if (!refillNeeded[active]) {
          refillNeeded[active] = true;
        }
        return lastSample;
      }
    }
    lastSample = buffers[active][index++];
    return lastSample;
  }

  // Loop side: after filling buffer k with n samples (write the data first!)
  void markFilled(uint8_t k, uint16_t n) {
    refillNeeded[k] = false;
    valid[k] = n;
  }
};

#endif
