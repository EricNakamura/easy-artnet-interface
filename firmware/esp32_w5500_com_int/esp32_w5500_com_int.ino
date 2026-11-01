/*
 * easy-artnet-interface — variante ESP32 + W5500
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

/*
 * Variante de compatibilidade do firmware principal (WT32-ETH01/LAN8720)
 * para o protótipo com ESP32 DevKit + módulo Ethernet W5500 (SPI).
 *
 * REQUISITOS:
 *  - ESP32 Arduino Core 2.0.17 (o mesmo do firmware WT32-ETH01).
 *  - Bibliotecas: esp_dmx 3.1.0 e ArtnetWifi.
 *
 * O W5500 é ligado diretamente ao lwIP através do driver nativo do ESP-IDF.
 * Assim o servidor web, o mDNS e o Art-Net continuam a funcionar como no
 * firmware original.
 *
 * LIGAÇÕES W5500 (iguais às do protótipo antigo, exceto o INT):
 *   W5500 SCK  -> GPIO18
 *   W5500 MISO -> GPIO19
 *   W5500 MOSI -> GPIO23
 *   W5500 CS   -> GPIO5
 *   W5500 INT  -> GPIO27 (obrigatório no core 2.x; o protótipo antigo
 *                         usava a biblioteca Ethernet em polling e não
 *                         precisava deste fio)
 *   W5500 RST  -> -1 (sem reset por GPIO; o driver faz reset por SPI)
 *
 * MAX485 / DMX (iguais ao protótipo antigo):
 *   DI    -> GPIO17 (TX)
 *   RO    -> GPIO16 (RX, não é usado na saída)
 *   DE/RE -> GPIO4  (EN)
 *
 * NOTA: o INT do W5500 vai para GPIO27 para não colidir com o EN do MAX485
 * (GPIO4). Se o seu módulo já tiver o INT noutro pino, altere W5500_IRQ.
 * Reveja a cablagem antes de gravar.
 */

#include <Arduino.h>
#include <esp_dmx.h>
#include <ArtnetWifi.h>
#include <Preferences.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "esp_eth.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

ArtnetWifi artnet;
Preferences memoria;
WebServer server(80);

// --- Configurações W5500 (SPI) ---
#define W5500_CS 5
#define W5500_IRQ 27
#define W5500_RST -1
#define W5500_SCK 18
#define W5500_MISO 19
#define W5500_MOSI 23

// --- Arrays de Configuração DMX ---
dmx_port_t dmxPort = 1;
int txPin = 17, enPin = 4, rxPin = 16;
byte dmxData[DMX_PACKET_SIZE];

// interface de rede criada para o W5500
esp_netif_t *eth_netif = nullptr;

// função interna do core do ESP32 que inicializa o lwIP
extern bool tcpipInit();

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

bool iniciarW5500() {
  if (!tcpipInit()) {
    log_e("Falha ao inicializar o lwIP");
    return false;
  }

  spi_bus_config_t buscfg = {};
  buscfg.mosi_io_num = W5500_MOSI;
  buscfg.miso_io_num = W5500_MISO;
  buscfg.sclk_io_num = W5500_SCK;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  if (spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO) != ESP_OK) {
    log_e("Falha ao inicializar o SPI do W5500");
    return false;
  }

  spi_device_interface_config_t devcfg = {};
  devcfg.command_bits = 16;  // fase de endereço do W5500
  devcfg.address_bits = 8;   // fase de controlo do W5500
  devcfg.clock_speed_hz = 20 * 1000 * 1000;
  devcfg.mode = 0;
  devcfg.spics_io_num = W5500_CS;
  devcfg.queue_size = 20;
  devcfg.input_delay_ns = 20;

  spi_device_handle_t spi_handle;
  if (spi_bus_add_device(SPI2_HOST, &devcfg, &spi_handle) != ESP_OK) {
    log_e("Falha ao adicionar o W5500 ao SPI");
    return false;
  }

  eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(spi_handle);
  w5500_config.int_gpio_num = W5500_IRQ;

  eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
  esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
  if (mac == nullptr) {
    log_e("Falha ao criar o MAC do W5500");
    return false;
  }

  // O driver W5500 do core 2.x usa interrupções no pino INT
  gpio_install_isr_service(0);

  eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
  phy_config.phy_addr = 1;
  phy_config.reset_gpio_num = W5500_RST;
  esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
  if (phy == nullptr) {
    log_e("Falha ao criar o PHY do W5500");
    return false;
  }

  esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
  esp_eth_handle_t eth_handle = nullptr;
  if (esp_eth_driver_install(&eth_config, &eth_handle) != ESP_OK) {
    log_e("Falha ao instalar o driver do W5500");
    return false;
  }

  // O W5500 não tem MAC de fábrica; gera um localmente a partir do ESP32
  uint8_t mac_addr[6];
  esp_efuse_mac_get_default(mac_addr);
  mac_addr[0] |= 0x02;
  esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, mac_addr);

  esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
  eth_netif = esp_netif_new(&cfg);
  if (eth_netif == nullptr) {
    log_e("Falha ao criar a interface de rede do W5500");
    return false;
  }

  if (esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)) != ESP_OK) {
    log_e("Falha ao ligar o W5500 ao lwIP");
    return false;
  }

  if (esp_eth_start(eth_handle) != ESP_OK) {
    log_e("Falha ao iniciar o W5500");
    return false;
  }

  return true;
}

