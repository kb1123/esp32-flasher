// ===== Board + firmware configuration =====
// Waveshare ESP32-S3-ETH (ESP32-S3R8, 16MB flash, 8MB OPI PSRAM, W5500 Ethernet)
#pragma once

// ---- Firmware identity (shown on the device's web page) ----
#define FW_NAME     "ota-base"
#define FW_VERSION  "1.1.0"

// ---- Access ----
// One password protects both the board's own Wi-Fi hotspot (WPA2) and the web
// page when it is reached through Ethernet or your home Wi-Fi. Anyone who has
// joined the hotspot already knows it, so the page doesn't ask again there.
#define OTA_HOSTNAME        "esp32-ota"   // http://esp32-ota.local where mDNS works
#define OTA_USER            "admin"
#define OTA_DEFAULT_PASS    "esp32ota"    // change it from the web page after first boot

// ---- Wi-Fi hotspot (always on) ----
#define AP_SSID_PREFIX      "ESP32-OTA-"  // + last 4 hex digits of the MAC, e.g. ESP32-OTA-3F2A
#define AP_CHANNEL          6             // moves to the home network's channel once joined
#define AP_MAX_CLIENTS      4

// ---- W5500 wiring (fixed on this board, from the Waveshare schematic) ----
// Set ETH_ENABLED to 0 to reuse this template on an ESP32 board without Ethernet.
#define ETH_ENABLED   1
#define ETH_PIN_SCK   13
#define ETH_PIN_MISO  12
#define ETH_PIN_MOSI  11
#define ETH_PIN_CS    14
#define ETH_PIN_INT   10
#define ETH_PIN_RST    9

// ---- Onboard WS2812 status LED ----
#define STATUS_LED_PIN 21

// ---- Rollback safety ----
// A freshly OTA-installed firmware is on trial. It is confirmed once it has kept
// the hotspot (or a network connection) up for OTA_CONFIRM_AFTER_MS. If it crashes,
// boot-loops, or can't bring networking up within OTA_CONFIRM_DEADLINE_MS, the
// bootloader goes back to the previous firmware.
#define OTA_CONFIRM_AFTER_MS     15000UL
#define OTA_CONFIRM_DEADLINE_MS 180000UL
