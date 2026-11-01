# Diagnóstico W5500 / DMX

O sketch está em `firmware/w5500_diag/`. Esta app imprime cada passo do
`setup` e um heartbeat no `loop`, para descobrir exatamente onde a placa
reinicia.

> Nota: o diag testa o caminho do `ETH.begin()` do core (W5500/DMX/WebServer),
> não a interface `esp_netif` própria do firmware. Para o DHCP cliente/servidor
> usa o serial da app `w5500_sem_int` e procura as linhas `DHCP srv:`.

## Gravar sem PlatformIO (rápido)

O binário já está compilado em `firmware/w5500_diag/w5500_diag.bin`
(não versionado).

1. Fecha a Arduino IDE / Monitor Serial.
2. Desliga o USB.
3. Segura **BOOT**.
4. Liga o USB, espera 2s e larga **BOOT**.
5. Corre:

```bash
./scripts/flash_app.sh firmware/w5500_diag/w5500_diag.bin
```

6. Abre o Monitor Serial a **115200** (ou usa `screen`/`pio device monitor`).

## Gravar com PlatformIO

```bash
pio run -e w5500_diag -t upload
pio device monitor -e w5500_diag
```

O ambiente usa o platform oficial `espressif32@8.0.0` (core 3.3.12) e o
filtro `esp32_exception_decoder`, para descodificar backtraces.

## O que enviar

Copia as últimas linhas do serial. A última linha antes de reiniciar indica
o passo problemático:

```text
1. Serial OK
2. Preferences.begin=...
3. ETH.begin=...
3b. linkUp=... ip=...
4. dmxTx.configure=...
5. NetworkUDP OK
6. WebServer OK
7. MDNS.begin=...
=== SETUP DONE ===
loop ...
```

- Se para em **3/3b**, o problema está no W5500 (SPI, alimentação ou driver).
- Se para em **4**, o problema está no DMX / `Dmx_ESP32` / pino EN.
- Se para em **5/6/7**, o problema está no Artnet / WebServer / mDNS.
- Se chega a **SETUP DONE** e reinicia no `loop`, o problema está no
  `dmx.transmit()`, no `artnet.read()`, no `handleClient()` ou na alimentação.
- Se nem chega a **1**, o problema é anterior ao `setup` (construtores
  globais ou imagem/bootloader).

O `reset_reason` também ajuda: `8` costuma ser watchdog (RTC WDT), `4` panic,
`1` power-on.
