// ===== ota-base: Waveshare ESP32-S3-ETH OTA template =====
// Ethernet + web firmware upload + automatic rollback.
// To build a new project: keep otaCoreBegin()/otaCoreLoop(), put your own
// code in appSetup()/appLoop(), bump FW_NAME/FW_VERSION in config.h.

#include "config.h"
#include "ota_core.h"

// ---------------------------------------------------------------- app
// The base firmware's "app" is just a status LED:
//   blue = waiting for network, green pulse = online, purple = receiving update
static void appSetup() {}

static void appLoop() {
  static uint32_t last = 0;
  if (otaUpdateRunning() || millis() - last < 40) return;
  last = millis();
  if (otaNetworkUp()) {
    uint8_t phase = (millis() / 12) & 0xFF;               // slow breathe
    uint8_t lvl = phase < 128 ? phase : 255 - phase;
    otaSetLed(0, 2 + lvl / 8, 0);
  } else {
    otaSetLed(0, 0, 20);
  }
}

// ---------------------------------------------------------------- main
void setup() {
  otaCoreBegin();
  appSetup();
}

void loop() {
  otaCoreLoop();
  appLoop();
}
