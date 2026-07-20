#ifndef PI_CONTROLLER_H
#define PI_CONTROLLER_H

// PI controller with clamped-integrator anti-windup: no hardware dependencies,
// unit-tested natively. Runs in loop() context (float is fine there - the
// modulation ISR never touches this).

struct PiController {
  float kp = 0.0f;
  float ki = 0.0f; // per second
  float outMin = 0.0f;
  float outMax = 1.0f;
  float integrator = 0.0f;
};

inline float piUpdate(PiController &c, float error, float dtSeconds) {
  c.integrator += c.ki * error * dtSeconds;
  if (c.integrator > c.outMax) {
    c.integrator = c.outMax;
  } else if (c.integrator < c.outMin) {
    c.integrator = c.outMin;
  }

  float out = c.kp * error + c.integrator;
  if (out > c.outMax) {
    out = c.outMax;
  } else if (out < c.outMin) {
    out = c.outMin;
  }
  return out;
}

inline void piReset(PiController &c, float value = 0.0f) {
  c.integrator = value;
}

#endif
