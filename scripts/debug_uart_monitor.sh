#!/usr/bin/env bash
set -euo pipefail

PORT="${1:-}"
if [[ -z "$PORT" ]]; then
  echo "Usage: ./scripts/debug_uart_monitor.sh <port_path>" >&2
  echo "Example: ./scripts/debug_uart_monitor.sh /tmp/gateway-vdev/uart-gw0" >&2
  exit 1
fi

if [[ ! -e "$PORT" ]]; then
  echo "[ERROR] port not found: $PORT" >&2
  exit 1
fi

if command -v hexdump >/dev/null 2>&1; then
  exec cat "$PORT" | hexdump -C
else
  exec cat "$PORT" | od -An -tx1 -v
fi
