#!/usr/bin/env bash
set -euo pipefail

echo "[WARN] scripts/monitor_virtual_port.sh is deprecated." >&2
echo "[WARN] Use scripts/monitor_virtual_uart_port.sh instead." >&2
exec "$(dirname "$0")/monitor_virtual_uart_port.sh" "$@"
