// ===== camera-viewer: live camera feed from the Waveshare ESP32-S3-ETH =====
// Join the board's hotspot and the sign-in page shows the camera.
// The firmware update page (with "switch back to the other firmware") stays at /update.

#include "esp_http_server.h"   // ESP-IDF server for the stream...
#undef HTTP_ANY                 // ...and Arduino's WebServer (via OtaCore) both define HTTP_ANY
#include <OtaCore.h>
#include <Preferences.h>
#include <atomic>
#include "esp_camera.h"
#include "camera_page.h"

OTA_FIRMWARE("camera-viewer", "1.0.0");

// ---------------------------------------------------------------- camera wiring
// Waveshare ESP32-S3-ETH DVP connector (from the Waveshare wiki). GPIO8 gates the
// camera's power and must be driven low, which the driver does when it's set as PWDN.
#define CAM_PIN_PWDN    8
#define CAM_PIN_RESET  -1
#define CAM_PIN_XCLK    3
#define CAM_PIN_SIOD   48
#define CAM_PIN_SIOC   47
#define CAM_PIN_D7     18
#define CAM_PIN_D6     15
#define CAM_PIN_D5     38
#define CAM_PIN_D4     40
#define CAM_PIN_D3     42
#define CAM_PIN_D2     46
#define CAM_PIN_D1     45
#define CAM_PIN_D0     41
#define CAM_PIN_VSYNC   1
#define CAM_PIN_HREF    2
#define CAM_PIN_PCLK   39

static const uint16_t STREAM_PORT = 81;

struct SizeOpt { const char *id; framesize_t fs; const char *label; };
static const SizeOpt SIZES[] = {
  {"qvga", FRAMESIZE_QVGA, "320×240"},   {"vga", FRAMESIZE_VGA, "640×480"},
  {"svga", FRAMESIZE_SVGA, "800×600"},   {"xga", FRAMESIZE_XGA, "1024×768"},
  {"hd",   FRAMESIZE_HD,   "1280×720"},  {"uxga", FRAMESIZE_UXGA, "1600×1200"},
};

static Preferences camPrefs;
static bool camOk = false;
static String camError, sensorName;
static httpd_handle_t streamServer = nullptr;
static std::atomic<int> viewers{0};
static volatile float streamFps = 0;

// ---------------------------------------------------------------- camera
static const SizeOpt *sizeById(const String &id) {
  for (auto &s : SIZES) if (id == s.id) return &s;
  return nullptr;
}

static void applySettings() {
  sensor_t *s = esp_camera_sensor_get();
  if (!s) return;
  const SizeOpt *sz = sizeById(camPrefs.getString("size", "vga"));
  s->set_framesize(s, sz ? sz->fs : FRAMESIZE_VGA);
  s->set_quality(s, camPrefs.getUChar("q", 12));
  s->set_vflip(s, camPrefs.getBool("vflip", false));
  s->set_hmirror(s, camPrefs.getBool("mirror", false));
  s->set_brightness(s, camPrefs.getChar("bright", 0));
}

