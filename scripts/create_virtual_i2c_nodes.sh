#!/usr/bin/env bash
set -euo pipefail

COUNT="${1:-1}"
BASE_DIR="${2:-/tmp/gateway-i2c-vdev}"
MAP_FILE="${3:-/tmp/gateway-vnodes-i2c-map.tsv}"

if ! command -v socat >/dev/null 2>&1; then
  echo "[ERROR] socat not found. Install it first (e.g. apt-get install socat)." >&2
  exit 1
fi

if ! [[ "$COUNT" =~ ^[0-9]+$ ]] || [[ "$COUNT" -le 0 ]]; then
  echo "[ERROR] count must be a positive integer" >&2
  exit 1
fi

mkdir -p "$BASE_DIR"
: > "$MAP_FILE"

echo "[INFO] Creating $COUNT virtual I2C-like pair(s) under $BASE_DIR"
echo "[INFO] note: these are byte-stream PTY nodes for integration simulation, not real kernel i2c-dev adapters."

for ((i=0; i<COUNT; i++)); do
  gw_node="$BASE_DIR/i2c-gw${i}"
  sim_node="$BASE_DIR/i2c-sim${i}"
  log_file="/tmp/gateway-vnodes-i2c-socat-${i}.log"

  rm -f "$gw_node" "$sim_node"

  socat -d -d \
    pty,link="$gw_node",raw,echo=0,mode=666 \
    pty,link="$sim_node",raw,echo=0,mode=666 \
    >"$log_file" 2>&1 &

  pid="$!"

  for _ in {1..30}; do
    [[ -e "$gw_node" && -e "$sim_node" ]] && break
    sleep 0.1
  done

  if [[ ! -e "$gw_node" || ! -e "$sim_node" ]]; then
    echo "[ERROR] Failed to create I2C-like pair index=$i (pid=$pid). See $log_file" >&2
    exit 1
  fi

  printf "%s\t%s\t%s\t%s\t%s\n" "$i" "i2c" "$gw_node" "$sim_node" "$pid" >> "$MAP_FILE"
  echo "[OK] i2c-pair[$i] gw=$gw_node <-> sim=$sim_node pid=$pid"
done

echo
echo "[INFO] map file: $MAP_FILE"
echo "[INFO] map columns: index, iface, gw_node, sim_node, socat_pid"
echo "[INFO] socat log: /tmp/gateway-vnodes-i2c-socat-<idx>.log"

