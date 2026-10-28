// ===== bench: camera + A4988 stepper + 3-channel signal generator =====
// Waveshare ESP32-S3-ETH. Join the board's hotspot; the sign-in page has three tabs:
//   Camera   live feed
//   Motor    speed 20-1000 rpm, acceleration limit, microstepping off / 1/4 / auto
//   Signals  OUT1-3: sine, triangle, sawtooth, square, DC
// The firmware update page (and "switch back to the other firmware") is at /update.
//
// Wiring (A4988):  STEP -> IO3 (header pin 14) or IO17 (header pin 7), selectable in the page
//                  MS2  -> IO16 (header pin 9)   high = 1/4 step, low = full step
// Signals:         OUT1 -> IO43 (header pin 20), OUT2 -> IO44 (header pin 19),
//                  OUT3 -> IO17 (header pin 7), only while STEP is on IO3
//                  Sine/triangle/saw/DC are 312.5 kHz PWM: add an RC low-pass per output.
// IO3 is also the camera's clock (XCLK): with STEP on IO3 the camera is switched off.

#include <OtaCore.h>
#include <Preferences.h>
#include "camera.h"
#include "stepper.h"
#include "siggen.h"
#include "page.h"

OTA_FIRMWARE("bench", "1.0.0");

static const int MS2_PIN = 16;
static const int STEP_PIN_CHOICES[] = {3, 17};

static Preferences motorPrefs, sigPrefs;
static int stepPin = 3;
static bool motorDirty = false;
static uint32_t rebootAt = 0;

static const char *busyReason() {
  return stepper::busy() ? "Stop the motor first: writing to flash briefly pauses the step pulses, which would stall it." : nullptr;
}

// ---------------------------------------------------------------- motor settings
static const char *msName(uint8_t m) { return m == motion::MS_OFF ? "off" : m == motion::MS_AUTO ? "auto" : "on"; }

static stepper::Settings loadMotor() {
  stepper::Settings s;
  s.speed = motorPrefs.getFloat("spd", 200);
  s.accel = motorPrefs.getFloat("acc", 500);
  s.ms = motorPrefs.getUChar("ms", motion::MS_ON);
  s.autoRpm = motorPrefs.getFloat("auto", 300);
  s.spr = motorPrefs.getUShort("spr", 200);
  if (s.ms > motion::MS_AUTO) s.ms = motion::MS_ON;
  if (s.spr != 200 && s.spr != 400) s.spr = 200;
  return s;
}

static void saveMotor() {
  stepper::Settings s = stepper::settings();
  motorPrefs.putFloat("spd", s.speed);
  motorPrefs.putFloat("acc", s.accel);
  motorPrefs.putUChar("ms", s.ms);
  motorPrefs.putFloat("auto", s.autoRpm);
  motorPrefs.putUShort("spr", s.spr);
}

static String motorJson() {
  stepper::Settings s = stepper::settings();
  stepper::Status st = stepper::status();
  String j = "{";
  j += "\"run\":" + String(st.cmdRun ? "true" : "false");
  j += ",\"running\":" + String(st.running ? "true" : "false");
  j += ",\"rpm\":" + String(st.rpm, 1);
  j += ",\"measured\":" + String(st.measuredRpm, 1);
  j += ",\"pulseHz\":" + String(st.pulseHz, 0);
  j += ",\"mode\":\"" + String(st.mode == motion::QUARTER ? "quarter" : "full") + "\"";
  j += ",\"pending\":" + String(st.wantMode != st.mode ? "true" : "false");
  j += ",\"switches\":" + String(st.switches);
  if (st.lastSwitchAgoMs == UINT32_MAX) j += ",\"last\":null";
  else j += ",\"last\":{\"to\":\"" + String(st.lastSwitchTo == motion::QUARTER ? "quarter" : "full") +
            "\",\"rpm\":" + String(st.lastSwitchRpm, 1) + ",\"ago\":" + String(st.lastSwitchAgoMs) + "}";
  j += ",\"speed\":" + String(s.speed, 0) + ",\"accel\":" + String(s.accel, 0);
  j += ",\"ms\":\"" + String(msName(s.ms)) + "\",\"autoRpm\":" + String(s.autoRpm, 0);
  j += ",\"spr\":" + String(s.spr) + ",\"stepPin\":" + String(stepPin) + ",\"ms2Pin\":" + String(MS2_PIN);
  j += ",\"min\":" + String(motion::MIN_RPM, 0) + ",\"max\":" + String(motion::MAX_RPM, 0);
  j += ",\"camOff\":" + String(camera::enabled() ? "false" : "true");
  j += ",\"fw\":\"" + String(OTA_FW_NAME) + " " + OTA_FW_VERSION + "\"";
  j += "}";
  return j;
}

