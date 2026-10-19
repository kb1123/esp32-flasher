#include "ota_core.h"
#include "config.h"
#include "web_ui.h"

#include <ETH.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <Preferences.h>
#include "esp_ota_ops.h"
#include "esp_app_desc.h"

static WebServer server(80);
static Preferences prefs;
static String otaPass;

static volatile bool netUp = false;
static uint32_t netUpSince = 0;
static bool pendingVerify = false;
static bool updating = false;
static bool uploadOk = false;
static String uploadError;
static size_t uploadBytes = 0;
static uint32_t rebootAt = 0;

// ---------------------------------------------------------------- rollback
// Tell the core not to auto-confirm a new OTA image; we confirm it ourselves
// only after it proves it can get back on the network.
bool verifyRollbackLater() { return true; }

static void checkPendingVerify() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
      state == ESP_OTA_IMG_PENDING_VERIFY) {
    pendingVerify = true;
    Serial.println("[ota] new firmware on trial, waiting for network before confirming");
  } else {
    // First USB flash or already-confirmed image: nothing to verify.
    esp_ota_mark_app_valid_cancel_rollback();
  }
}

static void serviceRollback() {
  if (!pendingVerify) return;
  if (netUp && millis() - netUpSince > OTA_CONFIRM_AFTER_MS) {
    esp_ota_mark_app_valid_cancel_rollback();
    pendingVerify = false;
    Serial.println("[ota] firmware confirmed good");
  } else if (millis() > OTA_CONFIRM_DEADLINE_MS) {
    Serial.println("[ota] no network on trial firmware, rolling back");
    delay(100);
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
}

// ---------------------------------------------------------------- LED
void otaSetLed(uint8_t r, uint8_t g, uint8_t b) {
  rgbLedWrite(STATUS_LED_PIN, r, g, b);
}

// ---------------------------------------------------------------- network
static void onNetEvent(arduino_event_id_t event, arduino_event_info_t) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      ETH.setHostname(OTA_HOSTNAME);
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("[eth] link up");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.printf("[eth] IP %s\n", ETH.localIP().toString().c_str());
      netUp = true;
      netUpSince = millis();
      break;
    case ARDUINO_EVENT_ETH_LOST_IP:
    case ARDUINO_EVENT_ETH_DISCONNECTED:
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("[eth] down");
      netUp = false;
      break;
    default: break;
  }
}

bool otaNetworkUp() { return netUp; }
bool otaUpdateRunning() { return updating; }

// ---------------------------------------------------------------- web
static bool auth() {
  if (server.authenticate(OTA_USER, otaPass.c_str())) return true;
  server.requestAuthentication(BASIC_AUTH, "ESP32 OTA");
  return false;
}

static String jsonEscape(const char *s) {
  String o;
  for (; *s; s++) {
    if (*s == '"' || *s == '\\') o += '\\';
    if ((uint8_t)*s >= 0x20) o += *s;
  }
  return o;
}

static void handleStatus() {
  if (!auth()) return;
  const esp_app_desc_t *app = esp_app_get_description();
  const esp_partition_t *run = esp_ota_get_running_partition();
  String j = "{";
  j += "\"fw\":\"" FW_NAME "\",\"ver\":\"" FW_VERSION "\"";
  j += ",\"built\":\"" + jsonEscape(app->date) + " " + jsonEscape(app->time) + "\"";
  j += ",\"idf\":\"" + jsonEscape(app->idf_ver) + "\"";
  j += ",\"partition\":\"" + String(run->label) + "\"";
  j += ",\"trial\":" + String(pendingVerify ? "true" : "false");
  j += ",\"ip\":\"" + ETH.localIP().toString() + "\"";
  j += ",\"mac\":\"" + ETH.macAddress() + "\"";
  j += ",\"host\":\"" OTA_HOSTNAME "\"";
  j += ",\"uptime\":" + String(millis() / 1000);
  j += ",\"heap\":" + String(ESP.getFreeHeap());
  j += ",\"psram\":" + String(ESP.getFreePsram());
  j += ",\"maxApp\":" + String(esp_ota_get_next_update_partition(NULL)->size);
  j += ",\"defaultPass\":" + String(otaPass == OTA_DEFAULT_PASS ? "true" : "false");
  j += "}";
  server.send(200, "application/json", j);
}

