// ===== Board + firmware configuration =====
// Waveshare ESP32-S3-ETH (ESP32-S3R8, 16MB flash, 8MB OPI PSRAM, W5500 Ethernet)
#pragma once

// ---- Firmware identity (shown on the device's web page) ----
#define FW_NAME     "ota-base"
#define FW_VERSION  "1.0.0"

// ---- Network ----
#define OTA_HOSTNAME        "esp32-ota"   // reachable as http://esp32-ota.local where mDNS works
#define OTA_USER            "admin"
#define OTA_DEFAULT_PASS    "esp32ota"    // change it from the web page after first boot

// ---- W5500 wiring (fixed on this board, from the Waveshare schematic) ----
#define ETH_PIN_SCK   13
#define ETH_PIN_MISO  12
#define ETH_PIN_MOSI  11
#define ETH_PIN_CS    14
#define ETH_PIN_INT   10
#define ETH_PIN_RST    9

// ---- Onboard WS2812 status LED ----
#define STATUS_LED_PIN 21

// ---- Rollback safety ----
// A freshly OTA-installed firmware must get an IP and stay up this long before it is
// confirmed as good. If it can't get online within OTA_CONFIRM_DEADLINE_MS it reboots
// and the bootloader falls back to the previous firmware.
#define OTA_CONFIRM_AFTER_MS     15000UL
#define OTA_CONFIRM_DEADLINE_MS 180000UL
