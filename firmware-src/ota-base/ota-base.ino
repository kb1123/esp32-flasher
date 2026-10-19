// ===== ota-base: the pipeline's starting firmware =====
// Wi-Fi hotspot + Ethernet/home Wi-Fi + browser firmware upload + rollback.
// Board: Waveshare ESP32-S3-ETH. All the networking lives in the OtaCore library;
// this sketch only adds a status LED.
//
// New projects: copy this sketch, keep otaCoreBegin()/otaCoreLoop(), change the
// OTA_FIRMWARE name/version, and add your own code.

#include <OtaCore.h>

OTA_FIRMWARE("ota-base", "1.2.0");

static OtaConfig makeConfig() {
  OtaConfig c;
  c.name = OTA_FW_NAME;
  c.version = OTA_FW_VERSION;
  c.ledPin = 21;                                   // onboard WS2812
  c.ethSck = 13; c.ethMiso = 12; c.ethMosi = 11;   // W5500 (fixed on this board)
  c.ethCs = 14;  c.ethInt = 10;  c.ethRst = 9;
  return c;
}

// LED, slow breathing: blue = hotspot only, green = also on Ethernet/home Wi-Fi,
// red = no networking, purple = receiving an update (set by the core).
static void statusLed() {
  static uint32_t last = 0;
  if (otaUpdateRunning() || millis() - last < 40) return;
  last = millis();
  uint8_t phase = (millis() / 12) & 0xFF;
  uint8_t lvl = 2 + (phase < 128 ? phase : 255 - phase) / 8;
  if (otaNetworkUp())      otaSetLed(0, lvl, 0);
  else if (otaHotspotUp()) otaSetLed(0, 0, lvl);
  else                     otaSetLed(lvl, 0, 0);
}

void setup() {
  otaCoreBegin(makeConfig());
}

void loop() {
  otaCoreLoop();
  statusLed();
}
