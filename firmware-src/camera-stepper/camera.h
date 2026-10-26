// ===== camera.h: live MJPEG camera for the Waveshare ESP32-S3-ETH =====
#pragma once
#include <Arduino.h>

namespace camera {

// enabled=false leaves the camera off (e.g. its XCLK pin is in use) and reports `reason`.
void begin(bool enabled, const String &reason);
void addRoutes();                 // /api/cam (GET/POST), /capture
void loop(bool canSave);          // writes changed settings to flash when canSave
bool ok();
bool enabled();
int viewers();
String json();

}  // namespace camera
