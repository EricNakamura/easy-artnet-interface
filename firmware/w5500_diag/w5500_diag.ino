// Diagnóstico passo a passo do firmware sem INT.
// Constrói os objetos dentro do setup para identificar exatamente onde a
// placa reinicia. O loop testa o caminho completo (DMX, Artnet e WebServer).

#include <Arduino.h>
#include <ETH.h>
#include <Preferences.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <NetworkUdp.h>
#include <Dmx_ESP32.h>
#include <esp_system.h>

static WebServer *server = nullptr;
static NetworkUDP *artnetUdp = nullptr;
static dmxTx *dmx = nullptr;
static uint8_t artnetPacket[530];

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("=== DIAG w5500 ===");
  Serial.printf("reset_reason=%d\n", (int)esp_reset_reason());
  Serial.println("1. Serial OK");

  Serial.println("2. Preferences.begin...");
  Preferences p;
  bool p_ok = p.begin("diag", false);
  Serial.printf("2. Preferences.begin=%d\n", p_ok);

  Serial.println("3. ETH.begin...");
  bool eth_ok = ETH.begin(ETH_PHY_W5500, 1, 5, -1, -1,
                          SPI2_HOST, 18, 19, 23);
  Serial.printf("3. ETH.begin=%d linkUp=%d ip=%s\n",
                eth_ok, ETH.linkUp(), ETH.localIP().toString().c_str());
  delay(1000);
  Serial.printf("3b. linkUp=%d ip=%s\n",
                ETH.linkUp(), ETH.localIP().toString().c_str());

  Serial.println("4. dmxTx configure...");
  dmx = new dmxTx(&Serial1, 17, 4, 27);
  bool dmx_ok = dmx->configure();
  Serial.printf("4. dmxTx.configure=%d\n", dmx_ok);

  Serial.println("5. NetworkUDP begin...");
  artnetUdp = new NetworkUDP();
  artnetUdp->begin(6454);
  Serial.println("5. NetworkUDP OK");

  Serial.println("6. WebServer begin...");
  server = new WebServer(80);
  server->begin();
  Serial.println("6. WebServer OK");

  Serial.println("7. MDNS begin...");
  bool mdns_ok = MDNS.begin("dmx");
  Serial.printf("7. MDNS.begin=%d\n", mdns_ok);

  Serial.println("=== SETUP DONE ===");
}

void loop() {
  static uint32_t dmx_timer = 0;
  static uint32_t heartbeat = 0;

  if (dmx && millis() - dmx_timer > 25) {
    dmx->transmit();
    dmx_timer = millis();
  }
  if (artnetUdp && artnetUdp->parsePacket() > 0) {
    artnetUdp->read(artnetPacket, sizeof(artnetPacket));
  }
  if (server) server->handleClient();

  if (millis() - heartbeat > 1000) {
    heartbeat = millis();
  }
  delay(1);
}
