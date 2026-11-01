#undef CORE_DEBUG_LEVEL
#define CORE_DEBUG_LEVEL 3
#include <Arduino.h>
#include <ETH.h>
#include <Preferences.h>
#include <WebServer.h>
#include <ESPmDNS.h>

#if defined(FW_SEM_INT)
#include <NetworkUdp.h>
#include <Dmx_ESP32.h>
#elif defined(FW_COM_INT) || defined(FW_WT32)
#include <ArtnetWifi.h>
#include <esp_dmx.h>
#endif

#if defined(FW_SEM_INT)
#include "../firmware/esp32_w5500_sem_int/esp32_w5500_sem_int.ino"
#elif defined(FW_COM_INT)
#include "../firmware/esp32_w5500_com_int/esp32_w5500_com_int.ino"
#elif defined(FW_WT32)
#include "../firmware/wt32_eth01/wt32_eth01.ino"
#else
#error "Define FW_SEM_INT, FW_COM_INT ou FW_WT32 no platformio.ini"
#endif
