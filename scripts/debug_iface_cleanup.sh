#!/usr/bin/env bash
set -euo pipefail

UART_BASE="${1:-/tmp/gateway-vdev}"
I2C_STUB_BUS="${2:-10}"
CAN_PREFIX="${3:-vcan}"
CAN_COUNT="${4:-2}"

echo "[INFO] Cleaning pseudo UART links under $UART_BASE"
for f in "$UART_BASE"/uart-gw* "$UART_BASE"/uart-sim* "$UART_BASE"/gw* "$UART_BASE"/sim*; do
  [[ -e "$f" || -L "$f" ]] && rm -f "$f"
done

echo "[INFO] Stopping socat debug processes"
pkill -f "gateway-debug-uart-socat" >/dev/null 2>&1 || true

if command -v modprobe >/dev/null 2>&1; then
  echo "[INFO] Removing i2c-stub (if loaded)"
  modprobe -r i2c-stub >/dev/null 2>&1 || true
fi

if command -v ip >/dev/null 2>&1; then
  echo "[INFO] Removing debug CAN interfaces"
  for ((i=0; i<CAN_COUNT; i++)); do
    ifname="${CAN_PREFIX}${i}"
    ip link del "$ifname" >/dev/null 2>&1 || true
  done
fi

echo "[INFO] Cleanup done"
