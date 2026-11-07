/*
 * easy-artnet-interface — variante ESP32 + W5500 SEM INT
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
 * Use esta variante enquanto o pino INT do W5500 não estiver ligado.
 *
 * Nesta versão o W5500 é inicializado diretamente pelo esp_eth do core 3.x
 * com o driver em polling (int_gpio_num = -1, poll_period_ms = 10): não
 * precisa do fio INT.
 *
 * REQUISITOS:
 *  - ESP32 Arduino Core 3.x (testado com 3.3.11/3.3.12).
 *  - Biblioteca Dmx_ESP32 (github.com/devarishi7/Dmx_ESP32).
 *
 * WI-FI / REDE:
 *  - O Wi-Fi do ESP32 não é inicializado. O Art-Net usa NetworkUDP/lwIP
 *    sobre o W5500; não há WiFi.begin/softAP nem rádio Wi-Fi ligado.
 *  - DHCP cliente normal (router) e fallback auto-router em 10.0.0.1,
 *    ambos sem INT. O W5500 fica em polling.
 *  - Quando ligares o INT, podes usar o sketch com INT (core 2.0.17),
 *    mas esta versão já não precisa dele.
 *
 * LIGAÇÕES (iguais ao protótipo antigo):
 *   W5500 SCK  -> GPIO18
 *   W5500 MISO -> GPIO19
 *   W5500 MOSI -> GPIO23
 *   W5500 CS   -> GPIO5
 *   W5500 INT  -> não ligado (polling)
 *   W5500 RST  -> não ligado (reset por SPI)
 *
 * MAX485 / DMX:
 *   DI    -> GPIO17 (TX)
 *   RO    -> GPIO16 (não usado nesta versão)
 *   DE/RE -> GPIO4  (EN)
 *   LED   -> GPIO27 (toggle do Dmx_ESP32; pino livre, sem LED onboard)
 */

#include <Arduino.h>
#include <Dmx_ESP32.h>
#include <NetworkUdp.h>
#include <Network.h>
#include <Preferences.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <lwip/sockets.h>
#include <mbedtls/md.h>
#include <esp_random.h>
#include <algorithm>
#include <vector>
#include "esp_eth.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "driver/spi_master.h"
#include "dhcpserver/dhcpserver.h"

Preferences memoria;
WebServer server(80);
NetworkUDP artnetUdp;
uint8_t artnetPacket[530];

#define FW_VERSION "0.1.0"
#define BEACON_PORT 6456
#define BEACON_INTERVAL_MS 2000

char mac_str[18];
bool dispositivo_claimed = false;
uint32_t last_beacon_ms = 0;
int beacon_sock = -1;

Preferences factoryPrefs;
Preferences authPrefs;
String claim_code;
uint8_t session_key[32];
uint64_t auth_counter = 0;
uint32_t diagnostico_janela_ate = 0;
uint32_t diagnostico_ok_ate = 0;
uint8_t claim_fails = 0;
uint32_t claim_block_until = 0;

// Interface W5500 própria: flags de servidor DHCP para o handle existir; em
// modo router o cliente DHCP é arrancado manualmente sobre a mesma interface.
esp_netif_t *eth_netif = nullptr;

enum ModoRede {
  MODO_DHCP_CLIENTE,
  MODO_IP_FIXO,
  MODO_DHCP_SERVIDOR
};
volatile ModoRede modo_rede = MODO_DHCP_CLIENTE;

// --- Configurações W5500 (SPI) ---
#define W5500_CS 5
#define W5500_IRQ -1
#define W5500_RST -1
#define W5500_SCK 18
#define W5500_MISO 19
#define W5500_MOSI 23