static void sendJson(int code, const String &j) {
  otaServer().sendHeader("Cache-Control", "no-store");
  otaServer().send(code, "application/json", j);
}

static void handleMotorGet() {
  if (!otaAuthorize()) return;
  sendJson(200, motorJson());
}

static void handleMotorSet() {
  if (!otaAuthorize()) return;
  WebServer &srv = otaServer();
  stepper::Settings s = stepper::settings();
  if (srv.hasArg("speed")) s.speed = srv.arg("speed").toFloat();
  if (srv.hasArg("accel")) s.accel = srv.arg("accel").toFloat();
  if (srv.hasArg("autoRpm")) s.autoRpm = srv.arg("autoRpm").toFloat();
  if (srv.hasArg("ms")) {
    String m = srv.arg("ms");
    s.ms = m == "off" ? motion::MS_OFF : m == "auto" ? motion::MS_AUTO : motion::MS_ON;
  }
  if (srv.hasArg("spr")) {
    int spr = srv.arg("spr").toInt();
    if (stepper::busy()) { sendJson(409, "{\"ok\":false,\"error\":\"Stop the motor to change steps per revolution.\"}"); return; }
    if (spr == 200 || spr == 400) s.spr = spr;
  }
  stepper::apply(s);
  motorDirty = true;                                   // saved once the motor is stopped
  if (srv.hasArg("run")) stepper::run(srv.arg("run") == "1");
  sendJson(200, motorJson());
}

static void handleStepPin() {
  if (!otaAuthorize()) return;
  WebServer &srv = otaServer();
  int pin = srv.arg("pin").toInt();
  bool valid = false;
  for (int p : STEP_PIN_CHOICES) valid |= (p == pin);
  if (!valid) { sendJson(400, "{\"ok\":false,\"error\":\"STEP can be IO3 or IO17\"}"); return; }
  if (stepper::busy()) { sendJson(409, "{\"ok\":false,\"error\":\"Stop the motor first.\"}"); return; }
  motorPrefs.putUChar("pin", pin);
  saveMotor();
  sendJson(200, "{\"ok\":true,\"reboot\":true}");
  rebootAt = millis() + 800;                           // the camera pin claim is decided at boot
}

static void handlePage() {
  if (!otaAuthorize()) return;
  otaServer().sendHeader("Cache-Control", "no-store");
  otaServer().send_P(200, "text/html", PAGE_HTML);
}

// ---------------------------------------------------------------- signal outputs
static const char *WAVE_NAMES[] = {"sine", "triangle", "saw", "square", "dc"};
static const uint32_t SIG_PREFS_VERSION = 1;

static void loadSignals(wave::Settings out[siggen::N]) {
  if (sigPrefs.getUInt("ver", 0) == SIG_PREFS_VERSION &&
      sigPrefs.getBytesLength("ch") == sizeof(wave::Settings) * siggen::N) {
    sigPrefs.getBytes("ch", out, sizeof(wave::Settings) * siggen::N);
    for (int i = 0; i < siggen::N; i++) out[i] = wave::sanitize(out[i]);
    return;
  }
  for (int i = 0; i < siggen::N; i++) out[i] = wave::Settings();   // all off
}

static void saveSignals() {
  wave::Settings s[siggen::N];
  for (int i = 0; i < siggen::N; i++) s[i] = siggen::get(i);
  sigPrefs.putBytes("ch", s, sizeof(s));
  sigPrefs.putUInt("ver", SIG_PREFS_VERSION);
}

