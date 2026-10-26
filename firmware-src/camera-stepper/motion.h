// ===== motion.h: stepper motion logic with no hardware dependencies =====
// Shared by the firmware and the host-side simulation test (tests/motion_sim.cpp).
//
// Two halves:
//   * rampTick()  runs every ~1 ms in a task. Moves the speed toward the target
//                 within the acceleration limit, decides microstepping, and
//                 publishes the step period. Floating point is fine here.
//   * onStep()    runs in the timer ISR right after each STEP pulse. Integer only
//                 (the ESP32-S3's FPU is off-limits inside ISRs).
//
// Driver: A4988 with only MS2 wired (MS1, MS3 pulled low by the driver):
//   MS2 high = quarter step, MS2 low = full step.
// Position is counted in quarter steps, so a full step adds 4. Switching quarter
// -> full only happens when the position is a multiple of 4, i.e. the rotor sits
// on a full-step position, so the driver's current table stays aligned.
#pragma once
#include <stdint.h>

namespace motion {

constexpr uint32_t TICK_HZ = 40000000;   // step timer resolution (40 MHz; 1 tick = 25 ns)
constexpr float MIN_RPM = 20.0f;         // start/stop speed and lowest settable speed
constexpr float MAX_RPM = 1000.0f;
constexpr float AUTO_HYSTERESIS = 0.95f; // auto mode drops back to 1/4 step below 95% of the switch speed

enum Mode : uint8_t { QUARTER = 0, FULL = 1 };
enum MsSetting : uint8_t { MS_OFF = 0, MS_ON = 1, MS_AUTO = 2 };  // user's microstepping choice
enum Action : uint8_t { NONE, START, STOP };

// Written by the ramp task, read by the ISR (and vice versa for the ISR-owned fields).
struct Shared {
  volatile uint32_t periodQ = 0;    // timer ticks per quarter step at the current speed
  volatile uint8_t wantMode = FULL; // mode the ramp wants; the ISR applies it at a safe position
  // ISR-owned
  volatile uint8_t mode = FULL;     // mode the driver is in right now (MS2 = mode == QUARTER)
  volatile uint32_t posQ = 0;       // position in quarter steps (wraps harmlessly)
  volatile uint32_t steps = 0;      // STEP pulses issued
  volatile uint32_t switches = 0;   // completed mode changes
};

// Task-owned ramp state and user settings.
struct Ramp {
  float target = 0;        // rpm; 0 = stop (decelerate to MIN_RPM, then stop)
  float rpm = 0;           // commanded speed right now
  float accel = 500;       // rpm per second, used for speeding up and slowing down
  float autoRpm = 300;     // MS_AUTO: switch to full step at/above this speed
  uint8_t ms = MS_ON;
  uint16_t spr = 200;      // motor full steps per revolution
  bool running = false;
};

inline uint32_t periodFor(float rpm, uint16_t spr) {
  double hz = (double)rpm * spr * 4.0 / 60.0;       // quarter steps per second
  double t = TICK_HZ / hz;
  return t < 1 ? 1 : (uint32_t)(t + 0.5);
}

inline uint8_t desiredMode(const Ramp &r, uint8_t current) {
  if (r.ms == MS_ON) return QUARTER;
  if (r.ms == MS_OFF) return FULL;
  if (current == QUARTER && r.rpm >= r.autoRpm) return FULL;
  if (current == FULL && r.rpm < r.autoRpm * AUTO_HYSTERESIS) return QUARTER;
  return current;
}

// Call every dt seconds (about 1 ms). Returns START/STOP when the step timer must start/stop.
inline Action rampTick(Ramp &r, Shared &s, float dt) {
  if (!r.running) {
    if (r.target < MIN_RPM) return NONE;
    r.rpm = MIN_RPM;                           // steppers can start instantly at low speed
    r.running = true;
    s.wantMode = desiredMode(r, r.ms == MS_AUTO ? (uint8_t)QUARTER : s.wantMode);
    s.periodQ = periodFor(r.rpm, r.spr);
    return START;
  }
  float goal = r.target >= MIN_RPM ? r.target : MIN_RPM;
  float dv = r.accel * dt;
  if (r.rpm < goal) r.rpm = (r.rpm + dv > goal) ? goal : r.rpm + dv;
  else if (r.rpm > goal) r.rpm = (r.rpm - dv < goal) ? goal : r.rpm - dv;
  if (r.target < MIN_RPM && r.rpm <= MIN_RPM) {
    r.running = false;
    r.rpm = 0;
    return STOP;
  }
  s.periodQ = periodFor(r.rpm, r.spr);
  s.wantMode = desiredMode(r, s.wantMode);
  return NONE;
}

// While stopped: adopt the wanted mode now if it's safe (always safe to go to 1/4
// step; full step only from a full-step position). Returns the MS2 level to output.
inline bool settleModeWhileStopped(Shared &s) {
  if (s.wantMode != s.mode && (s.wantMode == QUARTER || (s.posQ & 3) == 0)) s.mode = s.wantMode;
  return s.mode == QUARTER;
}

// Ticks until the first pulse after START.
inline uint32_t firstPeriod(const Shared &s) { return s.mode == FULL ? s.periodQ * 4 : s.periodQ; }

// ISR, right after a STEP pulse. Returns ticks until the next pulse.
// *ms2Changed is set when MS2 must be rewritten (to: mode == QUARTER).
inline uint32_t onStep(Shared &s, bool *ms2Changed) {
  uint8_t m = s.mode;
  s.posQ = s.posQ + ((m == FULL) ? 4u : 1u);
  s.steps = s.steps + 1;
  *ms2Changed = false;
  uint8_t w = s.wantMode;
  if (w != m && (w == QUARTER || (s.posQ & 3u) == 0)) {
    m = w;
    s.mode = w;
    s.switches = s.switches + 1;
    *ms2Changed = true;
  }
  uint32_t p = s.periodQ;
  return m == FULL ? p * 4u : p;
}

}  // namespace motion
