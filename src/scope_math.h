#ifndef SCOPE_MATH_H
#define SCOPE_MATH_H

// Triggered-scope state machine: pure logic driven one sample at a time from
// the capture ISR, no hardware dependencies, unit-tested natively.
//
// Arm -> the machine first waits for a sample on the "wrong" side of the
// trigger level (priming - prevents an instant trigger when the signal is
// already past the level), then fires on the first crossing in the requested
// direction. After the trigger it counts down a post-trigger sample budget;
// when that reaches zero the capture ring freezes, leaving the trigger point
// a known distance from the end of the record - everything earlier in the
// ring is pre-trigger history for free.

#include <stdint.h>

enum ScopeState : uint8_t {
  ScopeIdle = 0,
  ScopeArmed = 1,
  ScopeTriggered = 2,
  ScopeComplete = 3,
};

struct ScopeMachine {
  uint8_t state = ScopeIdle;
  uint16_t level = 2048;   // trigger level, raw ADC counts
  bool fallingEdge = false;
  bool primed = false;     // seen a sample on the pre-crossing side
  uint32_t postTotal = 0;  // samples to keep after the trigger sample
  uint32_t postRemaining = 0;
};

inline void scopeArm(ScopeMachine &m, uint16_t levelCounts, bool falling, uint32_t postSamples) {
  m.level = levelCounts;
  m.fallingEdge = falling;
  m.primed = false;
  m.postTotal = postSamples;
  m.postRemaining = 0;
  m.state = ScopeArmed;
}

inline void scopeDisarm(ScopeMachine &m) {
  m.state = ScopeIdle;
  m.primed = false;
}

// Feed one sample; returns true exactly once, on the sample where the capture
// should freeze. Rising = first sample >= level after one < level;
// falling mirrors that.
inline bool scopeStep(ScopeMachine &m, uint16_t sample) {
  if (m.state == ScopeArmed) {
    const bool below = sample < m.level;
    const bool preSide = m.fallingEdge ? !below : below;
    if (preSide) {
      m.primed = true;
    } else if (m.primed) {
      m.state = ScopeTriggered;
      m.postRemaining = m.postTotal;
      if (m.postRemaining == 0) {
        m.state = ScopeComplete;
        return true;
      }
    }
    return false;
  }
  if (m.state == ScopeTriggered) {
    if (--m.postRemaining == 0) {
      m.state = ScopeComplete;
      return true;
    }
  }
  return false;
}

// Position of the trigger sample within a window of the most recent `count`
// samples of a completed record (the trigger sits postSamples before the
// last sample). 0xFFFFFFFF when there is no trigger inside the window.
inline uint32_t scopeTrigOffset(bool triggered, uint32_t postSamples, uint32_t count) {
  if (!triggered || count == 0 || postSamples >= count) {
    return 0xFFFFFFFFu;
  }
  return count - 1 - postSamples;
}

// Millivolts at the pin -> raw 12-bit counts (3.3V reference), for trigger
// level entry in the UI
inline uint16_t scopeLevelCounts(uint32_t millivolts) {
  if (millivolts > 3300) {
    millivolts = 3300;
  }
  return static_cast<uint16_t>((millivolts * 4095u + 1650) / 3300);
}

#endif
