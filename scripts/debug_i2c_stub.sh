#!/usr/bin/env bash
set -euo pipefail

BUS_ID="${1:-10}"
CHIP_ADDR="${2:-0x50}"
MAP_FILE="${3:-/tmp/gateway-debug-i2c-map.tsv}"

if ! command -v modprobe >/dev/null 2>&1; then
  echo "[ERROR] modprobe not found" >&2
  exit 1
fi
if ! command -v i2cdetect >/dev/null 2>&1; then
  echo "[ERROR] i2c-tools not found (need i2cdetect/i2cset)" >&2
  exit 1
fi

# 依赖内核 i2c-stub，需 root 权限
modprobe i2c-stub chip_addr="$CHIP_ADDR" bus_num="$BUS_ID"

dev="/dev/i2c-$BUS_ID"
if [[ ! -e "$dev" ]]; then
  echo "[ERROR] stub bus not created: $dev" >&2
  exit 1
fi

: > "$MAP_FILE"
printf "%s\t%s\t%s\t%s\t%s\n" "$BUS_ID" "i2c" "stub" "$dev" "$CHIP_ADDR" >> "$MAP_FILE"
echo "[OK] i2c-stub bus=$BUS_ID dev=$dev chip_addr=$CHIP_ADDR"
echo "[INFO] map file: $MAP_FILE"
echo "[INFO] map columns: bus_id, iface, mode, dev, chip_addr"
echo "[INFO] verify: i2cdetect -y $BUS_ID"