static void startCamera() {
  camera_config_t c = {};
  c.pin_pwdn = CAM_PIN_PWDN;   c.pin_reset = CAM_PIN_RESET; c.pin_xclk = CAM_PIN_XCLK;
  c.pin_sccb_sda = CAM_PIN_SIOD; c.pin_sccb_scl = CAM_PIN_SIOC;
  c.pin_d7 = CAM_PIN_D7; c.pin_d6 = CAM_PIN_D6; c.pin_d5 = CAM_PIN_D5; c.pin_d4 = CAM_PIN_D4;
  c.pin_d3 = CAM_PIN_D3; c.pin_d2 = CAM_PIN_D2; c.pin_d1 = CAM_PIN_D1; c.pin_d0 = CAM_PIN_D0;
  c.pin_vsync = CAM_PIN_VSYNC; c.pin_href = CAM_PIN_HREF; c.pin_pclk = CAM_PIN_PCLK;
  c.xclk_freq_hz = 20000000;
  c.ledc_timer = LEDC_TIMER_0;
  c.ledc_channel = LEDC_CHANNEL_0;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAMESIZE_UXGA;      // allocate for the largest size we offer, then shrink
  c.jpeg_quality = 12;
  c.fb_count = 2;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;   // always show the newest frame, never a stale one

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    camError = String("camera not detected (") + esp_err_to_name(err) + ")";
    Serial.println("[cam] " + camError);
    return;
  }
  sensor_t *s = esp_camera_sensor_get();
  switch (s->id.PID) {
    case OV2640_PID: sensorName = "OV2640"; break;
    case OV3660_PID: sensorName = "OV3660"; break;
    case OV5640_PID: sensorName = "OV5640"; break;
    default: sensorName = "sensor 0x" + String(s->id.PID, HEX);
  }
  applySettings();
  camOk = true;
  Serial.println("[cam] " + sensorName + " ready");
}

// ---------------------------------------------------------------- MJPEG stream (port 81, own task)
#define BOUNDARY "frame7d3e1a"
static esp_err_t streamHandler(httpd_req_t *req) {
  char q[64], t[24];
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK ||
      httpd_query_key_value(q, "t", t, sizeof(t)) != ESP_OK || otaToken() != t) {
    httpd_resp_set_status(req, "403 Forbidden");
    return httpd_resp_send(req, "forbidden", HTTPD_RESP_USE_STRLEN);
  }
  if (!camOk) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_send(req, camError.c_str(), HTTPD_RESP_USE_STRLEN);
  }
  httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=" BOUNDARY);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  viewers++;
  char part[96];
  uint32_t t0 = millis(), frames = 0;
  esp_err_t res = ESP_OK;
  while (res == ESP_OK) {
    if (otaUpdateRunning()) { delay(200); continue; }   // give a firmware upload the bandwidth
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) { delay(20); continue; }
    size_t hl = snprintf(part, sizeof(part), "\r\n--" BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", (unsigned)fb->len);
    res = httpd_resp_send_chunk(req, part, hl);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    if (++frames == 10) {
      uint32_t now = millis();
      streamFps = 10000.0f / max<uint32_t>(1, now - t0);
      t0 = now; frames = 0;
    }
  }
  viewers--;
  if (!viewers) streamFps = 0;
  return res;
}

static void startStreamServer() {
  httpd_config_t c = HTTPD_DEFAULT_CONFIG();
  c.server_port = STREAM_PORT;
  c.ctrl_port = 32769;
  c.max_uri_handlers = 2;
  c.max_open_sockets = 3;
  c.lru_purge_enable = true;      // a phone that vanished mid-stream doesn't lock others out
  c.send_wait_timeout = 3;
  httpd_uri_t u = {};
  u.uri = "/stream"; u.method = HTTP_GET; u.handler = streamHandler;
  if (httpd_start(&streamServer, &c) == ESP_OK) httpd_register_uri_handler(streamServer, &u);
  else Serial.println("[cam] stream server failed to start");
}

// ---------------------------------------------------------------- page + API (port 80, via OtaCore)
static String camJson() {
  sensor_t *s = camOk ? esp_camera_sensor_get() : nullptr;
  String j = "{\"ok\":" + String(camOk ? "true" : "false");
  j += ",\"error\":\"" + camError + "\"";
  j += ",\"sensor\":\"" + sensorName + "\"";
  j += ",\"token\":\"" + otaToken() + "\",\"port\":" + String(STREAM_PORT);
  j += ",\"size\":\"" + camPrefs.getString("size", "vga") + "\"";
  j += ",\"quality\":" + String(camPrefs.getUChar("q", 12));
  j += ",\"vflip\":" + String(s && s->status.vflip ? "true" : "false");
  j += ",\"mirror\":" + String(s && s->status.hmirror ? "true" : "false");
  j += ",\"brightness\":" + String((int)camPrefs.getChar("bright", 0));
  j += ",\"viewers\":" + String(viewers.load()) + ",\"fps\":" + String(streamFps, 1);
  j += ",\"fw\":\"" + String(OTA_FW_NAME) + " " + OTA_FW_VERSION + "\"";
  j += ",\"sizes\":[";
  for (size_t i = 0; i < sizeof(SIZES) / sizeof(SIZES[0]); i++)
    j += String(i ? "," : "") + "{\"id\":\"" + SIZES[i].id + "\",\"label\":\"" + SIZES[i].label + "\"}";
  j += "]}";
  return j;
}

