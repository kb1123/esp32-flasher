// Host test for the signal generator math (firmware-src/bench/wavecore.h).
// g++ -std=c++17 -O2 -I firmware-src/bench tests/wave_test.cpp -o wave_test && ./wave_test
// Runs the exact integer ISR path, low-pass filters the PWM duty the way an RC filter
// would (on the sample stream, i.e. ideal carrier removal), and checks the result.
#include <cmath>
#include <cstdio>
#include <vector>
#include "wavecore.h"

using namespace wave;
static int failures = 0;
static void check(bool ok, const char *fmt, double a = 0, double b = 0, double c = 0) {
  printf("  %s ", ok ? "PASS" : "FAIL"); printf(fmt, a, b, c); printf("\n"); if (!ok) failures++;
}
static int16_t SIN[1 << SINE_BITS];

// volts per sample over n samples (average PWM voltage = duty/FULL * VMAX)
static std::vector<double> run(const Settings &s, size_t n, Chan *out = nullptr) {
  Chan c; toChan(sanitize(s), c); c.phase = c.phase0;
  std::vector<double> v(n);
  for (size_t i = 0; i < n; i++) v[i] = next(c, SIN) / (double)FULL * VMAX;
  if (out) *out = c;
  return v;
}
static double vmin(const std::vector<double> &v) { double m = 1e9; for (double x : v) m = std::min(m, x); return m; }
static double vmax(const std::vector<double> &v) { double m = -1e9; for (double x : v) m = std::max(m, x); return m; }
static double mean(const std::vector<double> &v) { double s = 0; for (double x : v) s += x; return s / v.size(); }

// frequency from rising crossings of the mean over a long window
static double measureHz(const std::vector<double> &v) {
  double m = mean(v); long first = -1, last = -1; int n = 0;
  for (size_t i = 1; i < v.size(); i++) if (v[i - 1] < m && v[i] >= m) { if (first < 0) first = i; last = i; n++; }
  return n > 1 ? (n - 1) * (double)FS / (last - first) : 0;
}
// amplitude of the fundamental (single-bin DFT), and total harmonic content
static void spectrum(const std::vector<double> &v, double hz, double *fund, double *thd) {
  double m = mean(v), re = 0, im = 0, tot = 0;
  for (size_t i = 0; i < v.size(); i++) {
    double x = v[i] - m, a = 2 * M_PI * hz * i / FS;
    re += x * cos(a); im += x * sin(a); tot += x * x;
  }
  double N = v.size();
  *fund = 2 * sqrt(re * re + im * im) / N;                  // peak amplitude
  double rmsAll = sqrt(tot / N), rmsF = *fund / sqrt(2.0);
  *thd = sqrt(std::max(0.0, rmsAll * rmsAll - rmsF * rmsF)) / rmsF;
}