// Validate the first chunk: must be an ESP32-S3 *app* image, not a merged
// full-flash image (which starts with the bootloader) or another chip's build.
static bool validateHeader(const uint8_t *buf, size_t len) {
  if (len < 0x24) { uploadError = "file too small"; return false; }
  if (buf[0] != 0xE9) { uploadError = "not an ESP32 firmware image"; return false; }
  uint16_t chip = buf[12] | (buf[13] << 8);
  if (chip != 9 /* ESP_CHIP_ID_ESP32S3 */) {
    uploadError = "firmware is for a different chip (id " + String(chip) + ")";
    return false;
  }
  uint32_t magic = buf[0x20] | (buf[0x21] << 8) | (buf[0x22] << 16) | ((uint32_t)buf[0x23] << 24);
  if (magic != 0xABCD5432) {
    uploadError = "this looks like a full-flash (merged) image; upload the app .bin instead";
    return false;
  }
  return true;
}

static void handleUploadChunk() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    uploadOk = false;
    uploadError = "";
    uploadBytes = 0;
    if (!server.authenticate(OTA_USER, otaPass.c_str())) { uploadError = "unauthorized"; return; }
    updating = true;
    otaSetLed(20, 0, 20);
    Serial.printf("[ota] receiving %s\n", up.filename.c_str());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!updating || uploadError.length()) return;
    if (uploadBytes == 0) {
      if (!validateHeader(up.buf, up.currentSize)) { Serial.println("[ota] rejected: " + uploadError); return; }
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) { uploadError = Update.errorString(); return; }
    }
    if (Update.write(up.buf, up.currentSize) != up.currentSize) { uploadError = Update.errorString(); return; }
    uploadBytes += up.currentSize;
  } else if (up.status == UPLOAD_FILE_END) {
    if (updating && !uploadError.length()) {
      if (uploadBytes == 0) uploadError = "empty file";
      else if (Update.end(true)) uploadOk = true;
      else uploadError = Update.errorString();
    }
    if (!uploadOk && Update.isRunning()) Update.abort();
    updating = false;
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (Update.isRunning()) Update.abort();
    uploadError = "upload aborted";
    updating = false;
  }
}

static void handleUploadDone() {
  if (!auth()) return;
  if (uploadOk) {
    server.send(200, "application/json",
                "{\"ok\":true,\"bytes\":" + String(uploadBytes) + "}");
    Serial.printf("[ota] %u bytes written, rebooting\n", (unsigned)uploadBytes);
    rebootAt = millis() + 800;
  } else {
    otaSetLed(30, 0, 0);
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"" + jsonEscape(uploadError.c_str()) + "\"}");
  }
}

static void handlePassword() {
  if (!auth()) return;
  String np = server.arg("new");
  if (np.length() < 8 || np.length() > 63) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"use 8 to 63 characters\"}");
    return;
  }
  prefs.putString("pass", np);
  otaPass = np;
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleReboot() {
  if (!auth()) return;
  server.send(200, "application/json", "{\"ok\":true}");
  rebootAt = millis() + 500;
}

static void handleRoot() {
  if (!auth()) return;
  server.send_P(200, "text/html", WEB_UI_HTML);
}

// ---------------------------------------------------------------- public
void otaCoreBegin() {
  Serial.begin(115200);
  otaSetLed(0, 0, 25);  // blue: starting, no network yet

  prefs.begin("ota", false);
  otaPass = prefs.getString("pass", OTA_DEFAULT_PASS);

  checkPendingVerify();

  Network.onEvent(onNetEvent);
  if (!ETH.begin(ETH_PHY_W5500, 1, ETH_PIN_CS, ETH_PIN_INT, ETH_PIN_RST,
                 SPI2_HOST, ETH_PIN_SCK, ETH_PIN_MISO, ETH_PIN_MOSI)) {
    Serial.println("[eth] W5500 init failed");
    otaSetLed(30, 0, 0);
  }

  if (MDNS.begin(OTA_HOSTNAME)) MDNS.addService("http", "tcp", 80);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/update", HTTP_POST, handleUploadDone, handleUploadChunk);
  server.on("/api/password", HTTP_POST, handlePassword);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();
}

void otaCoreLoop() {
  server.handleClient();
  serviceRollback();
  if (rebootAt && (int32_t)(millis() - rebootAt) > 0) {
    delay(50);
    ESP.restart();
  }
}