/*
 * QUANDO LIGARES O INT (código com INT, para trocar depois):
 *
 *   Sketch: ../esp32_w5500_com_int/esp32_w5500_com_int.ino
 *   Core:   2.0.17
 *   W5500:  INT -> GPIO27
 *   DMX:    TX=17, RX=16, EN=4 (esp_dmx 3.1.0)
 *
 * Esse sketch tem o servidor DHCP automático e o driver W5500 por
 * interrupção. Esta versão sem INT usa core 3.x, init esp_eth próprio
 * (polling) e Dmx_ESP32; não é só descomentar o INT aqui, porque a API do
 * core 2.x e a biblioteca DMX são diferentes.
 */

// --- Configurações DMX (iguais ao protótipo antigo) ---
#define DMX_TX 17
#define DMX_EN 4
// GPIO27 é um pino livre. O Dmx_ESP32 usa este pino como toggle; se ficar
// em -1, ele tenta escrever no pino 255 e gera erros. Usar 27 em vez do LED
// onboard (GPIO2) evita alguns mA.
#define DMX_LED 27

dmxTx dmx(&Serial1, DMX_TX, DMX_EN, DMX_LED);

// contagem para reset
int contagem_boots = 0;

// configuração de rede
bool usar_dhcp;
String ip_str, gw_str, mask_str;
String ip_atual = "0.0.0.0";
char mdns_nome[24];
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
        <p style="text-align:center; font-size:12px; margin:0 0 12px;">
          <a href="http://%MDNS%.local" style="color:#3498db;">http://%MDNS%.local</a><br>
          <a href="http://%IP_ATUAL%" style="color:#3498db;">http://%IP_ATUAL%</a>
        </p>
        <form action="salvar" method="POST">
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
  html.replace("%MDNS%", mdns_nome);
  html.replace("%IP_ATUAL%", ip_atual);
  html.replace("%DHCP_CHK%", usar_dhcp ? "checked" : "");
  html.replace("%DISPLAY_IP%", usar_dhcp ? "none" : "block");
  html.replace("%IP%", ip_str);
  html.replace("%GW%", gw_str);
  html.replace("%MASK%", mask_str);
  
  return html;
}

// --- Claim code + autenticação do painel ---
// O claim code é de fábrica (NVS "factory", sobrevive ao reset de config) e
// nunca é enviado. O hub prova posse com HMAC e envia uma chave de sessão
// cifrada por stream HMAC; os pedidos seguintes levam HMAC + contador.
static const char AUTH_HEADER[] = "X-EasyArtNet-Auth";
static const char BASE32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

String normalizarCodigo(String code) {
  code.trim();
  code.toUpperCase();
  code.replace("-", "");
  code.replace(" ", "");
  code.replace(":", "");
  return code;
}

String gerarClaimCode() {
  uint8_t raw[10];
  for (int i = 0; i < (int)sizeof(raw); i++) {
    raw[i] = (uint8_t)esp_random();
  }

  String out;
  uint32_t buffer = 0;
  int bits = 0;
  for (int i = 0; i < (int)sizeof(raw); i++) {
    buffer = (buffer << 8) | raw[i];
    bits += 8;
    while (bits >= 5) {
      out += BASE32[(buffer >> (bits - 5)) & 31];
      bits -= 5;
    }
  }
  return out;
}

void hmacSha256(const uint8_t *key, size_t key_len, const uint8_t *data,
                size_t data_len, uint8_t out[32]) {
  const mbedtls_md_info_t *info =
      mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_hmac(info, key, key_len, data, data_len, out);
}

