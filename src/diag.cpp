#undef CORE_DEBUG_LEVEL
#define CORE_DEBUG_LEVEL 5
#include <Arduino.h>
#include <ETH.h>
#include <Preferences.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <NetworkUdp.h>
#include <Dmx_ESP32.h>
#include <esp_system.h>

#include "../firmware/w5500_diag/w5500_diag.ino"
