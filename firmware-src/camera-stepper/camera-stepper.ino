// ===== camera-stepper: camera viewer + A4988 stepper test bench =====
// Waveshare ESP32-S3-ETH. Join the board's hotspot; the sign-in page has two tabs:
//   Camera  live feed (as camera-viewer)
//   Motor   speed 20-1000 rpm, acceleration limit, microstepping off / 1/4 / auto
// The firmware update page (and "switch back to the other firmware") is at /update.
//
// Wiring (A4988):  STEP -> IO3 (header pin 14) or IO17 (header pin 7), selectable in the page
//                  MS2  -> IO16 (header pin 9)   high = 1/4 step, low = full step
//                  MS1, MS3 unconnected (driver pulls them low). DIR/EN are not driven.
// IO3 is also the camera's clock (XCLK): with STEP on IO3 the camera is switched off.

#include <OtaCore.h>
#include <Preferences.h>
#include "camera.h"
#include "stepper.h"
#include "page.h"

OTA_FIRMWARE("camera-stepper", "1.0.0");

static const int MS2_PIN = 16;
static const int STEP_PIN_CHOICES[] = {3, 17};

static Preferences motorPrefs;
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

  WebServer &srv = otaServer();
  srv.on("/", HTTP_GET, handlePage);
  srv.on("/api/motor", HTTP_GET, handleMotorGet);
  srv.on("/api/motor", HTTP_POST, handleMotorSet);
  srv.on("/api/motor/pin", HTTP_POST, handleStepPin);
}

void loop() {
  otaCoreLoop();
  bool idle = !stepper::busy();
  if (motorDirty && idle) { motorDirty = false; saveMotor(); }
  camera::loop(idle);
  statusLed();
  if (rebootAt && (int32_t)(millis() - rebootAt) > 0) ESP.restart();
}
