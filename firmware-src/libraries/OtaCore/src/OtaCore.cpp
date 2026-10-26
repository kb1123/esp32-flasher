#include "OtaCore.h"
#include "OtaWebUi.h"

#include <WiFi.h>
#include <DNSServer.h>
#include <ETH.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <Preferences.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_random.h"

extern "C" const char OTA_FW_TAG[];  // defined by OTA_FIRMWARE() in the sketch

// ---------------------------------------------------------------- constants
static const char *OTA_USER = "admin";
static const char *OTA_DEFAULT_PASS = "esp32ota";
static const uint32_t CONFIRM_AFTER_MS = 15000;   // trial firmware confirmed after this long with networking up
static const uint32_t CONFIRM_DEADLINE_MS = 180000;
static const uint32_t STA_GIVE_UP_MS = 45000;     // stop retrying a home network we never reached
static const uint8_t AP_MAX_CLIENTS = 4;

// ---------------------------------------------------------------- state
static OtaConfig cfg;
static bool ethEnabled = false;
static WebServer server(80);
static DNSServer dns;
static Preferences prefs;
static String otaPass, apSsid, token;

static volatile bool ethUp = false, staUp = false, apUp = false;
static volatile uint32_t netUpSince = 0;

enum StaState : uint8_t { STA_OFF, STA_CONNECTING, STA_CONNECTED, STA_FAILED };
static volatile StaState staState = STA_OFF;
static uint32_t staStartedAt = 0;
static String staSsid;

static bool pendingVerify = false;
static bool updating = false, uploadOk = false;
static String uploadError;
static size_t uploadBytes = 0;
static uint32_t rebootAt = 0, apRestartAt = 0;

// ---------------------------------------------------------------- firmware tags
struct FwTag { bool valid = false; bool tagged = false; String name, version, built; };
static FwTag otherSlot;
static bool otherSlotKnown = false;

static FwTag parseTag(const char *s, size_t n) {
  // "\x01OTAFW|name|version|built|"
  FwTag t;
  String str(s, n);
  int a = str.indexOf('|'), b = str.indexOf('|', a + 1), c = str.indexOf('|', b + 1), d = str.indexOf('|', c + 1);
  if (a < 0 || b < 0 || c < 0 || d < 0) return t;
  t.valid = t.tagged = true;
  t.name = str.substring(a + 1, b);
  t.version = str.substring(b + 1, c);
  t.built = str.substring(c + 1, d);
  return t;
}

static FwTag ownTag() { return parseTag(OTA_FW_TAG, strnlen(OTA_FW_TAG, 160)); }

// Length of the app image in a partition, by walking its segment headers. 0 if not an image.
static size_t imageLength(const esp_partition_t *p) {
  uint8_t hdr[24];
  if (esp_partition_read(p, 0, hdr, sizeof(hdr)) != ESP_OK || hdr[0] != 0xE9) return 0;
  size_t off = 24;
  for (int i = 0; i < hdr[1] && i < 16; i++) {
    uint32_t seg[2];
    if (esp_partition_read(p, off, seg, 8) != ESP_OK) return 0;
    if (seg[1] > p->size) return 0;
    off += 8 + seg[1];
    if (off > p->size) return 0;
  }
  return off;
}

// Identify the firmware in the slot we're NOT running from (where "switch back" goes).
static void scanOtherSlot() {
  otherSlot = FwTag();
  otherSlotKnown = true;
  const esp_partition_t *p = esp_ota_get_next_update_partition(NULL);
  esp_app_desc_t desc;
  if (!p || esp_ota_get_partition_description(p, &desc) != ESP_OK) return;
  size_t len = imageLength(p);
  if (!len) return;
  otherSlot.valid = true;
  static const char needle[] = "\x01OTAFW|";
  const size_t nlen = sizeof(needle) - 1, chunk = 4096, keep = 192;
  uint8_t *buf = (uint8_t *)malloc(chunk + keep);
  if (!buf) return;
  size_t carry = 0;
  for (size_t off = 0; off < len; off += chunk) {
    size_t n = min(chunk, len - off);
    if (esp_partition_read(p, off, buf + carry, n) != ESP_OK) break;
    size_t total = carry + n;
    for (size_t i = 0; i + nlen <= total; i++) {
      if (buf[i] == 0x01 && memcmp(buf + i, needle, nlen) == 0) {
        size_t avail = total - i;
        if (avail < 160 && off + n < len) break;  // tag may straddle the chunk; get more first
        FwTag t = parseTag((const char *)buf + i, strnlen((const char *)buf + i, min(avail, (size_t)160)));
        if (t.tagged) { otherSlot = t; free(buf); return; }
      }
    }
    carry = min(keep, total);
    memmove(buf, buf + total - carry, carry);
  }
  free(buf);
}