void derivarChaveClaim(const char *code, uint8_t out[32]) {
  static const char prefix[] = "easyartnet-claim";
  hmacSha256((const uint8_t *)code, strlen(code),
             (const uint8_t *)prefix, strlen(prefix), out);
}

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool hexDecode(const String &in, uint8_t *out, size_t out_len) {
  if (in.length() != out_len * 2) return false;
  for (size_t i = 0; i < out_len; i++) {
    int hi = hexVal(in[i * 2]);
    int lo = hexVal(in[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

String hexEncode(const uint8_t *data, size_t len) {
  static const char HEX_CHARS[] = "0123456789abcdef";
  String out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; i++) {
    out += HEX_CHARS[data[i] >> 4];
    out += HEX_CHARS[data[i] & 0x0f];
  }
  return out;
}

bool jsonString(const String &json, const char *key, String &out) {
  String needle = String("\"") + key + "\"";
  int key_pos = json.indexOf(needle);
  if (key_pos < 0) return false;
  int colon = json.indexOf(':', key_pos + needle.length());
  if (colon < 0) return false;
  int start = json.indexOf('"', colon + 1);
  if (start < 0) return false;
  int end = json.indexOf('"', start + 1);
  if (end < 0) return false;
  out = json.substring(start + 1, end);
  return true;
}

bool constTimeEq(const uint8_t *a, const uint8_t *b, size_t len) {
  uint8_t diff = 0;
  for (size_t i = 0; i < len; i++) diff |= a[i] ^ b[i];
  return diff == 0;
}

void carregarAuth() {
  factoryPrefs.begin("factory", false);
  claim_code = normalizarCodigo(factoryPrefs.getString("claim", ""));
  if (claim_code.length() != 16) {
    claim_code = gerarClaimCode();
    factoryPrefs.putString("claim", claim_code);
  }
  log_i("Claim code: %s", claim_code.c_str());

  authPrefs.begin("auth", false);
  bool claimed = authPrefs.getBool("claimed", false);
  uint8_t key[32];
  size_t key_len = authPrefs.getBytes("key", key, sizeof(key));
  dispositivo_claimed = claimed && key_len == sizeof(key);
  if (dispositivo_claimed) {
    memcpy(session_key, key, sizeof(session_key));
  }
  auth_counter = authPrefs.getULong64("ctr", 0);
}

void salvarSessao(const uint8_t key[32]) {
  memcpy(session_key, key, sizeof(session_key));
  authPrefs.putBytes("key", key, sizeof(session_key));
  authPrefs.putBool("claimed", true);
  authPrefs.putULong64("ctr", 0);
  auth_counter = 0;
  dispositivo_claimed = true;
}

void limparAuth() {
  Preferences auth;
  auth.begin("auth", false);
  auth.clear();
  auth.end();
}

String corpoCanonico() {
  std::vector<std::pair<String, String>> args;
  args.reserve(server.args());
  for (int i = 0; i < server.args(); i++) {
    args.push_back({server.argName(i), server.arg(i)});
  }
  std::sort(args.begin(), args.end());

  String out;
  for (const auto &arg : args) {
    if (out.length()) out += '&';
    out += arg.first;
    out += '=';
    out += arg.second;
  }
  return out;
}

bool autorizado(const char *method, const char *path, const String &canonical) {
  if (diagnostico_ok_ate > 0 &&
      (int32_t)(millis() - diagnostico_ok_ate) < 0) {
    return true;
  }
  if (!dispositivo_claimed) {
    log_w("Auth: placa ainda nao reivindicada");
    return false;
  }

  String header = server.header(AUTH_HEADER);
  int colon = header.indexOf(':');
  if (colon <= 0) {
    log_w("Auth: header ausente em %s %s (%d headers)", method, path,
          server.headers());
    for (int i = 0; i < server.headers(); i++) {
      log_w("  hdr: %s", server.headerName(i).c_str());
    }
    return false;
  }

  uint64_t counter = strtoull(header.substring(0, colon).c_str(), nullptr, 10);
  if (counter <= auth_counter) {
    log_w("Auth: counter %llu <= %llu", (unsigned long long)counter,
          (unsigned long long)auth_counter);
    return false;
  }

  uint8_t tag[32];
  if (!hexDecode(header.substring(colon + 1), tag, sizeof(tag))) {
    log_w("Auth: tag hex invalido");
    return false;
  }

  char ctr[24];
  snprintf(ctr, sizeof(ctr), "%llu", (unsigned long long)counter);
  String data = String(method);
  data += '\n';
  data += path;
  data += '\n';
  data += ctr;
  data += '\n';
  data += canonical;

  uint8_t expected[32];
  hmacSha256(session_key, sizeof(session_key), (const uint8_t *)data.c_str(),
             data.length(), expected);
  if (!constTimeEq(tag, expected, sizeof(tag))) {
    log_w("Auth: HMAC invalido em %s %s counter=%llu", method, path,
          (unsigned long long)counter);
    return false;
  }

  auth_counter = counter;
  authPrefs.putULong64("ctr", counter);
  log_i("Auth OK counter=%llu", (unsigned long long)counter);
  return true;
}

void negar() {
  String html = "<!doctype html><meta charset=utf-8><title>401</title>";
  html += "<h1>401 - painel direto bloqueado</h1>";
  html += "<p>Usa o hub: <code>http://localhost:8080/d/";
  html += mdns_nome;
  html += "/</code></p>";
  html += "<p>Para diagnostico: ativa no hub e abre <code>/diagnostico</code>.</p>";
  server.send(401, "text/html", html);
}

void handleClaim() {
  if (claim_block_until > 0 &&
      (int32_t)(millis() - claim_block_until) < 0) {
    server.send(429, "text/plain", "Muitas tentativas. Tenta mais tarde.");
    return;
  }

  String nonce_hex, cipher_hex, tag_hex;
  String body = server.arg("plain");
  if (!jsonString(body, "nonce", nonce_hex) ||
      !jsonString(body, "cipher", cipher_hex) ||
      !jsonString(body, "tag", tag_hex)) {
    server.send(400, "text/plain", "Pedido de claim inválido.");
    return;
  }

  uint8_t nonce[16], cipher[32], tag[32];
  if (!hexDecode(nonce_hex, nonce, sizeof(nonce)) ||
      !hexDecode(cipher_hex, cipher, sizeof(cipher)) ||
      !hexDecode(tag_hex, tag, sizeof(tag))) {
    server.send(400, "text/plain", "Claim inválido.");
    return;
  }

  uint8_t key[32], stream[32], expected[32], session[32];
  derivarChaveClaim(claim_code.c_str(), key);

  uint8_t kd[9 + 16];
  memcpy(kd, "keystream", 9);
  memcpy(kd + 9, nonce, sizeof(nonce));
  hmacSha256(key, sizeof(key), kd, sizeof(kd), stream);
  for (size_t i = 0; i < sizeof(session); i++) {
    session[i] = cipher[i] ^ stream[i];
  }

  uint8_t td[3 + 16 + 32];
  memcpy(td, "tag", 3);
  memcpy(td + 3, nonce, sizeof(nonce));
  memcpy(td + 3 + sizeof(nonce), cipher, sizeof(cipher));
  hmacSha256(key, sizeof(key), td, sizeof(td), expected);
  if (!constTimeEq(tag, expected, sizeof(tag))) {
    claim_fails++;
    if (claim_fails >= 5) {
      claim_block_until = millis() + 60000;
      claim_fails = 0;
    }
    server.send(401, "text/plain", "Claim code inválido.");
    return;
  }

  salvarSessao(session);
  claim_fails = 0;
  server.send(200, "application/json", "{\"ok\":true}");
  log_i("Claim OK");
}

void handleDiagEnable() {
  if (!autorizado("POST", "/api/diagnostics/enable", "")) {
    negar();
    return;
  }
  diagnostico_janela_ate = millis() + 600000;
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleDiagGet() {
  bool janela = diagnostico_janela_ate > 0 &&
                (int32_t)(millis() - diagnostico_janela_ate) < 0;
  String html = "<!doctype html><meta charset=utf-8><title>Diagnostico</title>";
  html += janela ? "<p>Introduz o claim code para ativar 10 min de painel direto.</p>"
                 : "<p>Janela fechada. Ativa o diagnostico no hub.</p>";
  html += "<form method=post action=/diagnostico><input name=code autofocus>";
  html += "<button>Entrar</button></form>";
  server.send(200, "text/html", html);
}

void handleDiagPost() {
  bool janela = diagnostico_janela_ate > 0 &&
                (int32_t)(millis() - diagnostico_janela_ate) < 0;
  String code = normalizarCodigo(server.arg("code"));
  bool ok = janela && code.length() == claim_code.length() &&
            constTimeEq((const uint8_t *)code.c_str(),
                        (const uint8_t *)claim_code.c_str(), code.length());
  if (!ok) {
    server.send(401, "text/plain", "Diagnostico negado.");
    return;
  }
  diagnostico_ok_ate = millis() + 600000;
  server.send(200, "text/html",
              "<p>Diagnostico ativo 10 min. <a href=/>Abrir painel</a></p>");
}

void onArtNetFrame(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t* data) {
  if (universe == startUniverse) {
    uint16_t len = length > 512 ? 512 : length;
    memcpy(dmx.dmxBuffer() + 1, data, len);
  }
}

// Receção Art-Net mínima via lwIP/NetworkUDP (sem Wi-Fi).
static const uint16_t ART_NET_PORT = 6454;
static const uint16_t ART_DMX_OPCODE = 0x5000;
static const uint8_t ART_DMX_START = 18;

void artnetBegin() {
  artnetUdp.begin(ART_NET_PORT);
}

void artnetRead() {
  int packetSize = artnetUdp.parsePacket();
  if (packetSize < ART_DMX_START || packetSize > (int)sizeof(artnetPacket)) {
    return;
  }
  artnetUdp.read(artnetPacket, sizeof(artnetPacket));
  if (memcmp(artnetPacket, "Art-Net", 8) != 0) {
    return;
  }
  uint16_t opcode = artnetPacket[8] | (artnetPacket[9] << 8);
  if (opcode != ART_DMX_OPCODE) {
    return;
  }
  uint16_t universe = artnetPacket[14] | (artnetPacket[15] << 8);
  uint16_t length = artnetPacket[17] | (artnetPacket[16] << 8);
  onArtNetFrame(universe, length, artnetPacket[12], artnetPacket + ART_DMX_START);
}

// Descoberta passiva para o hub: não depende de DNS/mDNS do sistema.
void enviarBeacon() {
  if (beacon_sock < 0 || ip_atual == "0.0.0.0" ||
      millis() - last_beacon_ms < BEACON_INTERVAL_MS) {
    return;
  }
  last_beacon_ms = millis();

  char payload[192];
  int n = snprintf(payload, sizeof(payload),
                   "{\"id\":\"%s\",\"ip\":\"%s\",\"port\":80,\"fw\":\"%s\","
                   "\"claimed\":%s,\"name\":\"%s\"}",
                   mdns_nome, ip_atual.c_str(), FW_VERSION,
                   dispositivo_claimed ? "true" : "false", mdns_nome);
  if (n <= 0 || n >= (int)sizeof(payload)) {
    return;
  }

  struct sockaddr_in dest = {};
  dest.sin_family = AF_INET;
  dest.sin_port = htons(BEACON_PORT);
  dest.sin_addr.s_addr = 0xFFFFFFFF;  // 255.255.255.255
  sendto(beacon_sock, payload, n, 0, (struct sockaddr *)&dest, sizeof(dest));
}

void iniciarBeacon() {
  beacon_sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (beacon_sock < 0) {
    log_e("Falha ao criar socket do beacon");
    return;
  }
  int yes = 1;
  setsockopt(beacon_sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
  log_i("Beacon UDP %d ativo", BEACON_PORT);
}

void onEthEvent(void *arg, esp_event_base_t base, int32_t id, void *data) {
  if (base != ETH_EVENT || eth_netif == nullptr) {
    return;
  }
  if (id != ETHERNET_EVENT_START && id != ETHERNET_EVENT_CONNECTED) {
    return;
  }

  if (modo_rede == MODO_DHCP_CLIENTE) {
    esp_netif_dhcps_stop(eth_netif);
    esp_netif_dhcpc_start(eth_netif);
  } else if (modo_rede == MODO_DHCP_SERVIDOR) {
    esp_netif_dhcpc_stop(eth_netif);
    esp_netif_dhcps_start(eth_netif);
  } else {
    esp_netif_dhcpc_stop(eth_netif);
    esp_netif_dhcps_stop(eth_netif);
  }
}

esp_err_t aplicarIP(IPAddress ip, IPAddress gw, IPAddress mask) {
  if (eth_netif == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }
  esp_netif_ip_info_t info = {};
  info.ip.addr = static_cast<uint32_t>(ip);
  info.gw.addr = static_cast<uint32_t>(gw);
  info.netmask.addr = static_cast<uint32_t>(mask);
  return esp_netif_set_ip_info(eth_netif, &info);
}

bool iniciarDhcpServidor() {
  if (eth_netif == nullptr) {
    return false;
  }

  esp_netif_dhcpc_stop(eth_netif);
  esp_netif_dhcps_stop(eth_netif);

  esp_err_t err_ip = aplicarIP(IPAddress(10, 0, 0, 1), IPAddress(0, 0, 0, 0), IPAddress(255, 0, 0, 0));

  dhcps_lease_t lease = {};
  lease.enable = true;
  lease.start_ip.addr = static_cast<uint32_t>(IPAddress(10, 0, 0, 2));
  lease.end_ip.addr = static_cast<uint32_t>(IPAddress(10, 0, 0, 101));
  esp_err_t err_lease = esp_netif_dhcps_option(eth_netif, ESP_NETIF_OP_SET, ESP_NETIF_REQUESTED_IP_ADDRESS, &lease, sizeof(lease));

  uint8_t disable_opt = 0;
  esp_err_t err_router = esp_netif_dhcps_option(eth_netif, ESP_NETIF_OP_SET, ESP_NETIF_ROUTER_SOLICITATION_ADDRESS, &disable_opt, sizeof(disable_opt));
  esp_err_t err_dns = esp_netif_dhcps_option(eth_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &disable_opt, sizeof(disable_opt));

  esp_err_t err_start = esp_netif_dhcps_start(eth_netif);
  esp_netif_dhcp_status_t status = ESP_NETIF_DHCP_INIT;
  esp_err_t err_status = esp_netif_dhcps_get_status(eth_netif, &status);

  log_i("DHCP srv: ip=%s lease=%s router=%s dns=%s start=%s status=%s",
        esp_err_to_name(err_ip), esp_err_to_name(err_lease),
        esp_err_to_name(err_router), esp_err_to_name(err_dns),
        esp_err_to_name(err_start), esp_err_to_name(err_status));
  log_i("DHCP srv: state=%d linkUp=%d", (int)status, esp_netif_is_netif_up(eth_netif));

  return err_start == ESP_OK || err_start == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED;
}

bool iniciarW5500() {
  Network.begin();

  spi_bus_config_t buscfg = {};
  buscfg.mosi_io_num = W5500_MOSI;
  buscfg.miso_io_num = W5500_MISO;
  buscfg.sclk_io_num = W5500_SCK;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.data4_io_num = -1;
  buscfg.data5_io_num = -1;
  buscfg.data6_io_num = -1;
  buscfg.data7_io_num = -1;
  buscfg.max_transfer_sz = -1;
  esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    log_e("Falha ao inicializar o SPI do W5500");
    return false;
  }

  spi_device_interface_config_t devcfg = {};
  devcfg.mode = 0;
  devcfg.clock_speed_hz = 20 * 1000 * 1000;
  devcfg.input_delay_ns = 20;
  devcfg.spics_io_num = W5500_CS;
  devcfg.queue_size = 20;

  eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(SPI2_HOST, &devcfg);
  w5500_config.int_gpio_num = -1;
  w5500_config.poll_period_ms = 10;

  eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
  esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
  if (mac == nullptr) {
    log_e("Falha ao criar o MAC do W5500");
    return false;
  }

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

  uint8_t base_mac[6];
  if (esp_read_mac(base_mac, ESP_MAC_ETH) != ESP_OK) {
    log_e("Falha ao obter o MAC base");
    return false;
  }
  uint8_t mac_addr[6];
  esp_derive_local_mac(mac_addr, base_mac);
  esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, mac_addr);

  // O esp_netif do IDF não aceita os flags cliente e servidor juntos. O flag
  // servidor é obrigatório para o handle do dhcps existir; o cliente DHCP é
  // arrancado à mão sobre a mesma interface. Parar já o servidor evita que o
  // evento ETHERNET_EVENT_START o arranque sozinho com IP 0.0.0.0.
  esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
  esp_netif_inherent_config_t netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
  netif_config.flags = (esp_netif_flags_t)((netif_config.flags & ~ESP_NETIF_DHCP_CLIENT) | ESP_NETIF_DHCP_SERVER);
  cfg.base = &netif_config;

  eth_netif = esp_netif_new(&cfg);
  if (eth_netif == nullptr) {
    log_e("Falha ao criar a interface de rede do W5500");
    return false;
  }
  esp_netif_dhcps_stop(eth_netif);
  esp_netif_set_hostname(eth_netif, mdns_nome);

  esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(eth_handle);
  if (glue == nullptr || esp_netif_attach(eth_netif, glue) != ESP_OK) {
    log_e("Falha ao ligar o W5500 ao lwIP");
    return false;
  }

  esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, onEthEvent, nullptr);
  if (esp_eth_start(eth_handle) != ESP_OK) {
    log_e("Falha ao iniciar o W5500");
    return false;
  }

  return true;
}

void setup() {
  Serial.begin(115200);

  // Nome mDNS único por chip: usa os 3 últimos bytes do MAC (evita a OUI,
  // igual em todos os ESP32) para não colidir com "dmx.local" nem com outros nós.
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_ETH);
  mac[0] |= 0x02;  // mesmo MAC local derivado usado no W5500
  snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  snprintf(mdns_nome, sizeof(mdns_nome), "dmx-%02x%02x%02x",
           (unsigned)mac[3], (unsigned)mac[4], (unsigned)mac[5]);
  log_i("Nome mDNS: %s.local  MAC: %s", mdns_nome, mac_str);

  memoria.begin("config", false);

  contagem_boots = memoria.getInt("boots", 0);
  contagem_boots++; 
  memoria.putInt("boots", contagem_boots);

  if (contagem_boots >= 2) {
    log_w("\n[AVISO] 2 arranques rápidos detetados. FACTORY RESET...");
    memoria.clear();
    limparAuth();
    delay(1000);
    ESP.restart();
  }

  usar_dhcp = memoria.getBool("dhcp", true);
  ip_str = memoria.getString("ip", "2.0.0.10");
  gw_str = memoria.getString("gw", "0.0.0.0");
  mask_str = memoria.getString("mask", "255.0.0.0");
  startUniverse = memoria.getInt("univ", 0);

  carregarAuth();

  modo_rede = usar_dhcp ? MODO_DHCP_CLIENTE : MODO_IP_FIXO;

  // W5500 por SPI em polling (sem INT), com DHCP cliente e servidor.
  log_i("A ligar W5500 (sem INT, DHCP auto)...");
  if (!iniciarW5500()) {
    log_e("W5500 indisponível. Verifique as ligações SPI.");
  }

  if (eth_netif && !usar_dhcp) {
    log_i("Modo IP Fixo ativado...");
    IPAddress static_ip, static_gw, static_mask;
    static_ip.fromString(ip_str);
    static_gw.fromString(gw_str);
    static_mask.fromString(mask_str);
    esp_netif_dhcpc_stop(eth_netif);
    esp_netif_dhcps_stop(eth_netif);
    aplicarIP(static_ip, static_gw, static_mask);
  } else if (eth_netif) {
    log_i("Modo DHCP. A aguardar IP (Máximo 5s)...");
    esp_netif_dhcps_stop(eth_netif);
    esp_netif_dhcpc_start(eth_netif);
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
    modo_rede = MODO_DHCP_SERVIDOR;
    if (iniciarDhcpServidor()) {
      log_i("Servidor DHCP interno ATIVADO! A distribuir IPs...");
    } else {
      log_e("Falha ao arrancar o servidor DHCP; vou tentar novamente no loop.");
    }
  }

  if (eth_netif) {
    esp_netif_ip_info_t info = {};
    esp_netif_get_ip_info(eth_netif, &info);
    ip_atual = IPAddress(info.ip.addr).toString();
    log_i("Rede Pronta! IP Final: %s", ip_atual.c_str());
  }

  if (MDNS.begin(mdns_nome)) {
    MDNS.addService("easyartnet", "tcp", 80);
    MDNS.addServiceTxt((const char *)"easyartnet", (const char *)"tcp", (const char *)"id", (const char *)mdns_nome);
    MDNS.addServiceTxt((const char *)"easyartnet", (const char *)"tcp", (const char *)"fw", (const char *)FW_VERSION);
    log_i("mDNS Iniciado! Aceda através de http://%s.local", mdns_nome);
  } else {
    log_e("Falha ao iniciar mDNS; use o IP %s", ip_atual.c_str());
  }

  if (!dmx.configure()) {
    log_e("Falha ao configurar a saída DMX");
  }

  artnetBegin();
  iniciarBeacon();
  log_i("Artnet configurado (NetworkUDP/lwIP, sem Wi-Fi)");

  server.on("/api/info", []() {
    String json = "{\"id\":\"";
    json += mdns_nome;
    json += "\",\"mac\":\"";
    json += mac_str;
    json += "\",\"ip\":\"";
    json += ip_atual;
    json += "\",\"port\":80,\"fw\":\"" FW_VERSION "\",\"claimed\":";
    json += dispositivo_claimed ? "true" : "false";
    json += ",\"name\":\"";
    json += mdns_nome;
    json += "\"}";
    server.send(200, "application/json", json);
  });

  server.on("/", []() {
    if (!autorizado("GET", "/", "")) {
      negar();
      return;
    }
    server.send(200, "text/html", getPaginaWeb());
  });

  server.on("/api/claim", HTTP_POST, handleClaim);
  server.on("/api/diagnostics/enable", HTTP_POST, handleDiagEnable);
  server.on("/diagnostico", HTTP_GET, handleDiagGet);
  server.on("/diagnostico", HTTP_POST, handleDiagPost);

  server.on("/salvar", HTTP_POST, []() {
    String canonical = corpoCanonico();
    if (!autorizado("POST", "/salvar", canonical)) {
      negar();
      return;
    }
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
  enviarBeacon();
  artnetRead();
  server.handleClient();

  if (contagem_boots > 0 && millis() > 4000) {
    memoria.putInt("boots", 0);
    contagem_boots = 0;
  }

  // Se o servidor DHCP falhou por ainda não haver link, volta a tentar.
  static uint32_t dhcp_retry = 0;
  if (modo_rede == MODO_DHCP_SERVIDOR && eth_netif && millis() - dhcp_retry > 2000) {
    esp_netif_dhcp_status_t status = ESP_NETIF_DHCP_INIT;
    esp_err_t err_status = esp_netif_dhcps_get_status(eth_netif, &status);
    if (err_status == ESP_OK && status != ESP_NETIF_DHCP_STARTED && esp_netif_is_netif_up(eth_netif)) {
      // Repete o init completo: o IP pode ter falhado quando não havia link.
      iniciarDhcpServidor();
    }
    dhcp_retry = millis();
  }

  static uint32_t timer = 0;
  if (millis() - timer > 25) {
    dmx.transmit();
    timer = millis();
    }
  }
