#!/usr/bin/env bash
set -euo pipefail

COUNT="${1:-1}"
PREFIX="${2:-vcan}"
MAP_FILE="${3:-/tmp/gateway-vnodes-can-map.tsv}"
START_INDEX="${START_INDEX:-0}"

print_usage() {
  cat <<'USAGE'
Usage:
  ./scripts/create_virtual_can_nodes.sh [COUNT] [PREFIX] [MAP_FILE]

Examples:
  ./scripts/create_virtual_can_nodes.sh
  ./scripts/create_virtual_can_nodes.sh 2 vcan /tmp/gateway-vnodes-can-map.tsv
  START_INDEX=10 ./scripts/create_virtual_can_nodes.sh 4 gatewaycan

Notes:
  - 需要 Linux 内核支持 vcan（通常需 root 权限）。
  - 脚本仅创建并拉起 vcan 设备，不会删除已有设备。
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  print_usage
  exit 0
fi

if ! [[ "$COUNT" =~ ^[0-9]+$ ]] || [[ "$COUNT" -le 0 ]]; then
  echo "[ERROR] COUNT must be a positive integer" >&2
  exit 1
fi
if ! [[ "$START_INDEX" =~ ^[0-9]+$ ]]; then
  echo "[ERROR] START_INDEX must be a non-negative integer" >&2
  exit 1
fi
if [[ -z "$PREFIX" ]]; then
  echo "[ERROR] PREFIX must not be empty" >&2
  exit 1
fi

if ! command -v ip >/dev/null 2>&1; then
  echo "[ERROR] ip command not found. Install iproute2 first." >&2
  exit 1
fi

# 尝试加载 vcan 模块；失败时继续，后续由 ip add 给出明确错误。
if command -v modprobe >/dev/null 2>&1; then
  modprobe vcan >/dev/null 2>&1 || true
fi

: > "$MAP_FILE"

echo "[INFO] Creating $COUNT virtual CAN interface(s), prefix=$PREFIX, start_index=$START_INDEX"

for ((i=0; i<COUNT; i++)); do
  idx=$((START_INDEX + i))
  ifname="${PREFIX}${idx}"
  state="created"

  if ip link show "$ifname" >/dev/null 2>&1; then
    state="exists"
  else
    if ! ip link add dev "$ifname" type vcan 2>/tmp/gateway-vcan-add.err; then
      err="$(cat /tmp/gateway-vcan-add.err 2>/dev/null || true)"
      echo "[ERROR] Failed to add $ifname: ${err:-unknown error}" >&2
      rm -f /tmp/gateway-vcan-add.err
      exit 1
    fi
    rm -f /tmp/gateway-vcan-add.err
  fi

  if ! ip link set up "$ifname" 2>/tmp/gateway-vcan-up.err; then
    err="$(cat /tmp/gateway-vcan-up.err 2>/dev/null || true)"
    echo "[ERROR] Failed to set $ifname up: ${err:-unknown error}" >&2
    rm -f /tmp/gateway-vcan-up.err
    exit 1
  fi
  rm -f /tmp/gateway-vcan-up.err

  printf "%s\t%s\t%s\t%s\n" "$idx" "can" "$ifname" "$state" >> "$MAP_FILE"
  echo "[OK] can-if[$idx] name=$ifname state=$state up=1"
done

echo
echo "[INFO] map file: $MAP_FILE"
echo "[INFO] map columns: index, iface, name, state"
echo "[INFO] verify: ip -details link show type vcan"
echo "[INFO] test with cansend/candump if installed:"
echo "       cansend ${PREFIX}${START_INDEX} 123#DEADBEEF"
echo "       candump ${PREFIX}${START_INDEX}"