int main() {
  buildSineTable(SIN);
  printf("== constants\n");
  check(PWM_HZ == 312500, "analog PWM carrier %.0f Hz, %.0f duty steps (%.1f mV per step)", PWM_HZ, FULL, VMAX / FULL * 1000);

  printf("== sine: frequency accuracy (1 s windows)\n");
  for (double hz : {0.5, 1.0, 50.0, 123.45, 1000.0, 2000.0}) {
    Settings s; s.on = true; s.wave = SINE; s.hz = hz;
    double secs = hz < 2 ? 8 : 1;
    double got = measureHz(run(s, (size_t)(FS * secs)));
    check(fabs(got - hz) / hz < 0.002, "%.2f Hz requested -> %.4f Hz", hz, got);
  }
  printf("== sine: levels and purity at 1 kHz, 3.3 Vpp centred\n");
  {
    Settings s; s.on = true; s.wave = SINE; s.hz = 1000; s.vpp = 3.3f; s.vmid = 1.65f;
    auto v = run(s, FS);
    double f, thd; spectrum(v, 1000, &f, &thd);
    check(fabs(vmin(v)) < 0.02 && fabs(vmax(v) - 3.3) < 0.02, "swing %.3f .. %.3f V", vmin(v), vmax(v));
    check(fabs(f - 1.65) < 0.01, "fundamental %.4f V peak (expect 1.65)", f);
    check(thd < 0.01, "distortion of the PWM average %.3f%% (8-bit steps)", thd * 100);
  }
  printf("== amplitude / offset\n");
  {
    Settings s; s.on = true; s.wave = SINE; s.hz = 200; s.vpp = 1.0f; s.vmid = 2.0f;
    auto v = run(s, FS);
    check(fabs(vmin(v) - 1.5) < 0.02 && fabs(vmax(v) - 2.5) < 0.02, "1 Vpp around 2 V: %.3f .. %.3f V", vmin(v), vmax(v));
    s.vpp = 3.0f; s.vmid = 2.5f;
    check(clips(s), "3 Vpp around 2.5 V is flagged as clipping");
    v = run(s, FS);
    check(fabs(vmax(v) - 3.3) < 0.02 && fabs(vmin(v) - 1.0) < 0.02, "clipped output stays within rails: %.3f .. %.3f V", vmin(v), vmax(v));
  }
  printf("== triangle, sawtooth, DC\n");
  {
    Settings s; s.on = true; s.wave = TRIANGLE; s.hz = 100; s.vpp = 3.3f; s.vmid = 1.65f;
    auto v = run(s, FS);
    // a triangle's samples are uniformly distributed: mean of |x - mid| = Vpp/4
    double mad = 0; for (double x : v) mad += fabs(x - 1.65); mad /= v.size();
    check(fabs(mad - 3.3 / 4) < 0.01 && fabs(mean(v) - 1.65) < 0.01, "triangle: mean %.3f V, mean deviation %.3f V (expect 0.825)", mean(v), mad);
    s.wave = SAW;
    v = run(s, (size_t)FS / 100 * 3);
    int rises = 0, drops = 0; for (size_t i = 1; i < v.size(); i++) { if (v[i] > v[i - 1]) rises++; if (v[i] < v[i - 1] - 1) drops++; }
    check(drops == 2 || drops == 3, "sawtooth: ramps up, %.0f sharp drops in 3 cycles", drops);
    s.wave = DC; s.vmid = 1.0f;
    v = run(s, 1000);
    check(fabs(vmin(v) - 1.0) < 0.015 && vmax(v) == vmin(v), "DC 1.000 V -> %.4f V, steady", vmin(v));
  }
  printf("== phase offsets (3-phase at 50 Hz)\n");
  {
    double t0[3];
    for (int k = 0; k < 3; k++) {
      Settings s; s.on = true; s.wave = SINE; s.hz = 50; s.phaseDeg = 120.0f * k;
      auto v = run(s, FS / 25);
      // time of first rising crossing of mid
      for (size_t i = 1; i < v.size(); i++) if (v[i - 1] < 1.65 && v[i] >= 1.65) { t0[k] = i / (double)FS; break; }
    }
    double d1 = fmod((t0[0] - t0[1]) * 50 * 360 + 720, 360), d2 = fmod((t0[0] - t0[2]) * 50 * 360 + 720, 360);
    check(fabs(d1 - 120) < 1 && fabs(d2 - 240) < 1, "OUT2 leads OUT1 by %.1f deg, OUT3 by %.1f deg", d1, d2);
  }
  printf("== software square (below 5 Hz)\n");
  {
    Settings s; s.on = true; s.wave = SQUARE; s.hz = 2; s.dutyPct = 25;
    Chan c; auto v = run(s, FS * 2, &c);
    double hi = 0; for (double x : v) hi += x > 1.65;
    check(vmin(v) == 0 && fabs(vmax(v) - 3.3) < 1e-6, "full logic swing: duty 0%% and 100%% (%.2f / %.2f V)", vmin(v), vmax(v));
    check(fabs(hi / v.size() - 0.25) < 0.002, "duty %.2f%% (asked 25%%)", hi / v.size() * 100);
    check(!usesHwSquare(s), "2 Hz stays in software");
  }
  printf("== hardware square timing\n");
  {
    double worst = 0, worstHz = 0;
    for (double hz = 5; hz <= 20000; hz *= 1.013) {
      HwTiming t = hwSquare(hz, 50);
      if (!t.ok) { check(false, "no timer setting for %.2f Hz", hz); break; }
      double err = fabs(t.actualHz - hz) / hz; if (err > worst) { worst = err; worstHz = hz; }
      if (t.div256 < 256 || t.div256 > 0x3FFFF) { check(false, "divider out of range at %.2f Hz", hz); break; }
    }
    check(worst < 0.002, "5 Hz .. 20 kHz: worst frequency error %.3f%% (at %.0f Hz)", worst * 100, worstHz);
    HwTiming k = hwSquare(1000, 50);
    check(k.ok && fabs(k.actualHz - 1000) < 1e-6 && k.bits == 14, "1 kHz exact: %.6f Hz, %.0f-bit duty", k.actualHz, k.bits);
    HwTiming d = hwSquare(1000, 10);
    check(d.dutyCounts == 1638, "10%% duty -> %.0f of 16384 counts", d.dutyCounts);
    check(!hwSquare(4.5, 50).ok && hwSquare(5, 50).ok, "5 Hz is the slowest hardware square");
    HwTiming h = hwSquare(20000, 50);
    check(h.ok && h.bits >= 10, "20 kHz still has %.0f-bit duty resolution", h.bits);
  }
  printf("== frequency changes are glitch-free (phase continuous)\n");
  {
    Settings s; s.on = true; s.wave = SINE; s.hz = 300;
    Chan c; toChan(s, c);
    for (int i = 0; i < 777; i++) next(c, SIN);
    uint32_t before = c.phase;
    s.hz = 900; toChan(s, c);
    check(c.phase == before, "phase kept across a frequency change (%.0f)", c.phase == before);
  }
  printf(failures ? "\n%d FAILED\n" : "\nALL PASSED\n", failures);
  return failures ? 1 : 0;
}
