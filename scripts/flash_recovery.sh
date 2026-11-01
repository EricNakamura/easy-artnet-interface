#!/bin/bash
#
# easy-artnet-interface — recuperação do ESP32 + W5500
#
# A flash foi apagada (erase-flash). Este script repõe o bootloader, a
# tabela de partições e o firmware sem INT, em blocos de 4 KB, com retry.
#
# Uso:
#   1. Fecha a Arduino IDE (ou pelo menos o Monitor Serial).
#   2. Desliga o USB.
#   3. Mantém o botão BOOT pressionado.
#   4. Liga o USB e espera 2 segundos; larga o BOOT.
#   5. Corre: ./scripts/flash_recovery.sh
#
# Se falhar a meio, volta a colocar a placa em modo boot e corre outra vez;
# o script retoma no último bloco gravado.
#
# Variáveis opcionais:
#   PORT=/dev/cu.usbserial-0001
#   BAUD=115200
#   ESPTOOL=/caminho/para/esptool
#
set -u

DIR="$(cd "$(dirname "$0")" && pwd)"
IMG="$DIR/../recovery/recovery_esp32_w5500.bin"
WORK="$DIR/../recovery/.work"
LOG="$DIR/../recovery/flash_recovery.log"
PORT="${PORT:-/dev/cu.usbserial-0001}"
BAUD="${BAUD:-115200}"

if [ ! -f "$IMG" ]; then
  echo "ERRO: imagem não encontrada: $IMG"
  exit 1
fi

# Descobre o esptool instalado pela Arduino IDE (prefere a versão mais recente)
if [ -z "${ESPTOOL:-}" ]; then
  for p in "$HOME"/Library/Arduino15/packages/esp32/tools/esptool_py/*/esptool; do
    [ -x "$p" ] && ESPTOOL="$p"
  done
fi
if [ -z "${ESPTOOL:-}" ] || [ ! -x "$ESPTOOL" ]; then
  echo "ERRO: esptool não encontrado. Define ESPTOOL=/caminho/para/esptool"
  exit 1
fi

ESPTOOL_VERSION="$("$ESPTOOL" version 2>/dev/null | grep -Eo '[0-9]+\.[0-9]+\.[0-9]+' | head -1)"
case "$ESPTOOL_VERSION" in
  5.*)
    WRITE_CMD="write-flash"
    BEFORE_NO="no-reset"
    BEFORE_RESET="default-reset"
    AFTER_NO="no-reset"
    AFTER_RESET="hard-reset"
    CHIP_CMD="chip-id"
    ;;
  *)
    WRITE_CMD="write_flash"
    BEFORE_NO="no_reset"
    BEFORE_RESET="default_reset"
    AFTER_NO="no_reset"
    AFTER_RESET="hard_reset"
    CHIP_CMD="chip_id"
    ;;
esac

if [ ! -e "$PORT" ]; then
  echo "ERRO: porta $PORT não existe. Liga o USB e volta a correr."
  exit 1
fi

if command -v lsof >/dev/null 2>&1 && lsof "$PORT" >/dev/null 2>&1; then
  echo "ERRO: a porta $PORT está ocupada (provavelmente pelo Monitor Serial da Arduino IDE)."
  lsof "$PORT"
  echo "Fecha a Arduino IDE / o Monitor Serial e volta a correr."
  exit 1
fi

SHA=$(shasum -a 256 "$IMG" 2>/dev/null | awk '{print $1}')

mkdir -p "$WORK"
if [ ! -f "$WORK/img.sha" ] || [ "$(cat "$WORK/img.sha" 2>/dev/null)" != "$SHA" ]; then
  echo "A preparar os blocos de 4 KB..."
  rm -rf "$WORK"
  mkdir -p "$WORK"
  split -b 4096 "$IMG" "$WORK/chunk_"
  echo "$SHA" > "$WORK/img.sha"
  echo 0 > "$WORK/progress"
fi

chunks=("$WORK"/chunk_*)
if [ ! -f "${chunks[0]}" ]; then
  echo "ERRO: não foi possível criar os blocos em $WORK"
  exit 1
fi
TOTAL=${#chunks[@]}

START=$(cat "$WORK/progress" 2>/dev/null)
case "$START" in
  ''|*[!0-9]*) START=0 ;;
esac

echo "Imagem: $IMG"
echo "Porta:  $PORT @ $BAUD"
echo "esptool: $ESPTOOL ($ESPTOOL_VERSION)"
echo "Blocos: $TOTAL; a começar em $START"
echo

i=$START
while [ "$i" -lt "$TOTAL" ]; do
  chunk="${chunks[$i]}"
  if [ ! -f "$chunk" ]; then
    echo "ERRO: bloco em falta: $chunk"
    exit 1
  fi
  off=$(( i * 4096 ))
  ok=0
  attempt=1
  while [ "$attempt" -le 5 ]; do
    if [ "$attempt" -eq 1 ]; then
      before="$BEFORE_NO"
    else
      before="$BEFORE_RESET"
    fi
    if "$ESPTOOL" --port "$PORT" --baud "$BAUD" \
        --before "$before" --after "$AFTER_NO" \
        "$WRITE_CMD" "$off" "$chunk" >> "$LOG" 2>&1; then
      ok=1
      break
    fi
    echo "  bloco $i falhou (tentativa $attempt); a aguardar a porta..."
    w=0
    while [ "$w" -lt 10 ] && [ ! -e "$PORT" ]; do
      sleep 1
      w=$((w + 1))
    done
    sleep 2
    attempt=$((attempt + 1))
  done
  if [ "$ok" -ne 1 ]; then
    echo
    echo "ERRO no bloco $i (offset $(printf '0x%x' "$off"))."
    echo "Volta a colocar a placa em modo boot (BOOT + USB) e corre outra vez;"
    echo "o script retoma neste bloco. Últimas linhas do log:"
    tail -20 "$LOG"
    exit 1
  fi
  echo $((i + 1)) > "$WORK/progress"
  i=$((i + 1))
  if [ $((i % 10)) -eq 0 ]; then
    echo "  $i/$TOTAL"
  fi
done

echo
echo "Flash completo. A reiniciar a placa..."
"$ESPTOOL" --port "$PORT" --baud "$BAUD" \
  --before "$BEFORE_NO" --after "$AFTER_RESET" \
  "$CHIP_CMD" >/dev/null 2>&1

if [ $? -ne 0 ]; then
  "$ESPTOOL" --port "$PORT" --baud "$BAUD" \
    --before "$BEFORE_RESET" --after "$AFTER_RESET" \
    "$CHIP_CMD" >/dev/null 2>&1
fi

echo "Pronto. Abre o Monitor Serial a 115200 para ver os logs da placa."