// ---------------------------------------------------------------- rollback
// Don't let the core auto-confirm new images; we confirm once networking is up.
bool verifyRollbackLater() { return true; }

static void checkPendingVerify() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
    pendingVerify = true;
    Serial.println("[ota] new firmware on trial, waiting for network before confirming");
  } else {
    esp_ota_mark_app_valid_cancel_rollback();
  }
}

static void serviceRollback() {
  if (!pendingVerify) return;
  if (netUpSince && millis() - netUpSince > CONFIRM_AFTER_MS) {
    esp_ota_mark_app_valid_cancel_rollback();
    pendingVerify = false;
    Serial.println("[ota] firmware confirmed good");
  } else if (millis() > CONFIRM_DEADLINE_MS) {
    Serial.println("[ota] networking never came up on trial firmware, rolling back");
    delay(100);
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
}

// ---------------------------------------------------------------- LED
void otaSetLed(uint8_t r, uint8_t g, uint8_t b) {
  if (cfg.ledPin >= 0) rgbLedWrite(cfg.ledPin, r, g, b);
}

// ---------------------------------------------------------------- network
static void markNetUp() { if (!netUpSince) netUpSince = millis() | 1; }

static void onNetEvent(arduino_event_id_t event, arduino_event_info_t) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START: ETH.setHostname(cfg.hostname); break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.printf("[eth] IP %s\n", ETH.localIP().toString().c_str());
      ethUp = true; markNetUp(); break;
    case ARDUINO_EVENT_ETH_LOST_IP:
    case ARDUINO_EVENT_ETH_DISCONNECTED:
    case ARDUINO_EVENT_ETH_STOP: ethUp = false; break;
    case ARDUINO_EVENT_WIFI_AP_START:
      Serial.printf("[ap] hotspot %s up at %s\n", apSsid.c_str(), WiFi.softAPIP().toString().c_str());
      apUp = true; markNetUp(); break;
    case ARDUINO_EVENT_WIFI_AP_STOP: apUp = false; break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("[wifi] joined %s, IP %s\n", staSsid.c_str(), WiFi.localIP().toString().c_str());
      staUp = true; staState = STA_CONNECTED; markNetUp(); break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      staUp = false;
      if (staState == STA_CONNECTED) staState = STA_CONNECTING;
      break;
    default: break;
  }
}

bool otaNetworkUp() { return ethUp || staUp; }
bool otaHotspotUp() { return apUp; }
bool otaUpdateRunning() { return updating; }
WebServer &otaServer() { return server; }
const String &otaToken() { return token; }

static void startHotspot() { WiFi.softAP(apSsid.c_str(), otaPass.c_str(), cfg.apChannel, 0, AP_MAX_CLIENTS); }

static void startStation(const String &ssid, const String &pass) {
  staSsid = ssid; staState = STA_CONNECTING; staStartedAt = millis();
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : nullptr);
}

// A station that keeps failing makes the hotspot hop channels and drop your phone.
static void serviceStation() {
  if (staState == STA_CONNECTING && !staUp && millis() - staStartedAt > STA_GIVE_UP_MS) {
    Serial.printf("[wifi] couldn't join %s, giving up\n", staSsid.c_str());
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    staState = STA_FAILED;
  }
}

// ---------------------------------------------------------------- web helpers
static const char *busy() { return cfg.busyReason ? cfg.busyReason() : nullptr; }
static bool fromHotspot() { return server.client().localIP() == WiFi.softAPIP(); }

// Hotspot clients already proved they know the password (WPA2); others get a login prompt.
bool otaAuthorize() {
  if (fromHotspot()) return true;
  if (server.authenticate(OTA_USER, otaPass.c_str())) return true;
  server.requestAuthentication(BASIC_AUTH, "ESP32");
  return false;
}

static String jsonEscape(const String &s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char ch = s[i];
    if (ch == '"' || ch == '\\') o += '\\';
    if ((uint8_t)ch >= 0x20) o += ch;
  }
  return o;
}
static String jstr(const String &s) { return "\"" + jsonEscape(s) + "\""; }
static void sendJson(int code, const String &j) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", j);
}
// For handlers that write flash: answers 409 with the app's reason and returns true if busy.
static bool refuseIfBusy() {
  const char *why = busy();
  if (!why) return false;
  sendJson(409, "{\"ok\":false,\"error\":" + jstr(why) + "}");
  return true;
}