static void handlePage() {
  if (!otaAuthorize()) return;
  WebServer &srv = otaServer();
  srv.sendHeader("Cache-Control", "no-store");
  srv.send_P(200, "text/html", CAMERA_PAGE_HTML);
}

static void handleCamGet() {
  if (!otaAuthorize()) return;
  otaServer().sendHeader("Cache-Control", "no-store");
  otaServer().send(200, "application/json", camJson());
}

static void handleCamSet() {
  if (!otaAuthorize()) return;
  WebServer &srv = otaServer();
  if (srv.hasArg("size") && sizeById(srv.arg("size"))) camPrefs.putString("size", srv.arg("size"));
  if (srv.hasArg("quality")) camPrefs.putUChar("q", constrain(srv.arg("quality").toInt(), 6, 40));
  if (srv.hasArg("vflip")) camPrefs.putBool("vflip", srv.arg("vflip") == "1");
  if (srv.hasArg("mirror")) camPrefs.putBool("mirror", srv.arg("mirror") == "1");
  if (srv.hasArg("brightness")) camPrefs.putChar("bright", constrain(srv.arg("brightness").toInt(), -2, 2));
  if (camOk) applySettings();
  srv.send(200, "application/json", camJson());
}

static void handleCapture() {
  if (!otaAuthorize()) return;
  WebServer &srv = otaServer();
  if (!camOk) { srv.send(503, "text/plain", camError); return; }
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) { srv.send(500, "text/plain", "capture failed"); return; }
  srv.sendHeader("Content-Disposition", "inline; filename=snapshot.jpg");
  srv.sendHeader("Cache-Control", "no-store");
  srv.send_P(200, "image/jpeg", (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

// ---------------------------------------------------------------- LED
// Blue = hotspot only, green = also on Ethernet/home Wi-Fi, cyan = someone's watching,
// red = camera missing (the update page still works).
static void statusLed() {
  static uint32_t last = 0;
  if (otaUpdateRunning() || millis() - last < 40) return;
  last = millis();
  uint8_t phase = (millis() / 12) & 0xFF;
  uint8_t lvl = 2 + (phase < 128 ? phase : 255 - phase) / 8;
  if (!camOk)              otaSetLed(lvl, 0, 0);
  else if (viewers)        otaSetLed(0, lvl, lvl);
  else if (otaNetworkUp()) otaSetLed(0, lvl, 0);
  else                     otaSetLed(0, 0, lvl);
}

// ---------------------------------------------------------------- main
void setup() {
  OtaConfig c;
  c.name = OTA_FW_NAME;
  c.version = OTA_FW_VERSION;
  c.appOwnsRoot = true;                            // "/" is the camera page; updates at /update
  c.ledPin = 21;
  c.ethSck = 13; c.ethMiso = 12; c.ethMosi = 11;
  c.ethCs = 14;  c.ethInt = 10;  c.ethRst = 9;
  otaCoreBegin(c);                                 // networking first: a camera problem never blocks updates

  camPrefs.begin("cam", false);
  startCamera();
  startStreamServer();

  WebServer &srv = otaServer();
  srv.on("/", HTTP_GET, handlePage);
  srv.on("/api/cam", HTTP_GET, handleCamGet);
  srv.on("/api/cam", HTTP_POST, handleCamSet);
  srv.on("/capture", HTTP_GET, handleCapture);
}

void loop() {
  otaCoreLoop();
  statusLed();
}
