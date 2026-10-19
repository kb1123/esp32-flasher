#include "ota_core.h"
#include "config.h"
#include "web_ui.h"

#include <WiFi.h>
#include <DNSServer.h>
#if ETH_ENABLED
#include <ETH.h>
#endif
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <Preferences.h>
#include "esp_ota_ops.h"
#include "esp_app_desc.h"

static WebServer server(80);
static DNSServer dns;
static Preferences prefs;
static String otaPass;
static String apSsid;

// Network state (written from the network event task, read in loop)
static volatile bool ethUp = false;
static volatile bool staUp = false;
static volatile bool apUp = false;
static volatile uint32_t netUpSince = 0;   // first moment any interface was usable

// Home Wi-Fi (station) state
enum StaState : uint8_t { STA_OFF, STA_CONNECTING, STA_CONNECTED, STA_FAILED };
static volatile StaState staState = STA_OFF;
static uint32_t staStartedAt = 0;
static String staSsid;
static const uint32_t STA_GIVE_UP_MS = 45000;  // stop retrying a network we never reached

// OTA state
static bool pendingVerify = false;
static bool updating = false;
static bool uploadOk = false;
static String uploadError;
static size_t uploadBytes = 0;
static uint32_t rebootAt = 0;
static uint32_t apRestartAt = 0;

// ---------------------------------------------------------------- rollback
// Tell the core not to auto-confirm a new OTA image; we confirm it ourselves
// only after it proves it can bring networking up and keep running.
bool verifyRollbackLater() { return true; }

static void checkPendingVerify() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
      state == ESP_OTA_IMG_PENDING_VERIFY) {
    pendingVerify = true;
    Serial.println("[ota] new firmware on trial, waiting for network before confirming");
  } else {
    esp_ota_mark_app_valid_cancel_rollback();  // USB-flashed or already confirmed
  }
}

static void serviceRollback() {
  if (!pendingVerify) return;
  if (netUpSince && millis() - netUpSince > OTA_CONFIRM_AFTER_MS) {
    esp_ota_mark_app_valid_cancel_rollback();
    pendingVerify = false;
    Serial.println("[ota] firmware confirmed good");
  } else if (millis() > OTA_CONFIRM_DEADLINE_MS) {
    Serial.println("[ota] networking never came up on trial firmware, rolling back");
    delay(100);
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
}

// ---------------------------------------------------------------- LED
void otaSetLed(uint8_t r, uint8_t g, uint8_t b) { rgbLedWrite(STATUS_LED_PIN, r, g, b); }

// ---------------------------------------------------------------- network
static void markNetUp() { if (!netUpSince) netUpSince = millis() | 1; }

static void onNetEvent(arduino_event_id_t event, arduino_event_info_t) {
  switch (event) {
#if ETH_ENABLED
    case ARDUINO_EVENT_ETH_START: ETH.setHostname(OTA_HOSTNAME); break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.printf("[eth] IP %s\n", ETH.localIP().toString().c_str());
      ethUp = true; markNetUp();
      break;
    case ARDUINO_EVENT_ETH_LOST_IP:
    case ARDUINO_EVENT_ETH_DISCONNECTED:
    case ARDUINO_EVENT_ETH_STOP:
      ethUp = false;
      break;
#endif
    case ARDUINO_EVENT_WIFI_AP_START:
      Serial.printf("[ap] hotspot %s up at %s\n", apSsid.c_str(), WiFi.softAPIP().toString().c_str());
      apUp = true; markNetUp();
      break;
    case ARDUINO_EVENT_WIFI_AP_STOP: apUp = false; break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("[wifi] joined %s, IP %s\n", staSsid.c_str(), WiFi.localIP().toString().c_str());
      staUp = true; staState = STA_CONNECTED; markNetUp();
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      staUp = false;
      if (staState == STA_CONNECTED) staState = STA_CONNECTING;  // auto-reconnect is on
      break;
    default: break;
  }
}

bool otaNetworkUp() { return ethUp || staUp; }
bool otaHotspotUp() { return apUp; }
bool otaUpdateRunning() { return updating; }

static void startHotspot() {
  WiFi.softAP(apSsid.c_str(), otaPass.c_str(), AP_CHANNEL, 0, AP_MAX_CLIENTS);
}

static void startStation(const String &ssid, const String &pass) {
  staSsid = ssid;
  staState = STA_CONNECTING;
  staStartedAt = millis();
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : nullptr);
}

// A station that keeps failing makes the hotspot hop channels and drop your
// phone, so give up on a network we have never managed to join.
static void serviceStation() {
  if (staState == STA_CONNECTING && !staUp && millis() - staStartedAt > STA_GIVE_UP_MS) {
    Serial.printf("[wifi] couldn't join %s, giving up\n", staSsid.c_str());
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    staState = STA_FAILED;
  }
}

