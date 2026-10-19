// ===== ota-base: Waveshare ESP32-S3-ETH OTA template =====
// Wi-Fi hotspot + Ethernet/home Wi-Fi + web firmware upload + automatic rollback.
// To build a new project: keep otaCoreBegin()/otaCoreLoop(), put your own
// code in appSetup()/appLoop(), bump FW_NAME/FW_VERSION in config.h.

#include "config.h"
#include "ota_core.h"

// ---------------------------------------------------------------- app
// The base firmware's "app" is just a status LED (slow breathing):
//   blue = hotspot only, green = also on Ethernet/home Wi-Fi, purple = receiving update
static void appSetup() {}

static void appLoop() {
  static uint32_t last = 0;
  if (otaUpdateRunning() || millis() - last < 40) return;
  last = millis();
  uint8_t phase = (millis() / 12) & 0xFF;
  uint8_t lvl = 2 + (phase < 128 ? phase : 255 - phase) / 8;
  if (otaNetworkUp())      otaSetLed(0, lvl, 0);
  else if (otaHotspotUp()) otaSetLed(0, 0, lvl);
  else                     otaSetLed(lvl, 0, 0);   // red: no networking at all
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
