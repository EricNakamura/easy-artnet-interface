#!/bin/bash
#
# Grava uma app (.bin) do ESP32 em 0x10000, em blocos de 4 KB, com retry.
# Usa-se quando o upload normal (Arduino IDE / PlatformIO) falha por causa
# da ligação USB desta placa.
#
# Uso:
#   ./scripts/flash_app.sh firmware/w5500_diag/w5500_diag.bin
#
# Antes:
#   1. Fecha a Arduino IDE / Monitor Serial / screen.
#   2. Desliga o USB.
#   3. Segura BOOT.
#   4. Liga o USB, espera 2s e larga BOOT.
#
# Se falhar a meio, repete o modo boot e corre outra vez; retoma no bloco
# onde parou.
#
set -u

DIR="$(cd "$(dirname "$0")" && pwd)"
IMG="${1:-}"
WORK="$DIR/../recovery/.app_work"
LOG="$DIR/../recovery/flash_app.log"
PORT="${PORT:-/dev/cu.usbserial-0001}"
BAUD="${BAUD:-115200}"

if [ -z "$IMG" ] || [ ! -f "$IMG" ]; then
  echo "Uso: $0 caminho/para/app.bin"
  exit 1
fi

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
  echo "ERRO: a porta $PORT está ocupada. Fecha a IDE/Monitor Serial/screen."
  lsof "$PORT"
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

echo "App:    $IMG"
echo "Porta:  $PORT @ $BAUD"
echo "esptool: $ESPTOOL ($ESPTOOL_VERSION)"
echo "Blocos: $TOTAL; a começar em $START"
echo

i=$START
while [ "$i" -lt "$TOTAL" ]; do
  chunk="${chunks[$i]}"
  off=$(( 0x10000 + i * 4096 ))
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
    echo "Volta a colocar a placa em modo boot e corre outra vez."
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
echo "App gravada. A reiniciar..."
"$ESPTOOL" --port "$PORT" --baud "$BAUD" \
  --before "$BEFORE_NO" --after "$AFTER_RESET" \
  "$CHIP_CMD" >/dev/null 2>&1
if [ $? -ne 0 ]; then
  "$ESPTOOL" --port "$PORT" --baud "$BAUD" \
    --before "$BEFORE_RESET" --after "$AFTER_RESET" \
    "$CHIP_CMD" >/dev/null 2>&1
fi
echo "Pronto. Abre o Monitor Serial a 115200."
