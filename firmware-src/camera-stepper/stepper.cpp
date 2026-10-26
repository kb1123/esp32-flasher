#include "stepper.h"
#include "driver/gptimer.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_struct.h"

// Timing model
//   * A 40 MHz hardware timer fires once per STEP pulse. It reloads itself in
//     hardware, so pulse spacing doesn't drift with interrupt latency.
//   * A 1 kHz ramp (esp_timer task) moves the speed toward the target within the
//     acceleration limit and decides the microstep mode (motion.h).
//   * Flash writes (settings, firmware updates) pause this timer interrupt on the
//     ESP32-S3, so the app refuses them while the motor turns (see busy()).

namespace stepper {

using namespace motion;

static Shared sh;
static Ramp ramp;
static Settings cur;
static volatile bool cmdRun = false;
static int stepPin = -1, ms2Pin = -1;
static gptimer_handle_t tmr = nullptr;
static esp_timer_handle_t rampTimer = nullptr;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static int64_t lastUs = 0;

// telemetry (ramp task writes, web handlers read)
static volatile float measured = 0, pulseHz = 0;
static uint32_t lastPosQ = 0, lastSteps = 0;
static int64_t lastMeasUs = 0;
static uint32_t seenSwitches = 0;
static volatile uint8_t lastSwitchTo = FULL;
static volatile float lastSwitchRpm = 0;
static volatile int64_t lastSwitchUs = -1;

static bool IRAM_ATTR onAlarm(gptimer_handle_t t, const gptimer_alarm_event_data_t *, void *) {
  gpio_ll_set_level(&GPIO, stepPin, 1);
  bool ms2Changed;
  uint32_t next = onStep(sh, &ms2Changed);
  if (ms2Changed) gpio_ll_set_level(&GPIO, ms2Pin, sh.mode == QUARTER);  // latched at the NEXT step edge
  esp_rom_delay_us(2);                                                    // A4988 needs >= 1 us high
  gpio_ll_set_level(&GPIO, stepPin, 0);
  static gptimer_alarm_config_t a = {};
  a.alarm_count = next;
  a.reload_count = 0;
  a.flags.auto_reload_on_alarm = true;
  gptimer_set_alarm_action(t, &a);
  return false;
}

static void startTimer() {
  gpio_ll_set_level(&GPIO, ms2Pin, settleModeWhileStopped(sh));
  static gptimer_alarm_config_t a = {};
  a.alarm_count = firstPeriod(sh);
  a.reload_count = 0;
  a.flags.auto_reload_on_alarm = true;
  gptimer_set_raw_count(tmr, 0);
  gptimer_set_alarm_action(tmr, &a);
  gptimer_start(tmr);
}

static void rampCb(void *) {
  int64_t now = esp_timer_get_time();
  float dt = (now - lastUs) / 1e6f;
  lastUs = now;
  if (dt <= 0 || dt > 0.02f) dt = 0.001f;

  portENTER_CRITICAL(&lock);
  ramp.target = cmdRun ? cur.speed : 0;
  ramp.accel = cur.accel;
  ramp.ms = cur.ms;
  ramp.autoRpm = cur.autoRpm;
  ramp.spr = cur.spr;
  portEXIT_CRITICAL(&lock);

  Action a = rampTick(ramp, sh, dt);
  if (a == START) startTimer();
  else if (a == STOP) gptimer_stop(tmr);
  if (!ramp.running) gpio_ll_set_level(&GPIO, ms2Pin, settleModeWhileStopped(sh));

  uint32_t sw = sh.switches;
  if (sw != seenSwitches) {
    seenSwitches = sw;
    lastSwitchTo = sh.mode;
    lastSwitchRpm = ramp.rpm;
    lastSwitchUs = now;
  }
  if (now - lastMeasUs >= 250000) {   // measured speed from pulses actually issued
    uint32_t p = sh.posQ, n = sh.steps;
    float secs = (now - lastMeasUs) / 1e6f;
    measured = (uint32_t)(p - lastPosQ) / (ramp.spr * 4.0f) / secs * 60.0f;
    pulseHz = (uint32_t)(n - lastSteps) / secs;
    lastPosQ = p; lastSteps = n; lastMeasUs = now;
  }
}

bool begin(int step, int ms2, const Settings &s) {
  stepPin = step; ms2Pin = ms2;
  cur = s;
  pinMode(stepPin, OUTPUT); digitalWrite(stepPin, LOW);
  pinMode(ms2Pin, OUTPUT);
  uint8_t m0 = (s.ms == MS_OFF) ? FULL : QUARTER;   // power-up position is a full-step position
  sh.mode = m0;
  sh.wantMode = m0;
  digitalWrite(ms2Pin, sh.mode == QUARTER);

  gptimer_config_t tc = {};
  tc.clk_src = GPTIMER_CLK_SRC_DEFAULT;
  tc.direction = GPTIMER_COUNT_UP;
  tc.resolution_hz = TICK_HZ;
  tc.intr_priority = 3;   // highest C-level priority: keeps pulse jitter low next to Wi-Fi
  if (gptimer_new_timer(&tc, &tmr) != ESP_OK) return false;
  gptimer_event_callbacks_t cbs = {};
  cbs.on_alarm = onAlarm;
  if (gptimer_register_event_callbacks(tmr, &cbs, nullptr) != ESP_OK) return false;  // ISR lands on this core
  if (gptimer_enable(tmr) != ESP_OK) return false;

  esp_timer_create_args_t ra = {};
  ra.callback = rampCb;
  ra.name = "ramp";
  lastUs = lastMeasUs = esp_timer_get_time();
  if (esp_timer_create(&ra, &rampTimer) != ESP_OK) return false;
  return esp_timer_start_periodic(rampTimer, 1000) == ESP_OK;
}

void run(bool on) { cmdRun = on; }

void apply(const Settings &s) {
  portENTER_CRITICAL(&lock);
  uint16_t spr = cur.spr;
  cur = s;
  cur.speed = constrain(s.speed, MIN_RPM, MAX_RPM);
  cur.accel = constrain(s.accel, 10.0f, 20000.0f);
  cur.autoRpm = constrain(s.autoRpm, MIN_RPM, MAX_RPM);
  if (ramp.running) cur.spr = spr;  // changing steps/rev mid-run would jump the speed
  portEXIT_CRITICAL(&lock);
}

Settings settings() {
  portENTER_CRITICAL(&lock);
  Settings s = cur;
  portEXIT_CRITICAL(&lock);
  return s;
}

Status status() {
  Status st;
  st.cmdRun = cmdRun;
  st.running = ramp.running;
  st.rpm = ramp.running ? ramp.rpm : 0;
  st.measuredRpm = ramp.running ? measured : 0;
  st.pulseHz = ramp.running ? pulseHz : 0;
  st.mode = sh.mode;
  st.wantMode = sh.wantMode;
  st.switches = sh.switches;
  st.lastSwitchTo = lastSwitchTo;
  st.lastSwitchRpm = lastSwitchRpm;
  int64_t at = lastSwitchUs;
  st.lastSwitchAgoMs = at < 0 ? UINT32_MAX : (uint32_t)((esp_timer_get_time() - at) / 1000);
  return st;
}

bool busy() { return ramp.running || cmdRun; }

}  // namespace stepper