// ---------------------------------------------------------------- web helpers
static bool fromHotspot() {
  return server.client().localIP() == WiFi.softAPIP();
}

// Hotspot clients already proved they know the password (WPA2). Everyone else
// (Ethernet, home Wi-Fi) gets a browser login prompt.
static bool auth() {
  if (fromHotspot()) return true;
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
static String jstr(const String &s) { return "\"" + jsonEscape(s.c_str()) + "\""; }
static void sendJson(int code, const String &j) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", j);
}

// ---------------------------------------------------------------- handlers
static void handleStatus() {
  if (!auth()) return;
  const esp_app_desc_t *app = esp_app_get_description();
  const esp_partition_t *run = esp_ota_get_running_partition();
  static const char *staNames[] = {"off", "connecting", "connected", "failed"};
  String j = "{";
  j += "\"fw\":\"" FW_NAME "\",\"ver\":\"" FW_VERSION "\"";
  j += ",\"built\":" + jstr(String(app->date) + " " + app->time);
  j += ",\"partition\":" + jstr(run->label);
  j += ",\"trial\":" + String(pendingVerify ? "true" : "false");
  j += ",\"host\":\"" OTA_HOSTNAME "\"";
  j += ",\"via\":\"" + String(fromHotspot() ? "hotspot" : "network") + "\"";
  j += ",\"ap\":{\"ssid\":" + jstr(apSsid) + ",\"ip\":" + jstr(WiFi.softAPIP().toString()) +
       ",\"clients\":" + String(WiFi.softAPgetStationNum()) + "}";
  j += ",\"sta\":{\"state\":\"" + String(staNames[staState]) + "\",\"ssid\":" + jstr(staSsid) +
       ",\"ip\":" + jstr(staUp ? WiFi.localIP().toString() : String("")) +
       ",\"rssi\":" + String(staUp ? WiFi.RSSI() : 0) + "}";
#if ETH_ENABLED
  j += ",\"eth\":{\"up\":" + String(ethUp ? "true" : "false") + ",\"ip\":" +
       jstr(ethUp ? ETH.localIP().toString() : String("")) + ",\"mac\":" + jstr(ETH.macAddress()) + "}";
#else
  j += ",\"eth\":null";
#endif
  j += ",\"uptime\":" + String(millis() / 1000);
  j += ",\"heap\":" + String(ESP.getFreeHeap());
  j += ",\"psram\":" + String(ESP.getFreePsram());
  j += ",\"maxApp\":" + String(esp_ota_get_next_update_partition(NULL)->size);
  j += ",\"defaultPass\":" + String(otaPass == OTA_DEFAULT_PASS ? "true" : "false");
  j += "}";
  sendJson(200, j);
}

// Validate the first chunk: must be an ESP32-S3 *app* image, not a merged
// full-flash image (which starts with the bootloader) or another chip's build.
static bool validateHeader(const uint8_t *buf, size_t len) {
  if (len < 0x24) { uploadError = "file too small"; return false; }
  if (buf[0] != 0xE9) { uploadError = "not an ESP32 firmware image"; return false; }
  uint16_t chip = buf[12] | (buf[13] << 8);
  if (chip != 9 /* ESP32-S3 */) {
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
    uploadOk = false; uploadError = ""; uploadBytes = 0;
    if (!fromHotspot() && !server.authenticate(OTA_USER, otaPass.c_str())) { uploadError = "unauthorized"; return; }
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
    sendJson(200, "{\"ok\":true,\"bytes\":" + String(uploadBytes) + "}");
    Serial.printf("[ota] %u bytes written, rebooting\n", (unsigned)uploadBytes);
    rebootAt = millis() + 800;
  } else {
    otaSetLed(30, 0, 0);
    sendJson(400, "{\"ok\":false,\"error\":" + jstr(uploadError) + "}");
  }
}

static void handlePassword() {
  if (!auth()) return;
  String np = server.arg("new");
  if (np.length() < 8 || np.length() > 63) {
    sendJson(400, "{\"ok\":false,\"error\":\"use 8 to 63 characters\"}");
    return;
  }
  prefs.putString("pass", np);
  otaPass = np;
  sendJson(200, "{\"ok\":true}");
  apRestartAt = millis() + 1500;  // hotspot password changes too; let the reply get out first
}

static void handleReboot() {
  if (!auth()) return;
  sendJson(200, "{\"ok\":true}");
  rebootAt = millis() + 500;
}