static String sigJson() {
  String j = "{\"fs\":" + String(wave::FS) + ",\"pwmHz\":" + String(wave::PWM_HZ) + ",\"vmax\":" + String(wave::VMAX, 2);
  j += ",\"analogMax\":" + String(wave::ANALOG_MAX_HZ, 0) + ",\"squareMax\":" + String(wave::SQUARE_MAX_HZ, 0) + ",\"min\":" + String(wave::MIN_HZ, 1);
  j += ",\"stepPin\":" + String(stepPin) + ",\"ch\":[";
  for (int i = 0; i < siggen::N; i++) {
    const siggen::Output &o = siggen::output(i);
    wave::Settings s = siggen::get(i);
    if (i) j += ",";
    j += "{\"id\":" + String(i + 1) + ",\"pin\":" + String(o.pin) + ",\"header\":" + String(o.header);
    j += ",\"available\":" + String(o.available ? "true" : "false");
    j += ",\"on\":" + String(s.on ? "true" : "false") + ",\"wave\":\"" + WAVE_NAMES[s.wave] + "\"";
    j += ",\"hz\":" + String(s.hz, 3) + ",\"vpp\":" + String(s.vpp, 3) + ",\"vmid\":" + String(s.vmid, 3);
    j += ",\"phase\":" + String(s.phaseDeg, 1) + ",\"duty\":" + String(s.dutyPct, 1);
    j += ",\"hw\":" + String(siggen::hardwareSquare(i) ? "true" : "false");
    j += ",\"clips\":" + String(wave::clips(s) ? "true" : "false");
    j += ",\"actualHz\":" + String(siggen::actualHz(i), 4) + "}";
  }
  j += "]}";
  return j;
}

static uint8_t waveByName(const String &n, uint8_t dflt) {
  for (uint8_t k = 0; k <= wave::DC; k++) if (n == WAVE_NAMES[k]) return k;
  return dflt;
}

static void applyPreset(const String &name) {
  wave::Settings s[siggen::N];
  for (int i = 0; i < siggen::N; i++) s[i] = siggen::get(i);
  if (name == "3phase") {
    for (int i = 0; i < siggen::N; i++) { s[i] = wave::Settings(); s[i].on = true; s[i].wave = wave::SINE; s[i].hz = 50; s[i].phaseDeg = 120.0f * i; }
  } else if (name == "quadrature") {
    for (int i = 0; i < 2; i++) { s[i] = wave::Settings(); s[i].on = true; s[i].wave = wave::SINE; s[i].hz = 1000; s[i].phaseDeg = 90.0f * i; }
  } else if (name == "test1k") {
    const uint8_t w[] = {wave::SINE, wave::SQUARE, wave::TRIANGLE};
    for (int i = 0; i < siggen::N; i++) { s[i] = wave::Settings(); s[i].on = true; s[i].wave = w[i]; s[i].hz = 1000; }
  } else if (name == "off") {
    for (int i = 0; i < siggen::N; i++) s[i].on = false;
  } else return;
  for (int i = 0; i < siggen::N; i++) siggen::set(i, s[i], false);
  siggen::resync();
}

static void handleSigGet() {
  if (!otaAuthorize()) return;
  sendJson(200, sigJson());
}

