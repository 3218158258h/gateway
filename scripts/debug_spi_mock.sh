#!/usr/bin/env bash
set -euo pipefail

MODULE_NAME="${1:-spi-mockup}"
DEVICE_NODE="${2:-/dev/spidev0.0}"
MAP_FILE="${3:-/tmp/gateway-debug-spi-map.tsv}"

if ! command -v modprobe >/dev/null 2>&1; then
  echo "[ERROR] modprobe not found" >&2
  exit 1
fi

echo "[INFO] Loading SPI mock module: $MODULE_NAME"
if ! modprobe "$MODULE_NAME"; then
  echo "[ERROR] failed to load module: $MODULE_NAME" >&2
  echo "[HINT] check your kernel mock/stub module name and pass it as arg1" >&2
  exit 1
fi

if [[ ! -e "$DEVICE_NODE" ]]; then
  echo "[WARN] device node not found yet: $DEVICE_NODE"
  echo "[WARN] pass the actual node path as arg2 once created by your module"
fi

: > "$MAP_FILE"
printf "%s\t%s\t%s\t%s\n" "0" "spi" "mock" "$DEVICE_NODE" >> "$MAP_FILE"
echo "[INFO] map file: $MAP_FILE"
echo "[INFO] map columns: index, iface, mode, dev"
