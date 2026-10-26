// Host-side simulation of the stepper motion engine (firmware-src/camera-stepper/motion.h).
// Build & run:  g++ -std=c++17 -O2 -I firmware-src/camera-stepper tests/motion_sim.cpp -o /tmp/motion_sim && /tmp/motion_sim
// Replays exactly what the firmware does: a 1 ms ramp tick plus a timer ISR that fires
// at each step, and checks speed accuracy, the acceleration limit, microstep-switch
// alignment and continuity, auto-switch speeds, and stopping.
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>
#include "motion.h"

using namespace motion;

static int failures = 0;
static void check(bool ok, const char *fmt, double a = 0, double b = 0, double c = 0) {
  printf("  %s ", ok ? "PASS" : "FAIL");
  printf(fmt, a, b, c);
  printf("\n");
  if (!ok) failures++;
}

struct StepRec { uint64_t t; uint8_t mode; uint32_t posQ; float rampRpm; };
struct SwitchRec { double t; uint8_t to; float rpm; };

struct Sim {
  Ramp r;
  Shared s;
  uint64_t t = 0, nextRamp = TICK_HZ / 1000, nextStep = UINT64_MAX;
  bool ms2 = false;
  std::vector<StepRec> steps;
  std::vector<SwitchRec> sw;
  uint32_t lastSwitches = 0;
  bool misaligned = false;

  void run(double seconds, std::function<void(Sim &)> each = nullptr) {
    uint64_t end = t + (uint64_t)(seconds * TICK_HZ);
    while (t < end) {
      if (nextStep <= nextRamp) {
        t = nextStep;
        uint8_t modeAtStep = s.mode;
        bool ch;
        uint32_t p = onStep(s, &ch);
        if (ch) ms2 = (s.mode == QUARTER);
        if (s.mode == FULL && (s.posQ & 3) != 0) misaligned = true;
        steps.push_back({t, modeAtStep, s.posQ, r.rpm});
        nextStep = t + p;
      } else {
        t = nextRamp;
        nextRamp += TICK_HZ / 1000;
        Action a = rampTick(r, s, 0.001f);
        if (a == START) { ms2 = settleModeWhileStopped(s); nextStep = t + firstPeriod(s); }
        if (a == STOP) nextStep = UINT64_MAX;
        if (!r.running) ms2 = settleModeWhileStopped(s);
        if (s.switches != lastSwitches) { lastSwitches = s.switches; sw.push_back({t / (double)TICK_HZ, s.mode, r.rpm}); }
        if (each) each(*this);
      }
    }
  }
  // rpm implied by the interval that ENDS at step i (mode used for that interval = mode at step i-1's exit)
  double rpmAt(size_t i) const {
    uint64_t dt = steps[i].t - steps[i - 1].t;
    uint8_t m = steps[i].mode;  // the pulse at i was issued in this mode
    double perRev = r.spr * (m == FULL ? 1.0 : 4.0);
    return 60.0 * TICK_HZ / (dt * perRev);
  }
  double measuredRpm(double fromSec) const {  // average over steps after fromSec
    size_t a = 0;
    while (a < steps.size() && steps[a].t < fromSec * TICK_HZ) a++;
    const auto &x = steps[a], &y = steps.back();
    return (double)(y.posQ - x.posQ) / (r.spr * 4.0) / ((y.t - x.t) / (double)TICK_HZ) * 60.0;
  }
  // Worst acceleration over any 20 ms span, rpm/s. The ramp updates every 1 ms, so
  // speed is a fine staircase; a 20 ms span can catch 21 stairs (5% over the limit
  // by construction), plus 25 ns timer rounding at the endpoints.
  double worstAccel() const {
    double worst = 0;
    const uint64_t span = TICK_HZ / 50;
    size_t j = 2;
    for (size_t i = 2; i < steps.size(); i++) {
      if (j < i) j = i;
      while (j < steps.size() && steps[j].t - steps[i].t < span) j++;
      if (j >= steps.size()) break;
      double a = std::fabs(rpmAt(j) - rpmAt(i)) / ((steps[j].t - steps[i].t) / (double)TICK_HZ);
      worst = std::max(worst, a);
    }
    return worst;
  }
};