static void handleSigSet() {
  if (!otaAuthorize()) return;
  WebServer &srv = otaServer();
  String action = srv.arg("action");
  if (action == "save") {
    if (stepper::busy()) { sendJson(409, "{\"ok\":false,\"error\":\"Stop the motor first: saving pauses the step pulses.\"}"); return; }
    saveSignals();
  } else if (action == "sync") {
    siggen::resync();
  } else if (action == "preset") {
    applyPreset(srv.arg("name"));
  } else {
    int i = srv.arg("ch").toInt() - 1;
    if (i < 0 || i >= siggen::N) { sendJson(400, "{\"ok\":false,\"error\":\"ch must be 1-3\"}"); return; }
    if (!siggen::output(i).available) { sendJson(409, "{\"ok\":false,\"error\":\"OUT3 shares IO17 with the motor's STEP. Move STEP to IO3 to use it.\"}"); return; }
    wave::Settings s = siggen::get(i);
    if (srv.hasArg("on")) s.on = srv.arg("on") == "1";
    if (srv.hasArg("wave")) s.wave = waveByName(srv.arg("wave"), s.wave);
    if (srv.hasArg("hz")) s.hz = srv.arg("hz").toFloat();
    if (srv.hasArg("vpp")) s.vpp = srv.arg("vpp").toFloat();
    if (srv.hasArg("vmid")) s.vmid = srv.arg("vmid").toFloat();
    if (srv.hasArg("phase")) s.phaseDeg = srv.arg("phase").toFloat();
    if (srv.hasArg("duty")) s.dutyPct = srv.arg("duty").toFloat();
    siggen::set(i, s, false);
  }
  sendJson(200, sigJson());
}

// ---------------------------------------------------------------- LED
// amber = motor turning, cyan = camera being watched, red = camera fitted but failed,
// green = on Ethernet/home Wi-Fi, blue = hotspot only
static void statusLed() {
  static uint32_t last = 0;
  if (otaUpdateRunning() || millis() - last < 40) return;
  last = millis();
  uint8_t phase = (millis() / 12) & 0xFF;
  uint8_t lvl = 2 + (phase < 128 ? phase : 255 - phase) / 8;
  if (stepper::busy())                         otaSetLed(lvl, lvl / 3, 0);
  else if (camera::viewers())                  otaSetLed(0, lvl, lvl);
  else if (camera::enabled() && !camera::ok()) otaSetLed(lvl, 0, 0);
  else if (otaNetworkUp())                     otaSetLed(0, lvl, 0);
  else                                         otaSetLed(0, 0, lvl);
}

// ---------------------------------------------------------------- main
void setup() {
  OtaConfig c;
  c.name = OTA_FW_NAME;
  c.version = OTA_FW_VERSION;
  c.appOwnsRoot = true;
  c.ledPin = 21;
  c.ethSck = 13; c.ethMiso = 12; c.ethMosi = 11;
  c.ethCs = 14;  c.ethInt = 10;  c.ethRst = 9;
  c.busyReason = busyReason;
  otaCoreBegin(c);                                     // networking first: nothing below can block updates

  motorPrefs.begin("motor", false);
  stepPin = motorPrefs.getUChar("pin", 3);
  if (stepPin != 3 && stepPin != 17) stepPin = 3;

  camera::begin(stepPin != 3, "Camera off: the motor's STEP signal is using IO3, which is the camera's clock pin.");
  camera::addRoutes();

  if (!stepper::begin(stepPin, MS2_PIN, loadMotor())) Serial.println("[motor] timer setup failed");

  // UART0's pins are free (Serial is the USB port). IO17 is OUT3 unless STEP is using it.
  sigPrefs.begin("sig", false);
  siggen::Output outs[siggen::N] = {{43, 20, true}, {44, 19, true}, {17, 7, stepPin != 17}};
  wave::Settings saved[siggen::N];
  loadSignals(saved);
  if (!siggen::begin(outs, saved)) Serial.println("[sig] timer setup failed");

  WebServer &srv = otaServer();
  srv.on("/", HTTP_GET, handlePage);
  srv.on("/api/motor", HTTP_GET, handleMotorGet);
  srv.on("/api/motor", HTTP_POST, handleMotorSet);
  srv.on("/api/motor/pin", HTTP_POST, handleStepPin);
  srv.on("/api/sig", HTTP_GET, handleSigGet);
  srv.on("/api/sig", HTTP_POST, handleSigSet);
}

void loop() {
  otaCoreLoop();
  bool idle = !stepper::busy();
  if (motorDirty && idle) { motorDirty = false; saveMotor(); }
  camera::loop(idle);
  statusLed();
  if (rebootAt && (int32_t)(millis() - rebootAt) > 0) ESP.restart();
}
