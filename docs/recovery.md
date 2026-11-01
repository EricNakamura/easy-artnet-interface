# Recuperação do ESP32 + W5500

A flash foi apagada pelo `erase-flash` da Arduino IDE. O bootloader e a
tabela de partições desapareceram, por isso a placa já não arranca nem
aparece normalmente. Isto é recuperável: o ESP32 tem o bootloader ROM na
mask ROM, que continua acessível com o botão **BOOT**.

O script `scripts/flash_recovery.sh` repõe:

- bootloader
- tabela de partições
- `boot_app0`
- firmware **sem INT** atualizado (core 3.x + Dmx_ESP32 + Art-Net por
  NetworkUDP, sem Wi-Fi, CPU 160 MHz, toggle no GPIO27 e **DHCP cliente +
  auto-router** em 10.0.0.1)

Grava em blocos de 4 KB, com retry, porque a ligação USB desta placa tem
falhado em gravações grandes.

> [!IMPORTANT]
> **O caminho normal agora é o upload do PlatformIO** com
> `platform = espressif32@8.0.0` (core 3.3.12). Esta recovery só é
> necessária se a flash for apagada/corrompida ou se o bootloader entrar
> em loop. Antes de gravar, fecha o Monitor Serial e o OLA.

## Passos

1. **Fecha a Arduino IDE por completo.** O Monitor Serial segura
   `/dev/cu.usbserial-0001` e impede o esptool de abrir a porta.
2. Desliga o cabo USB.
3. Mantém o botão **BOOT** do ESP32 pressionado.
4. Liga o USB (ainda com BOOT pressionado) e espera 2 segundos.
5. Larga o botão **BOOT**.
6. Corre o script:

```bash
./scripts/flash_recovery.sh
```

Se a porta for outra:

```bash
PORT=/dev/cu.usbserial-0001 ./scripts/flash_recovery.sh
```

Se o esptool não for encontrado:

```bash
ESPTOOL="$HOME/Library/Arduino15/packages/esp32/tools/esptool_py/5.3.1/esptool" \
  ./scripts/flash_recovery.sh
```

## Se falhar a meio

O script guarda o progresso em `recovery/.work/progress`. Volta a colocar a
placa em modo boot (BOOT + USB), fecha a IDE se a tiveres aberto, e corre o
script outra vez. Ele continua no bloco onde parou.

## Depois de recuperar

- Abre o Monitor Serial a **115200**.
- A placa deve imprimir `A ligar W5500...`, `Rede Pronta!` e `Servidor Web a correr!`.
- Por omissão tenta DHCP. Se não houver router/DHCP ao fim de ~5 s, a placa
  transforma-se em servidor DHCP em `10.0.0.1` (lease `10.0.0.2`–`10.0.0.101`);
  o PC ligado diretamente recebe IP automaticamente. Abre `http://10.0.0.1`.
- O painel web e o mDNS (`http://dmx-xxxxxx.local`, sufixo único mostrado no
  serial e no painel) continuam a funcionar. O IP também aparece no painel.

## Voltar ao firmware antigo ou à versão com INT

- O firmware antigo está em `prototype_dump/app0.bin`. Pode ser gravado em
  `0x10000` depois da recuperação.
- A versão com INT está em `firmware/esp32_w5500_com_int/` e precisa do
  core **2.0.17** e do fio INT no GPIO27. Quando ligares o INT, compila
  essa versão e grava a app em `0x10000`.
- A imagem `recovery/recovery_esp32_w5500.bin` não é versionada: fica
  apenas na cópia local.

> **Importante:** não voltes a usar “Erase All Flash” na Arduino IDE. Se
> precisares de gravar, fecha primeiro o Monitor Serial.
