// ===== OtaCore =====
// Shared by every firmware in this pipeline. It provides:
//   * the board's own Wi-Fi hotspot (WPA2) with a captive portal
//   * optional home Wi-Fi and Ethernet
//   * the firmware update page at /update (and at / unless the app takes it)
//   * automatic rollback of new firmware that can't bring networking up
//   * a button to boot back into the firmware in the other slot
//
// Every sketch MUST call otaCoreBegin() in setup() and otaCoreLoop() in loop().
// Drop them and the board can only be updated over USB again.
#pragma once
#include <Arduino.h>
#include <WebServer.h>

// Tag a firmware with its name and version. Use exactly once per sketch, at file
// scope. The tag is embedded in the binary so the update page can tell which
// firmware sits in the other slot.
#define OTA_FIRMWARE(NAME, VERSION)                                                  \
  extern "C" const char OTA_FW_TAG[] __attribute__((used)) =                         \
      "\x01OTAFW|" NAME "|" VERSION "|" __DATE__ " " __TIME__ "|";                   \
  static const char *const OTA_FW_NAME = NAME;                                       \
  static const char *const OTA_FW_VERSION = VERSION

struct OtaConfig {
  const char *name;                       // pass OTA_FW_NAME
  const char *version;                    // pass OTA_FW_VERSION
  const char *hostname = "esp32-ota";     // http://<hostname>.local where mDNS works
  const char *apPrefix = "ESP32-OTA-";    // hotspot name = prefix + last 4 hex of MAC
  uint8_t apChannel = 6;
  bool appOwnsRoot = false;               // true: the app serves "/", update page stays at /update
  int ledPin = -1;                        // WS2812 data pin for otaSetLed, or -1

  // Optional: return a reason (e.g. "Stop the motor first") while the app can't
  // tolerate flash writes; firmware uploads, slot switches, password and Wi-Fi
  // changes are refused with that message. Return nullptr when it's fine.
  const char *(*busyReason)() = nullptr;

  // W5500 Ethernet over SPI (all -1 = no Ethernet)
  int ethSck = -1, ethMiso = -1, ethMosi = -1, ethCs = -1, ethInt = -1, ethRst = -1;
};

void otaCoreBegin(const OtaConfig &cfg);  // call first in setup()
void otaCoreLoop();                       // call every loop(); keep loop() non-blocking

bool otaNetworkUp();       // Ethernet or home Wi-Fi has an IP
bool otaHotspotUp();       // the board's own hotspot is running
bool otaUpdateRunning();   // a firmware upload is in progress
void otaSetLed(uint8_t r, uint8_t g, uint8_t b);

// For apps adding their own pages.
WebServer &otaServer();    // register routes after otaCoreBegin()
bool otaAuthorize();       // call at the top of a handler; false = it already sent a login prompt
const String &otaToken();  // random per-boot secret, for side servers (e.g. a stream on another port)