static void handleScan() {
  if (!auth()) return;
  int16_t n = WiFi.scanComplete();
  if (server.hasArg("start") || n == WIFI_SCAN_FAILED) {
    if (n != WIFI_SCAN_RUNNING) { WiFi.scanDelete(); WiFi.scanNetworks(true, false); }
    sendJson(200, "{\"scanning\":true}");
    return;
  }
  if (n == WIFI_SCAN_RUNNING) { sendJson(200, "{\"scanning\":true}"); return; }
  String j = "{\"scanning\":false,\"networks\":[";
  int shown = 0;
  for (int i = 0; i < n && shown < 30; i++) {
    String ssid = WiFi.SSID(i);
    if (!ssid.length()) continue;  // hidden networks
    bool dup = false;
    for (int k = 0; k < i; k++) if (WiFi.SSID(k) == ssid) { dup = true; break; }
    if (dup) continue;             // keep only the strongest entry per name
    if (shown++) j += ",";
    j += "{\"ssid\":" + jstr(ssid) + ",\"rssi\":" + String(WiFi.RSSI(i)) +
         ",\"open\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  j += "]}";
  sendJson(200, j);
}

static void handleWifiJoin() {
  if (!auth()) return;
  String ssid = server.arg("ssid"), pass = server.arg("pass");
  if (!ssid.length() || ssid.length() > 32) { sendJson(400, "{\"ok\":false,\"error\":\"network name required\"}"); return; }
  if (pass.length() && (pass.length() < 8 || pass.length() > 63)) {
    sendJson(400, "{\"ok\":false,\"error\":\"Wi-Fi passwords are 8 to 63 characters\"}");
    return;
  }
  prefs.putString("ssid", ssid);
  prefs.putString("wpass", pass);
  sendJson(200, "{\"ok\":true}");
  delay(100);
  WiFi.disconnect(false, false);
  startStation(ssid, pass);
}

static void handleWifiForget() {
  if (!auth()) return;
  prefs.remove("ssid"); prefs.remove("wpass");
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, true);
  staSsid = ""; staState = STA_OFF; staUp = false;
  sendJson(200, "{\"ok\":true}");
}

static void handleRoot() {
  if (!auth()) return;
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html", WEB_UI_HTML);
}

// Phones probe a known URL when they join Wi-Fi. Answering hotspot clients with
// a redirect makes Android/iOS show "Sign in to network" and open this page.
static void captiveRedirect() {
  server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

static void handleNotFound() {
  if (fromHotspot()) { captiveRedirect(); return; }
  server.send(404, "text/plain", "not found");
}

// ---------------------------------------------------------------- public
void otaCoreBegin() {
  Serial.begin(115200);
  otaSetLed(0, 0, 25);

  prefs.begin("ota", false);
  otaPass = prefs.getString("pass", OTA_DEFAULT_PASS);

  checkPendingVerify();
  Network.onEvent(onNetEvent);

  // Wi-Fi: hotspot always, plus home network if one was saved
  WiFi.persistent(false);          // we keep credentials in our own prefs
  WiFi.setHostname(OTA_HOSTNAME);
  WiFi.mode(WIFI_AP_STA);
  uint8_t mac[6];
  WiFi.softAPmacAddress(mac);
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
  apSsid = String(AP_SSID_PREFIX) + suffix;
  startHotspot();
  dns.setTTL(30);
  dns.start(53, "*", WiFi.softAPIP());

  String savedSsid = prefs.getString("ssid", "");
  if (savedSsid.length()) startStation(savedSsid, prefs.getString("wpass", ""));

#if ETH_ENABLED
  if (!ETH.begin(ETH_PHY_W5500, 1, ETH_PIN_CS, ETH_PIN_INT, ETH_PIN_RST,
                 SPI2_HOST, ETH_PIN_SCK, ETH_PIN_MISO, ETH_PIN_MOSI)) {
    Serial.println("[eth] W5500 init failed");
  }
#endif

  if (MDNS.begin(OTA_HOSTNAME)) MDNS.addService("http", "tcp", 80);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/update", HTTP_POST, handleUploadDone, handleUploadChunk);
  server.on("/api/password", HTTP_POST, handlePassword);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/wifi/scan", HTTP_GET, handleScan);
  server.on("/api/wifi/join", HTTP_POST, handleWifiJoin);
  server.on("/api/wifi/forget", HTTP_POST, handleWifiForget);
  server.onNotFound(handleNotFound);
  server.begin();
}

void otaCoreLoop() {
  server.handleClient();
  serviceRollback();
  serviceStation();
  uint32_t now = millis();
  if (apRestartAt && (int32_t)(now - apRestartAt) > 0) {
    apRestartAt = 0;
    Serial.println("[ap] restarting hotspot with new password");
    startHotspot();
  }
  if (rebootAt && (int32_t)(now - rebootAt) > 0) {
    delay(50);
    ESP.restart();
  }
}
