/*
 * easy-artnet-interface
 * Copyright (C) 2026 Eric Masaro Nakamura Bafa
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <Arduino.h>
#include <esp_dmx.h>
#include <ArtnetWifi.h>
#include <ETH.h>
#include <Preferences.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "esp_netif.h"
#include "dhcpserver/dhcpserver.h"

ArtnetWifi artnet;
Preferences memoria;
WebServer server(80);

// --- Configurações WT32-ETH01 ---
#define ETH_PHY_ADDR 1
#define ETH_PHY_POWER 16
#define ETH_PHY_MDC 23
#define ETH_PHY_MDIO 18
#define ETH_PHY_TYPE ETH_PHY_LAN8720
#define ETH_CLK_MODE ETH_CLOCK_GPIO0_IN

// --- Arrays de Configuração DMX ---
dmx_port_t dmxPort = 1;
int txPin = 17, enPin = 4, rxPin = 5;
byte dmxData[DMX_PACKET_SIZE]; 

// contagem para reset
int contagem_boots = 0;

// configuração de rede
bool usar_dhcp;
String ip_str, gw_str, mask_str;
int startUniverse = 0;

String getPaginaWeb() {
  String html = R"=====(
    <!DOCTYPE html>
    <html lang="pt">
    <head>
      <meta charset="UTF-8">
      <meta name="viewport" content="width=device-width, initial-scale=1.0">
      <title>Painel Art-Net DMX</title>
      <style>
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: #2c3e50; color: #ecf0f1; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }
        .card { background: #34495e; padding: 30px; border-radius: 12px; box-shadow: 0 10px 20px rgba(0,0,0,0.3); width: 320px; }
        h2 { text-align: center; color: #3498db; margin-top: 0; }
        label { font-weight: 600; display: block; margin-top: 15px; margin-bottom: 5px; font-size: 14px;}
        input[type="text"], input[type="number"] { width: 100%; padding: 10px; border: 1px solid #7f8c8d; border-radius: 6px; box-sizing: border-box; background: #2c3e50; color: #fff; }
        .checkbox-container { background: #2c3e50; padding: 10px; border-radius: 6px; margin-top: 15px; display: flex; align-items: center; }
        .checkbox-container input { margin-right: 10px; }
        input[type="submit"] { width: 100%; padding: 12px; margin-top: 25px; background-color: #3498db; color: white; border: none; border-radius: 6px; cursor: pointer; font-size: 16px; font-weight: bold; transition: 0.3s;}
        input[type="submit"]:hover { background-color: #2980b9; }
      </style>
    </head>
    <body>
      <div class="card">
        <h2>Easy ArtNet Interface</h2>
        <form action="/salvar" method="POST">
          <label>Universo Art-Net:</label>
          <input type="number" name="univ" min="0" max="32767" value="%UNIV%">

          <div class="checkbox-container">
            <input type="checkbox" id="dhcp" name="dhcp" value="1" %DHCP_CHK% onchange="toggleIP()">
            <label style="margin: 0; cursor: pointer;" for="dhcp">Obter IP Automático (DHCP)</label>
          </div>
          
          <div id="ip_fields" style="display:%DISPLAY_IP%;">
            <label>IP Fixo da Placa:</label>
            <input type="text" name="ip" value="%IP%">
            <label>Gateway:</label>
            <input type="text" name="gw" value="%GW%">
            <label>Máscara de Rede:</label>
            <input type="text" name="mask" value="%MASK%">
          </div>
          
          <input type="submit" value="Guardar e Reiniciar">
        </form>
      </div>
      <script>
        function toggleIP() {
          document.getElementById('ip_fields').style.display = document.getElementById('dhcp').checked ? 'none' : 'block';
        }
      </script>
    </body>
    </html>
  )=====";
  
  html.replace("%UNIV%", String(startUniverse));
  html.replace("%DHCP_CHK%", usar_dhcp ? "checked" : "");
  html.replace("%DISPLAY_IP%", usar_dhcp ? "none" : "block");
  html.replace("%IP%", ip_str);
  html.replace("%GW%", gw_str);
  html.replace("%MASK%", mask_str);
  
  return html;
}

void onArtNetFrame(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t* data) {
  if (universe == startUniverse) { 
    memcpy(&dmxData[1], data, length); 
    dmx_write(dmxPort, dmxData, 513); 
  }
}

void setup() {
  Serial.begin(115200);

  memoria.begin("config", false);

  contagem_boots = memoria.getInt("boots", 0);
  contagem_boots++; 
  memoria.putInt("boots", contagem_boots);

  if (contagem_boots >= 2) {
    log_w("\n[AVISO] 2 arranques rápidos detetados. FACTORY RESET...");
    memoria.clear();
    delay(1000);
    ESP.restart();
  }

  usar_dhcp = memoria.getBool("dhcp", true);
  ip_str = memoria.getString("ip", "2.0.0.10");
  gw_str = memoria.getString("gw", "0.0.0.0");
  mask_str = memoria.getString("mask", "255.0.0.0");
  startUniverse = memoria.getInt("univ", 0);

  ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_TYPE, ETH_CLK_MODE);

  if (!usar_dhcp) {
    log_i("Modo IP Fixo ativado...");
    IPAddress static_ip, static_gw, static_mask;
    static_ip.fromString(ip_str);
    static_gw.fromString(gw_str);
    static_mask.fromString(mask_str);
    ETH.config(static_ip, static_gw, static_mask, IPAddress(0,0,0,0));
  } else {
    log_i("Modo DHCP. A aguardar IP (Máximo 5s)...");
  }

  log_i("A ligar Rede");
  uint32_t tempo_inicio = millis();
  bool rede_ok = false;
  
  while (millis() - tempo_inicio < 5000) {
    if (ETH.linkUp() && ETH.localIP() != IPAddress(0, 0, 0, 0)) {
      rede_ok = true;
      break;
    }
    delay(100);
  }

  if (!rede_ok && usar_dhcp) {
    log_w("DHCP Falhou. A transformar a Placa num Servidor DHCP...");


    IPAddress placa_IP(10, 0, 0, 1);
    IPAddress placa_Mask(255, 0, 0, 0);
    IPAddress zero_IP(0, 0, 0, 0); 
    
    // A ordem é: IP Local, Gateway, Máscara, DNS
    ETH.config(placa_IP, zero_IP, placa_Mask, zero_IP);

    esp_netif_t *netif = esp_netif_next(NULL);
    while (netif) {
      if (strcmp(esp_netif_get_desc(netif), "eth") == 0) {
        esp_netif_dhcpc_stop(netif);

        uint8_t disable_opt = 0;
        
        // Remove o Gateway (Router)
        esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET, ESP_NETIF_ROUTER_SOLICITATION_ADDRESS, &disable_opt, sizeof(disable_opt));
        
        // Remove o DNS
        esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &disable_opt, sizeof(disable_opt));

        esp_netif_dhcps_start(netif);
        log_i("Servidor DHCP interno ATIVADO! A distribuir IPs...");
        break;
      }
      netif = esp_netif_next(netif);
    }
  }

  log_i("Rede Pronta! IP Final: %s", ETH.localIP().toString().c_str());

  if (MDNS.begin("dmx")) {
    log_i("mDNS Iniciado! Aceda através de http://dmx.local");
  }

  dmx_config_t config = DMX_CONFIG_DEFAULT;
  
  dmx_driver_install(dmxPort, &config, 0); 
  dmx_set_pin(dmxPort, txPin, rxPin, enPin);

  artnet.setArtDmxCallback(onArtNetFrame);
  artnet.begin();
  log_i("Artnet configurado");

  server.on("/", []() {
    server.send(200, "text/html", getPaginaWeb());
  });

  server.on("/salvar", HTTP_POST, []() {
    bool novo_dhcp = server.hasArg("dhcp");
    memoria.putBool("dhcp", novo_dhcp);
    memoria.putString("ip", server.arg("ip"));
    memoria.putString("gw", server.arg("gw"));
    memoria.putString("mask", server.arg("mask"));
    memoria.putInt("univ", server.arg("univ").toInt());
    
    server.send(200, "text/html", "<h2 style='font-family:Arial; text-align:center; padding-top:50px; color:#3498db;'>Salvo! Reiniciando a placa...</h2>");
    delay(1000);
    ESP.restart();
  });
  server.begin();
  log_i("Servidor Web a correr!");
}

void loop() { 
  artnet.read();
  server.handleClient();

  if (contagem_boots > 0 && millis() > 4000) {
    memoria.putInt("boots", 0);
    contagem_boots = 0;
  }

  static uint32_t timer = 0;
  if (millis() - timer > 25) {
    dmx_send(dmxPort, 513); 
    timer = millis();
    }
  }