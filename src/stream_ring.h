#ifndef STREAM_RING_H
#define STREAM_RING_H

// Double-buffer scheduling core for SD-streamed waveform playback: the ISR
// consumes samples with next(); when a buffer empties it flips to the other
// and flags the empty one for refill by the main loop. Pure logic (buffers
// are owned by the caller) - unit-tested natively.

#include <stdint.h>

struct StreamRing {
  // One aligned word publishes ownership + count atomically across ISR/loop.
  // Low 16 bits = valid samples; high state: 0 published/idle, 1 requested,
  // 2 producer filling. The old two-store valid/refillNeeded protocol could
  // publish a half-state and let the loop rewrite a buffer the ISR consumed.
  volatile uint32_t published[2] = {0, 0};
  static constexpr uint32_t RefillRequested = 1UL << 16;
  static constexpr uint32_t Filling = 2UL << 16;
  uint8_t active = 0;                    // ISR-owned
  uint16_t index = 0;                    // ISR-owned
  int16_t lastSample = 0;                // held on underrun
  volatile uint32_t underruns = 0;

  void reset() {
    __atomic_store_n(&published[0], 0U, __ATOMIC_RELEASE);
    __atomic_store_n(&published[1], 0U, __ATOMIC_RELEASE);
    active = 0;
    index = 0;
    lastSample = 0;
    underruns = 0;
  }

  // ISR side: returns the next sample, or holds the last one on underrun
  int16_t next(const int16_t *const buffers[2]) {
    uint32_t state = __atomic_load_n(&published[active], __ATOMIC_ACQUIRE);
    uint16_t valid = static_cast<uint16_t>(state);
    if (valid != 0 && index >= valid) {
      // Exhausted a published buffer: hand it back and prefer the other one.
      __atomic_store_n(&published[active], RefillRequested, __ATOMIC_RELEASE);
      active ^= 1;
      index = 0;
      state = __atomic_load_n(&published[active], __ATOMIC_ACQUIRE);
      valid = static_cast<uint16_t>(state);
    }
    if (valid == 0) {
      // Never overwrite Filling: the producer owns that memory. Request only an
      // idle buffer, and opportunistically use the other buffer if it published
      // while we were looking.
      if (state == 0) {
        uint32_t expected = 0;
        __atomic_compare_exchange_n(&published[active], &expected, RefillRequested,
                                    false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
      }
      const uint8_t other = active ^ 1;
      const uint32_t otherState = __atomic_load_n(&published[other], __ATOMIC_ACQUIRE);
      if (static_cast<uint16_t>(otherState) != 0) {
        active = other;
        index = 0;
      } else {
        if (otherState == 0) {
          uint32_t expected = 0;
          __atomic_compare_exchange_n(&published[other], &expected, RefillRequested,
                                      false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
        }
        underruns = underruns + 1;
        return lastSample;
      }
    }
    lastSample = buffers[active][index++];
    return lastSample;
  }

  bool refillNeeded(uint8_t k) const {
    return __atomic_load_n(&published[k], __ATOMIC_ACQUIRE) == RefillRequested;
  }

  // Loop side: atomically claim ownership before writing the buffer.
  bool beginRefill(uint8_t k) {
    uint32_t expected = RefillRequested;
    return __atomic_compare_exchange_n(&published[k], &expected, Filling, false,
                                       __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
  }

  // Loop side: after filling buffer k with n samples (write the data first!)
  void markFilled(uint8_t k, uint16_t n) {
    __atomic_store_n(&published[k], static_cast<uint32_t>(n), __ATOMIC_RELEASE);
  }

  uint16_t validCount(uint8_t k) const {
    return static_cast<uint16_t>(__atomic_load_n(&published[k], __ATOMIC_ACQUIRE));
  }
};

#endif
