#!/usr/bin/env bash
set -euo pipefail

echo "[WARN] scripts/create_virtual_nodes.sh is deprecated." >&2
echo "[WARN] Use scripts/create_virtual_uart_nodes.sh instead." >&2
exec "$(dirname "$0")/create_virtual_uart_nodes.sh" "$@"
