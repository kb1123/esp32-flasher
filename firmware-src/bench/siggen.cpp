#include "siggen.h"
#include "driver/ledc.h"
#include "driver/gptimer.h"
#include "hal/ledc_ll.h"
#include "soc/ledc_struct.h"

// Output i uses LEDC channel 1+i on LEDC timer 1+i (channel/timer 0 belong to the
// camera's clock). Analog modes run the timer at 312.5 kHz / 8 bits and a 31.25 kHz
// interrupt rewrites the duty. Square >= 5 Hz retunes the channel's own timer to the
// square's frequency, so its edges come from hardware with no interrupt involved.

namespace siggen {

using namespace wave;

static Output outs[N];
static Settings cur[N];
static Chan chans[N];
static bool hwMode[N];
static HwTiming hwT[N];
static int16_t SIN[1 << SINE_BITS];
static volatile bool resyncReq = false;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static gptimer_handle_t sampleTimer = nullptr;

static inline ledc_channel_t chOf(int i) { return (ledc_channel_t)(LEDC_CHANNEL_1 + i); }
static inline ledc_timer_t tmOf(int i) { return (ledc_timer_t)(LEDC_TIMER_1 + i); }

static bool IRAM_ATTR onSample(gptimer_handle_t, const gptimer_alarm_event_data_t *, void *) {
  bool rs = resyncReq;
  for (int i = 0; i < N; i++) {
    Chan &c = chans[i];
    if (!c.soft) continue;
    if (rs) c.phase = c.phase0;
    uint32_t d = next(c, SIN);
    ledc_ll_set_duty_int_part(&LEDC, LEDC_LOW_SPEED_MODE, chOf(i), d);
    ledc_ll_set_duty_start(&LEDC, LEDC_LOW_SPEED_MODE, chOf(i));
    ledc_ll_ls_channel_update(&LEDC, LEDC_LOW_SPEED_MODE, chOf(i));
  }
  if (rs) resyncReq = false;
  return false;
}

static bool timerConfig(int i, uint8_t bits, uint32_t hz) {
  ledc_timer_config_t t = {};
  t.speed_mode = LEDC_LOW_SPEED_MODE;
  t.duty_resolution = (ledc_timer_bit_t)bits;
  t.timer_num = tmOf(i);
  t.freq_hz = hz;
  t.clk_cfg = LEDC_USE_APB_CLK;   // every LEDC timer on the S3 shares one clock source
  return ledc_timer_config(&t) == ESP_OK;
}

static void setDuty(int i, uint32_t duty) {
  ledc_set_duty_with_hpoint(LEDC_LOW_SPEED_MODE, chOf(i), duty, 0);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, chOf(i));
}

static void toAnalogTimer(int i) {
  timerConfig(i, PWM_BITS, PWM_HZ);
  hwMode[i] = false;
}

bool begin(const Output o[N], const Settings saved[N]) {
  buildSineTable(SIN);
  for (int i = 0; i < N; i++) {
    outs[i] = o[i];
    hwMode[i] = false;
    if (!outs[i].available) continue;   // never touch a pin someone else owns
    toAnalogTimer(i);
    ledc_channel_config_t c = {};
    c.gpio_num = outs[i].pin;
    c.speed_mode = LEDC_LOW_SPEED_MODE;
    c.channel = chOf(i);
    c.timer_sel = tmOf(i);
    c.duty = 0;
    c.hpoint = 0;
    c.intr_type = LEDC_INTR_DISABLE;
    if (ledc_channel_config(&c) != ESP_OK) { outs[i].available = false; continue; }
  }

  gptimer_config_t tc = {};
  tc.clk_src = GPTIMER_CLK_SRC_DEFAULT;
  tc.direction = GPTIMER_COUNT_UP;
  tc.resolution_hz = 1000000;
  tc.intr_priority = 2;   // below the motor's step interrupt (3)
  if (gptimer_new_timer(&tc, &sampleTimer) != ESP_OK) return false;
  gptimer_event_callbacks_t cb = {};
  cb.on_alarm = onSample;
  if (gptimer_register_event_callbacks(sampleTimer, &cb, nullptr) != ESP_OK) return false;
  gptimer_alarm_config_t a = {};
  a.alarm_count = 1000000 / FS;   // 32 us
  a.reload_count = 0;
  a.flags.auto_reload_on_alarm = true;
  gptimer_set_alarm_action(sampleTimer, &a);
  if (gptimer_enable(sampleTimer) != ESP_OK || gptimer_start(sampleTimer) != ESP_OK) return false;

  for (int i = 0; i < N; i++) set(i, saved[i], false);
  resync();
  return true;
}

bool set(int i, const Settings &in, bool resyncAfter) {
  if (i < 0 || i >= N) return false;
  Settings s = sanitize(in);
  if (!outs[i].available) { cur[i] = s; cur[i].on = false; return false; }
  bool phaseChanged = s.phaseDeg != cur[i].phaseDeg;
  cur[i] = s;

  portENTER_CRITICAL(&mux);
  chans[i].soft = false;            // the ISR leaves this channel alone while we reconfigure
  portEXIT_CRITICAL(&mux);

  if (usesHwSquare(s)) {
    HwTiming t = hwSquare(s.hz, s.dutyPct);
    uint32_t approx = (uint32_t)lround(t.actualHz);
    if (!hwMode[i] || t.bits != hwT[i].bits || t.div256 != hwT[i].div256) {
      timerConfig(i, t.bits, approx < 1 ? 1 : approx);
      // the driver only takes whole hertz; set the exact 10.8 fixed-point divider ourselves
      ledc_ll_set_clock_divider(&LEDC, LEDC_LOW_SPEED_MODE, tmOf(i), t.div256);
      ledc_ll_ls_timer_update(&LEDC, LEDC_LOW_SPEED_MODE, tmOf(i));
    }
    hwT[i] = t;
    hwMode[i] = true;
    setDuty(i, t.dutyCounts);
  } else {
    if (hwMode[i]) toAnalogTimer(i);
    Chan nc;
    portENTER_CRITICAL(&mux);
    nc = chans[i];
    portEXIT_CRITICAL(&mux);
    toChan(s, nc);                  // keeps the running phase: frequency changes don't glitch
    if (!s.on) setDuty(i, 0);       // off = steady 0 V, no interrupt work
    portENTER_CRITICAL(&mux);
    chans[i] = nc;
    chans[i].soft = s.on;
    portEXIT_CRITICAL(&mux);
  }
  if (resyncAfter || phaseChanged) resync();
  return true;
}

void resync() {
  portENTER_CRITICAL(&mux);
  for (int i = 0; i < N; i++)
    if (outs[i].available && hwMode[i]) ledc_ll_timer_rst(&LEDC, LEDC_LOW_SPEED_MODE, tmOf(i));
  resyncReq = true;                 // the ISR restarts software waves at their phase on its next tick
  portEXIT_CRITICAL(&mux);
}

Settings get(int i) { return cur[i]; }
const Output &output(int i) { return outs[i]; }
bool hardwareSquare(int i) { return outs[i].available && hwMode[i]; }

double actualHz(int i) {
  const Settings &s = cur[i];
  if (!s.on || s.wave == DC || !outs[i].available) return 0;
  if (hwMode[i]) return hwT[i].actualHz;
  return hzFor(incFor(s.hz));
}

}  // namespace siggen
