# Protótipo antigo — firmware extraído

Este diretório contém o firmware que estava gravado no ESP32 ligado por USB
(ESP32-D0WD-V3, 4 MB de flash, MAC `68:FE:71:81:68:BC`).

> **Importante:** o código-fonte `.ino` não existe dentro do ESP32. O que está
> na flash é o binário compilado. Estes ficheiros permitem recuperar/analisar
> esse binário, mas não reconstruir automaticamente o código original.

## Ficheiros

| Ficheiro | Conteúdo |
| :--- | :--- |
| `app0.bin` | Aplicação compilada (imagem válida, 324 864 bytes). SHA-256: `62d7374c5d35d0909a94fb6b791601aa58dfb21298e005adc6bc6af5b2e397a0` |
| `partitions.bin` | Tabela de partições lida de `0x8000` |
| `otadata.bin` | Dados OTA lidos de `0xE000` (sem OTA válido; o arranque é pela app0/factory) |

> Os binários não são versionados; ficam apenas na cópia local em
> `prototype_dump/`.

A app foi lida de `0x10000` (partição `app0`, 1,25 MB) e cortada no fim da
imagem válida. O resto da partição estava a `0xFF`.

## O que o firmware usa

Identificado a partir das strings e da desmontagem da imagem:

- **Arduino-ESP32 core 2.0.17** / ESP-IDF 4.4.7 (app compilada em `Mar 5 2024 12:36:24`).
- **esp_dmx 3.1.0** — `dmx_driver_install`, `dmx_set_pin`, `dmx_send`.
- **ArtNet** de `hideakitai/ArtNet` (a string `Arduino ArtNet Protocol by hideakitai/ArtNet`).
- **Ethernet / W5500** pela biblioteca Arduino `Ethernet` (não usa o `ETH.h` do core).
- Não tem servidor web, mDNS nem `Preferences`/NVS (a partição NVS está apagada).
- Não tem SPIFFS/LittleFS (a partição está apagada).

## Configuração recuperada do binário

Valores retirados dos globais e da chamada a `dmx_set_pin`:

| Item | Valor |
| :--- | :--- |
| W5500 CS (`Ethernet.init`) | GPIO5 |
| W5500 SPI | GPIO18 (SCK), GPIO19 (MISO), GPIO23 (MOSI) |
| W5500 INT | Não usado (a biblioteca Ethernet faz polling) |
| DMX port | 1 |
| DMX TX | GPIO17 |
| DMX RX | GPIO16 |
| DMX EN / RTS | GPIO4 |
| MAC usada pelo sketch | `DE:AD:BE:EF:FE:ED` |

O sketch imprime no serial, por esta ordem:

```text
Serial initialized.
Initializing Ethernet...
My IP address: <ip>
Server started.
Art-Net started.
Packet received: size = ...
```

O IP era atribuído no código (IP fixo ou DHCP); não está guardado na NVS.
O valor exato não foi recuperado porque, durante a leitura, a placa entrou em
boot loop/reinício (`try 0x400805e4` no serial) e não chegou a imprimir o IP.

## Relação com o firmware novo (`firmware/esp32_w5500_com_int/`)

O firmware novo foi ajustado para manter a cablagem DMX do protótipo antigo:

- DMX: `TX=17`, `RX=16`, `EN=4` (igual ao antigo).
- W5500: `CS=5`, `SCK=18`, `MISO=19`, `MOSI=23` (igual ao antigo).
- **Única alteração de hardware necessária:** ligar o pino `INT` do W5500 ao
  GPIO27, porque o driver nativo do core 2.x é interrompido (o antigo não usava
  INT). Se o módulo já tiver INT noutro pino, ajustar `W5500_IRQ` no sketch.
- O reset do W5500 ficou em `-1` (o driver faz reset por SPI).

## Repor/verificar o binário (opcional)

O `app0.bin` pode ser gravado de volta em `0x10000`:

```bash
esptool.py --port /dev/cu.usbserial-0001 write_flash 0x10000 prototype_dump/app0.bin
```

Isto substitui apenas a aplicação; o bootloader e a tabela de partições não
estão incluídos. Para um backup completo, ler os 4 MB da flash antes de
gravar qualquer coisa.
