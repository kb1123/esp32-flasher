// ===== stepper.h: A4988 STEP + MS2 driver built on motion.h =====
#pragma once
#include <Arduino.h>
#include "motion.h"

namespace stepper {

struct Settings {
  float speed = 200;        // rpm the motor runs at when started (20..1000)
  float accel = 500;        // rpm per second
  uint8_t ms = motion::MS_ON;
  float autoRpm = 300;      // MS_AUTO switch speed
  uint16_t spr = 200;       // full steps per revolution
};

struct Status {
  bool cmdRun, running;
  float rpm, measuredRpm, pulseHz;
  uint8_t mode, wantMode;
  uint32_t switches;
  uint8_t lastSwitchTo;       // mode switched into
  float lastSwitchRpm;        // commanded speed when it happened
  uint32_t lastSwitchAgoMs;   // UINT32_MAX = never
};

bool begin(int stepPin, int ms2Pin, const Settings &s);
void run(bool on);            // start (ramp up to speed) or stop (ramp down)
void apply(const Settings &s);// change settings live; spr only while stopped
Settings settings();
Status status();
bool busy();                  // true while the motor is turning (no flash writes then)

}  // namespace stepper
