#!/usr/bin/env bash
set -euo pipefail

COUNT="${1:-1}"
PREFIX="${2:-vcan}"
MAP_FILE="${3:-/tmp/gateway-debug-can-map.tsv}"
START_INDEX="${START_INDEX:-0}"

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

if command -v modprobe >/dev/null 2>&1; then
  modprobe vcan >/dev/null 2>&1 || true
fi

: > "$MAP_FILE"
echo "[INFO] Creating $COUNT debug CAN interface(s), prefix=$PREFIX, start_index=$START_INDEX"
for ((i=0; i<COUNT; i++)); do
  idx=$((START_INDEX + i))
  ifname="${PREFIX}${idx}"
  state="created"
  if ip link show "$ifname" >/dev/null 2>&1; then
    state="exists"
  else
    ip link add dev "$ifname" type vcan
  fi
  ip link set up "$ifname"
  printf "%s\t%s\t%s\t%s\n" "$idx" "can" "vcan" "$ifname" >> "$MAP_FILE"
  echo "[OK] can-if[$idx] name=$ifname state=$state up=1"
done

echo
echo "[INFO] map file: $MAP_FILE"
echo "[INFO] map columns: index, iface, mode, name"
