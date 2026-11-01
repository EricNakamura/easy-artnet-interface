#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

export EASYARTNET_MEMORY_VAULT=1
cargo build --release >/tmp/easy-artnet-build.log 2>&1
python3 scripts/mock_device.py >/tmp/easy-artnet-mock.log 2>&1 &
MOCK_PID=$!
./target/release/easy-artnet-hub --port 18080 --no-open >/tmp/easy-artnet-hub.log 2>&1 &
HUB_PID=$!
trap 'kill "$MOCK_PID" "$HUB_PID" 2>/dev/null || true' EXIT

for _ in $(seq 1 100); do
  if curl -sf http://127.0.0.1:18080/api/devices >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done

curl -sf -X POST -H 'content-type: application/json' \
  -d '{"address":"127.0.0.1:18081"}' \
  http://127.0.0.1:18080/api/devices/add >/dev/null

curl -sf -X POST -H 'content-type: application/json' \
  -d '{"code":"TESTCLAIMCODE123"}' \
  http://127.0.0.1:18080/api/devices/dmx-test/claim >/dev/null

PANEL=$(curl -sf http://127.0.0.1:18080/d/dmx-test/)
[[ "$PANEL" == *'action="salvar"'* ]]

SAVED=$(curl -sf -X POST -d 'univ=1&dhcp=on' http://127.0.0.1:18080/d/dmx-test/salvar)
[[ "$SAVED" == "SAVED univ=1&dhcp=on" ]]

DIAG=$(curl -sf -X POST http://127.0.0.1:18080/api/devices/dmx-test/diagnostics)
[[ "$DIAG" == *'/diagnostico'* ]]

echo "hub smoke OK"
