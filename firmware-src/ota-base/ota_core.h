// ===== OTA core =====
// Every firmware built from this template MUST keep calling otaCoreBegin() and
// otaCoreLoop(). They provide Ethernet, the web upload page and rollback safety.
// Drop them and the board can only be updated over USB again.
#pragma once
#include <Arduino.h>

void otaCoreBegin();      // call first in setup()
void otaCoreLoop();       // call every loop(), never block loop() for long
bool otaNetworkUp();      // true once Ethernet has an IP
bool otaUpdateRunning();  // true while a firmware upload is in progress
void otaSetLed(uint8_t r, uint8_t g, uint8_t b);
