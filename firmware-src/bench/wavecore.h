// ===== wavecore.h: signal-generator math with no hardware dependencies =====
// Shared by the firmware and the host test (tests/wave_test.cpp).
//
// The ESP32-S3 has no DAC, so analog waveforms are made by a fast PWM carrier
// (312.5 kHz, 8-bit) whose duty cycle is updated FS times per second; a simple
// RC low-pass on the pin turns that into a smooth voltage.
//   * sample()/dutyFor()/advance() run in the sample ISR: integers only.
//   * Everything else (settings -> integer parameters) runs in tasks.
// Square waves at >= HW_SQUARE_MIN_HZ don't use the ISR at all: the channel's
// own LEDC timer is retuned to the requested frequency, giving exact edges.
#pragma once
#include <stdint.h>
#include <math.h>

namespace wave {

constexpr uint32_t FS = 31250;               // samples per second per channel
constexpr uint32_t PWM_BITS = 8;
constexpr uint32_t FULL = 1u << PWM_BITS;    // duty value for 100% (always high)
constexpr uint32_t LEDC_CLK = 80000000;      // APB clock feeding the LEDC timers
constexpr uint32_t PWM_HZ = LEDC_CLK / FULL; // 312.5 kHz analog carrier
constexpr float VMAX = 3.3f;
constexpr float ANALOG_MAX_HZ = 2000;        // sine/triangle/saw: >= 15 samples per cycle
constexpr float SQUARE_MAX_HZ = 20000;
constexpr float MIN_HZ = 0.1f;
constexpr float HW_SQUARE_MIN_HZ = 5;        // slowest the LEDC timer reaches at 14-bit, divider <= 1023
constexpr int SINE_BITS = 10;                // 1024-entry table

enum Wave : uint8_t { SINE = 0, TRIANGLE, SAW, SQUARE, DC };

// ISR-facing state for one output. Written in a critical section, read by the ISR.
struct Chan {
  bool soft = false;   // true: the sample ISR drives this channel's duty
  uint8_t wave = DC;
  uint32_t inc = 0;    // phase increment per sample (2^32 = one cycle)
  uint32_t phase = 0;
  uint32_t phase0 = 0; // phase offset, applied on resync
  uint32_t dutyP = 0;  // square: high while phase < dutyP
  int32_t off = 0;     // centre level, duty units << 16
  int32_t half = 0;    // half the peak-to-peak swing, (duty units << 16) per full-scale sample
};

// User-facing settings for one output.
struct Settings {
  bool on = false;
  uint8_t wave = SINE;
  float hz = 1000;
  float vpp = 3.3f;     // peak-to-peak volts (ignored by square: always 0-3.3 V)
  float vmid = 1.65f;   // centre volts (for DC: the level)
  float phaseDeg = 0;
  float dutyPct = 50;   // square only
};

inline void buildSineTable(int16_t *t) {
  for (int i = 0; i < (1 << SINE_BITS); i++)
    t[i] = (int16_t)lround(32767.0 * sin(2.0 * M_PI * i / (1 << SINE_BITS)));
}

// Full-scale sample in [-32767, 32767].
inline int32_t sample(const int16_t *sineTab, uint8_t w, uint32_t phase, uint32_t dutyP) {
  switch (w) {
    case SINE: return sineTab[phase >> (32 - SINE_BITS)];
    case TRIANGLE: {
      int32_t q = (int32_t)(phase >> 16);               // 0..65535
      return q < 32768 ? q * 2 - 32767 : 32767 - (q - 32768) * 2;
    }
    case SAW: {
      int32_t s = (int32_t)(phase >> 16) - 32768;
      return s < -32767 ? -32767 : s;
    }
    case SQUARE: return phase < dutyP ? 32767 : -32767;
    default: return 0;
  }
}

inline uint32_t dutyFor(const Chan &c, const int16_t *sineTab) {
  int32_t v = c.off + c.half * sample(sineTab, c.wave, c.phase, c.dutyP);
  int32_t d = (v + 32768) >> 16;
  return d < 0 ? 0 : d > (int32_t)FULL ? FULL : (uint32_t)d;
}

// ISR: duty for this sample, then advance the phase.
inline uint32_t next(Chan &c, const int16_t *sineTab) {
  uint32_t d = dutyFor(c, sineTab);
  c.phase += c.inc;
  return d;
}

// ---------------------------------------------------------------- task side
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

inline float maxHz(uint8_t w) { return w == SQUARE ? SQUARE_MAX_HZ : w == DC ? 0 : ANALOG_MAX_HZ; }

inline Settings sanitize(Settings s) {
  if (s.wave > DC) s.wave = SINE;
  s.hz = clampf(s.hz, MIN_HZ, s.wave == SQUARE ? SQUARE_MAX_HZ : ANALOG_MAX_HZ);
  s.vpp = clampf(s.vpp, 0, VMAX);
  s.vmid = clampf(s.vmid, 0, VMAX);
  s.phaseDeg = fmodf(fmodf(s.phaseDeg, 360) + 360, 360);
  s.dutyPct = clampf(s.dutyPct, 1, 99);
  return s;
}

inline bool clips(const Settings &s) {
  if (s.wave == SQUARE || s.wave == DC) return false;
  return s.vmid - s.vpp / 2 < -0.005f || s.vmid + s.vpp / 2 > VMAX + 0.005f;
}

inline uint32_t incFor(double hz) { return (uint32_t)llround(hz * 4294967296.0 / FS); }
inline double hzFor(uint32_t inc) { return inc * (double)FS / 4294967296.0; }

inline bool usesHwSquare(const Settings &s) { return s.on && s.wave == SQUARE && s.hz >= HW_SQUARE_MIN_HZ; }

// Integer parameters for the ISR. Keeps the current phase so frequency changes are glitch-free.
inline void toChan(const Settings &s, Chan &c) {
  c.wave = s.on ? s.wave : DC;
  c.inc = (s.on && s.wave != DC) ? incFor(s.hz) : 0;
  c.phase0 = (uint32_t)llround(s.phaseDeg / 360.0 * 4294967296.0);
  c.dutyP = (uint32_t)llround(s.dutyPct / 100.0 * 4294967296.0);
  double toDuty = FULL / (double)VMAX * 65536.0;
  if (!s.on) { c.off = 0; c.half = 0; return; }
  if (s.wave == SQUARE) {   // slow software square: full logic swing like the hardware one
    c.off = (int32_t)llround(FULL / 2.0 * 65536.0);
    c.half = (int32_t)llround(FULL / 2.0 * 65536.0 / 32767.0) + 1;   // reaches 0 and FULL after clamping
    return;
  }
  c.off = (int32_t)llround(s.vmid * toDuty);
  c.half = s.wave == DC ? 0 : (int32_t)llround(s.vpp / 2.0 * toDuty / 32767.0);
}

// Hardware square: LEDC timer resolution and 10.8 fixed-point divider for a frequency.
struct HwTiming { bool ok; uint8_t bits; uint32_t div256; double actualHz; uint32_t dutyCounts; };
inline HwTiming hwSquare(double hz, double dutyPct) {
  HwTiming t = {false, 0, 0, 0, 0};
  for (int bits = 14; bits >= 1; bits--) {
    double div = LEDC_CLK / (hz * (double)(1u << bits));
    if (div > 1023.99) return t;               // too slow even at the highest resolution
    if (div < 1.0) continue;                   // too fast for this resolution: try fewer bits
    t.ok = true;
    t.bits = bits;
    t.div256 = (uint32_t)llround(div * 256);
    t.actualHz = LEDC_CLK * 256.0 / ((double)t.div256 * (1u << bits));
    uint32_t top = 1u << bits;
    long d = lround(dutyPct / 100.0 * top);
    t.dutyCounts = d < 1 ? 1 : d > (long)top - 1 ? top - 1 : (uint32_t)d;
    return t;
  }
  return t;
}

}  // namespace wave