// ---------------------------------------------------------------- handlers
static void handleStatus() {
  if (!otaAuthorize()) return;
  if (!otherSlotKnown) scanOtherSlot();
  FwTag me = ownTag();
  const esp_partition_t *run = esp_ota_get_running_partition();
  static const char *staNames[] = {"off", "connecting", "connected", "failed"};
  String j = "{";
  j += "\"fw\":" + jstr(cfg.name) + ",\"ver\":" + jstr(cfg.version) + ",\"built\":" + jstr(me.built);
  j += ",\"partition\":" + jstr(run->label);
  j += ",\"trial\":" + String(pendingVerify ? "true" : "false");
  j += ",\"appRoot\":" + String(cfg.appOwnsRoot ? "true" : "false");
  j += ",\"other\":{\"valid\":" + String(otherSlot.valid ? "true" : "false") +
       ",\"name\":" + jstr(otherSlot.name) + ",\"ver\":" + jstr(otherSlot.version) + ",\"built\":" + jstr(otherSlot.built) + "}";
  j += ",\"host\":" + jstr(cfg.hostname);
  j += ",\"via\":\"" + String(fromHotspot() ? "hotspot" : "network") + "\"";
  j += ",\"ap\":{\"ssid\":" + jstr(apSsid) + ",\"ip\":" + jstr(WiFi.softAPIP().toString()) +
       ",\"clients\":" + String(WiFi.softAPgetStationNum()) + "}";
  j += ",\"sta\":{\"state\":\"" + String(staNames[staState]) + "\",\"ssid\":" + jstr(staSsid) +
       ",\"ip\":" + jstr(staUp ? WiFi.localIP().toString() : String("")) + ",\"rssi\":" + String(staUp ? WiFi.RSSI() : 0) + "}";
  if (ethEnabled)
    j += ",\"eth\":{\"up\":" + String(ethUp ? "true" : "false") + ",\"ip\":" +
         jstr(ethUp ? ETH.localIP().toString() : String("")) + "}";
  else
    j += ",\"eth\":null";
  j += ",\"uptime\":" + String(millis() / 1000);
  j += ",\"heap\":" + String(ESP.getFreeHeap()) + ",\"psram\":" + String(ESP.getFreePsram());
  j += ",\"maxApp\":" + String(esp_ota_get_next_update_partition(NULL)->size);
  const char *why = busy();
  j += ",\"busy\":" + (why ? jstr(why) : String("null"));
  j += ",\"defaultPass\":" + String(otaPass == OTA_DEFAULT_PASS ? "true" : "false");
  j += "}";
  sendJson(200, j);
}

// First chunk must be an ESP32-S3 app image (not a merged full-flash image).
static bool validateHeader(const uint8_t *buf, size_t len) {
  if (len < 0x24) { uploadError = "file too small"; return false; }
  if (buf[0] != 0xE9) { uploadError = "not an ESP32 firmware image"; return false; }
  uint16_t chip = buf[12] | (buf[13] << 8);
  if (chip != 9) { uploadError = "firmware is for a different chip (id " + String(chip) + ")"; return false; }
  uint32_t magic = buf[0x20] | (buf[0x21] << 8) | (buf[0x22] << 16) | ((uint32_t)buf[0x23] << 24);
  if (magic != 0xABCD5432) { uploadError = "this looks like a full-flash (merged) image; upload the app .bin instead"; return false; }
  return true;
}

static void handleUploadChunk() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    uploadOk = false; uploadError = ""; uploadBytes = 0;
    if (!fromHotspot() && !server.authenticate(OTA_USER, otaPass.c_str())) { uploadError = "unauthorized"; return; }
    if (const char *why = busy()) { uploadError = why; return; }
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
    otherSlotKnown = false;  // the other slot was (partly) overwritten
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (Update.isRunning()) Update.abort();
    uploadError = "upload aborted";
    updating = false;
    otherSlotKnown = false;
  }
}

static void handleUploadDone() {
  if (!otaAuthorize()) return;
  if (uploadOk) {
    sendJson(200, "{\"ok\":true,\"bytes\":" + String(uploadBytes) + "}");
    Serial.printf("[ota] %u bytes written, rebooting\n", (unsigned)uploadBytes);
    rebootAt = millis() + 800;
  } else {
    otaSetLed(30, 0, 0);
    sendJson(400, "{\"ok\":false,\"error\":" + jstr(uploadError) + "}");
  }
}