int main() {
  printf("== steady speed, 1/4 step, 200 steps/rev\n");
  for (float rpm : {20.f, 137.f, 600.f, 1000.f}) {
    Sim m; m.r.ms = MS_ON; m.r.accel = 5000; m.r.target = rpm;
    m.run(2.0);
    double got = m.measuredRpm(1.0);
    check(std::fabs(got - rpm) / rpm < 0.003, "target %.0f rpm -> measured %.2f rpm", rpm, got);
  }
  printf("== steady speed, full step, 400 steps/rev at 1000 rpm (fastest pulse rate)\n");
  {
    Sim m; m.r.ms = MS_OFF; m.r.spr = 400; m.r.accel = 5000; m.r.target = 1000;
    m.run(2.0);
    double got = m.measuredRpm(1.0);
    check(std::fabs(got - 1000) < 3, "measured %.2f rpm", got);
  }
  {
    Sim m; m.r.ms = MS_ON; m.r.spr = 400; m.r.accel = 5000; m.r.target = 1000;
    m.run(1.5);
    double hz = 1000.0 * 400 * 4 / 60;
    check(m.s.periodQ == 1500, "1/4 step at 1000 rpm, 400 spr: %.0f pulses/s, period %.0f ticks (37.5 us)", hz, m.s.periodQ);
  }

  printf("== acceleration limit\n");
  for (float acc : {100.f, 500.f, 3000.f}) {
    Sim m; m.r.ms = MS_ON; m.r.accel = acc; m.r.target = 800;
    m.run(800 / acc + 0.5);
    double w = m.worstAccel();
    double t80 = 0;  // time to reach 780 rpm
    for (auto &st : m.steps) if (st.rampRpm >= 780) { t80 = st.t / (double)TICK_HZ; break; }
    double expect = (780 - MIN_RPM) / acc;
    check(w <= acc * 1.06 + 40, "limit %.0f rpm/s: worst over any 20 ms %.0f rpm/s", acc, w);
    check(std::fabs(t80 - expect) < 0.01 + 0.02 * expect, "20 -> 780 rpm took %.3f s (ideal %.3f s)", t80, expect);
  }

  printf("== manual switch while running at 500 rpm\n");
  {
    Sim m; m.r.ms = MS_ON; m.r.accel = 2000; m.r.target = 500;
    m.run(1.0);
    size_t before = m.steps.size();
    uint64_t tReq = m.t;
    m.r.ms = MS_OFF;  // user taps "off"
    m.run(0.5);
    // find switch step
    size_t k = 0;
    for (size_t i = before; i < m.steps.size(); i++) if (m.steps[i].mode == FULL) { k = i; break; }
    double lag = (m.steps[k].t - tReq) / (double)TICK_HZ * 1000;
    double qms = 60.0 / (500 * 200 * 4) * 1000;  // one quarter step at 500 rpm, ms
    // worst case: wait for the 1 ms ramp tick, up to 3 quarter steps to reach a full-step
    // position, then one full step (4 quarter periods) until the first full-step pulse
    check(k && lag <= 1.0 + 7 * qms + 0.01, "quarter -> full: first full-step pulse %.2f ms after the request (worst case %.2f ms)", lag, 1.0 + 7 * qms);
    check(!m.misaligned, "every full step landed on a full-step position");
    double jump = std::fabs(m.rpmAt(k + 1) - m.rpmAt(k - 1));
    check(jump < 1.0, "speed across the switch: %.2f -> %.2f rpm", m.rpmAt(k - 1), m.rpmAt(k + 1));
    m.r.ms = MS_ON;
    size_t b2 = m.steps.size();
    uint64_t tReq2 = m.t;
    m.run(0.5);
    size_t k2 = 0;
    for (size_t i = b2; i < m.steps.size(); i++) if (m.steps[i].mode == QUARTER) { k2 = i; break; }
    double lag2 = (m.steps[k2].t - tReq2) / (double)TICK_HZ * 1000;
    check(k2 && lag2 <= 1.0 + 2 * 4 * qms, "full -> quarter took effect %.2f ms after the request (no alignment wait)", lag2);
    double jump2 = std::fabs(m.rpmAt(k2 + 1) - m.rpmAt(k2 - 1));
    check(jump2 < 1.0, "speed across that switch: %.2f -> %.2f rpm", m.rpmAt(k2 - 1), m.rpmAt(k2 + 1));
    check(std::fabs(m.measuredRpm(1.6) - 500) < 1.5, "still %.2f rpm afterwards", m.measuredRpm(1.6));
  }

  printf("== auto mode: switch at 400 rpm, ramp 20 -> 1000 -> stop at 500 rpm/s\n");
  {
    Sim m; m.r.ms = MS_AUTO; m.r.autoRpm = 400; m.r.accel = 500; m.r.target = 1000;
    m.run(2.2);
    check(m.steps.front().mode == QUARTER, "starts in 1/4 step (mode %.0f)", m.steps.front().mode);
    check(m.sw.size() == 1 && m.sw[0].to == FULL && std::fabs(m.sw[0].rpm - 400) < 2, "switched to full step at %.1f rpm (%.0f switch)", m.sw.size() ? m.sw[0].rpm : -1, m.sw.size());
    m.r.target = 0;
    m.run(2.5);
    check(m.sw.size() == 2 && m.sw[1].to == QUARTER && std::fabs(m.sw[1].rpm - 380) < 2, "switched back at %.1f rpm (expect 380, 5%% hysteresis)", m.sw.size() > 1 ? m.sw[1].rpm : -1);
    check(!m.r.running && m.nextStep == UINT64_MAX, "stopped (running=%.0f)", m.r.running);
    double stopAt = m.steps.back().t / (double)TICK_HZ;
    double expect = 2.2 + (1000 - MIN_RPM) / 500;
    check(std::fabs(stopAt - expect) < 0.05, "last step at %.3f s (expect ~%.3f s)", stopAt, expect);
    check(!m.misaligned, "every full step landed on a full-step position");
    check(m.worstAccel() <= 500 * 1.06 + 40, "worst accel over any 20 ms: %.0f rpm/s (limit 500), whole run incl. both switches", m.worstAccel());
  }

  printf("== auto mode hunting: hold exactly at the switch speed\n");
  {
    Sim m; m.r.ms = MS_AUTO; m.r.autoRpm = 300; m.r.accel = 1000; m.r.target = 300;
    m.run(3.0);
    check(m.sw.size() == 1, "%.0f switch(es) while holding 300 rpm (no flip-flopping)", m.sw.size());
  }

  printf("== stop from 1 / 4 step at an odd position, restart in full step\n");
  {
    Sim m; m.r.ms = MS_ON; m.r.accel = 2000; m.r.target = 100;
    m.run(0.4013);
    m.r.target = 0; m.run(0.3);
    uint32_t pos = m.s.posQ;
    m.r.ms = MS_OFF;
    m.run(0.01);
    bool heldQuarter = (pos & 3) ? m.s.mode == QUARTER : m.s.mode == FULL;
    check(heldQuarter, "stopped at posQ %% 4 = %.0f; mode while stopped = %.0f (stays 1/4 unless aligned)", pos & 3, m.s.mode);
    m.r.target = 200;
    m.run(0.5);
    check(m.s.mode == FULL && !m.misaligned, "restarted and reached full step on an aligned position (mode %.0f)", m.s.mode);
  }

  printf("== target changes mid-ramp\n");
  {
    Sim m; m.r.ms = MS_ON; m.r.accel = 400; m.r.target = 900;
    m.run(1.0);  // ~420 rpm, still accelerating
    m.r.target = 200;
    m.run(2.0);
    check(std::fabs(m.measuredRpm(2.6) - 200) < 1, "settles at %.2f rpm", m.measuredRpm(2.6));
    check(m.worstAccel() <= 400 * 1.06 + 40, "worst accel over any 20 ms: %.0f rpm/s (limit 400)", m.worstAccel());
  }

  printf(failures ? "\n%d FAILED\n" : "\nALL PASSED\n", failures);
  return failures ? 1 : 0;
}