void setup() {
  Serial.begin(115200);

  // Wi-Fi não é usado; o Art-Net usa WiFiUDP sobre o W5500/lwIP.
  WiFi.mode(WIFI_OFF);

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

  log_i("A ligar W5500...");
  if (!iniciarW5500()) {
    log_e("W5500 indisponível. Verifique as ligações SPI/INT.");
  }

  if (eth_netif && !usar_dhcp) {
    log_i("Modo IP Fixo ativado...");
    IPAddress static_ip, static_gw, static_mask;
    static_ip.fromString(ip_str);
    static_gw.fromString(gw_str);
    static_mask.fromString(mask_str);

    esp_netif_ip_info_t info = {};
    info.ip.addr = static_cast<uint32_t>(static_ip);
    info.gw.addr = static_cast<uint32_t>(static_gw);
    info.netmask.addr = static_cast<uint32_t>(static_mask);

    esp_netif_dhcpc_stop(eth_netif);
    esp_netif_set_ip_info(eth_netif, &info);
  } else if (eth_netif) {
    log_i("Modo DHCP. A aguardar IP (Máximo 5s)...");
  }

  log_i("A ligar Rede");
  uint32_t tempo_inicio = millis();
  bool rede_ok = false;
  
  while (eth_netif && millis() - tempo_inicio < 5000) {
    esp_netif_ip_info_t info = {};
    if (esp_netif_is_netif_up(eth_netif) &&
        esp_netif_get_ip_info(eth_netif, &info) == ESP_OK &&
        info.ip.addr != 0) {
      rede_ok = true;
      break;
    }
    delay(100);
  }

  if (eth_netif && !rede_ok && usar_dhcp) {
    log_w("DHCP Falhou. A transformar a Placa num Servidor DHCP...");

    IPAddress placa_IP(10, 0, 0, 1);
    IPAddress placa_Mask(255, 0, 0, 0);
    IPAddress zero_IP(0, 0, 0, 0); 

    esp_netif_ip_info_t info = {};
    info.ip.addr = static_cast<uint32_t>(placa_IP);
    info.gw.addr = static_cast<uint32_t>(zero_IP);
    info.netmask.addr = static_cast<uint32_t>(placa_Mask);

    esp_netif_dhcpc_stop(eth_netif);
    esp_netif_set_ip_info(eth_netif, &info);

    uint8_t disable_opt = 0;
    
    // Remove o Gateway (Router)
    esp_netif_dhcps_option(eth_netif, ESP_NETIF_OP_SET, ESP_NETIF_ROUTER_SOLICITATION_ADDRESS, &disable_opt, sizeof(disable_opt));
    
    // Remove o DNS
    esp_netif_dhcps_option(eth_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &disable_opt, sizeof(disable_opt));

    esp_netif_dhcps_start(eth_netif);
    log_i("Servidor DHCP interno ATIVADO! A distribuir IPs...");
  }

  if (eth_netif) {
    esp_netif_ip_info_t info = {};
    esp_netif_get_ip_info(eth_netif, &info);
    log_i("Rede Pronta! IP Final: %s", IPAddress(info.ip.addr).toString().c_str());
  }

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