// Boot the firmware in the other slot (the previous one, unless an upload replaced it).
static void handleSwitch() {
  if (!otaAuthorize() || refuseIfBusy()) return;
  const esp_partition_t *p = esp_ota_get_next_update_partition(NULL);
  esp_err_t err = p ? esp_ota_set_boot_partition(p) : ESP_ERR_NOT_FOUND;  // validates the image first
  if (err != ESP_OK) {
    sendJson(400, "{\"ok\":false,\"error\":" + jstr(String("the other slot has no valid firmware (") + esp_err_to_name(err) + ")") + "}");
    return;
  }
  Serial.printf("[ota] switching to %s\n", p->label);
  sendJson(200, "{\"ok\":true}");
  rebootAt = millis() + 800;
}

static void handlePassword() {
  if (!otaAuthorize() || refuseIfBusy()) return;
  String np = server.arg("new");
  if (np.length() < 8 || np.length() > 63) { sendJson(400, "{\"ok\":false,\"error\":\"use 8 to 63 characters\"}"); return; }
  prefs.putString("pass", np);
  otaPass = np;
  sendJson(200, "{\"ok\":true}");
  apRestartAt = millis() + 1500;  // the hotspot password changes too
}

static void handleReboot() {
  if (!otaAuthorize()) return;
  sendJson(200, "{\"ok\":true}");
  rebootAt = millis() + 500;
}

static void handleScan() {
  if (!otaAuthorize()) return;
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
    if (!ssid.length()) continue;
    bool dup = false;
    for (int k = 0; k < i; k++) if (WiFi.SSID(k) == ssid) { dup = true; break; }
    if (dup) continue;
    if (shown++) j += ",";
    j += "{\"ssid\":" + jstr(ssid) + ",\"rssi\":" + String(WiFi.RSSI(i)) +
         ",\"open\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  j += "]}";
  sendJson(200, j);
}

static void handleWifiJoin() {
  if (!otaAuthorize() || refuseIfBusy()) return;
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
  if (!otaAuthorize() || refuseIfBusy()) return;
  prefs.remove("ssid"); prefs.remove("wpass");
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, true);
  staSsid = ""; staState = STA_OFF; staUp = false;
  sendJson(200, "{\"ok\":true}");
}

static void handleUpdatePage() {
  if (!otaAuthorize()) return;
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html", OTA_WEB_UI_HTML);
}

// Phones probe a known URL after joining Wi-Fi; redirecting hotspot clients makes
// Android/iOS show "Sign in to network" and open our front page.
static void handleNotFound() {
  if (fromHotspot()) {
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
    return;
  }
  server.send(404, "text/plain", "not found");
}

// ---------------------------------------------------------------- public
void otaCoreBegin(const OtaConfig &c) {
  cfg = c;
  ethEnabled = cfg.ethSck >= 0 && cfg.ethMiso >= 0 && cfg.ethMosi >= 0 && cfg.ethCs >= 0;
  Serial.begin(115200);
  otaSetLed(0, 0, 25);

  char tok[17];
  snprintf(tok, sizeof(tok), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
  token = tok;

  prefs.begin("ota", false);
  otaPass = prefs.getString("pass", OTA_DEFAULT_PASS);

  checkPendingVerify();
  Network.onEvent(onNetEvent);

  WiFi.persistent(false);
  WiFi.setHostname(cfg.hostname);
  WiFi.mode(WIFI_AP_STA);
  uint8_t mac[6];
  WiFi.softAPmacAddress(mac);
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
  apSsid = String(cfg.apPrefix) + suffix;
  startHotspot();
  dns.setTTL(30);
  dns.start(53, "*", WiFi.softAPIP());

  String savedSsid = prefs.getString("ssid", "");
  if (savedSsid.length()) startStation(savedSsid, prefs.getString("wpass", ""));

  if (ethEnabled && !ETH.begin(ETH_PHY_W5500, 1, cfg.ethCs, cfg.ethInt, cfg.ethRst, SPI2_HOST,
                               cfg.ethSck, cfg.ethMiso, cfg.ethMosi)) {
    Serial.println("[eth] W5500 init failed");
  }

  if (MDNS.begin(cfg.hostname)) MDNS.addService("http", "tcp", 80);

  // The update page always lives at /update. "/" shows it too unless the app takes over.
  server.on("/update", HTTP_GET, handleUpdatePage);
  if (!cfg.appOwnsRoot) server.on("/", HTTP_GET, handleUpdatePage);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/update", HTTP_POST, handleUploadDone, handleUploadChunk);
  server.on("/api/switch", HTTP_POST, handleSwitch);
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
