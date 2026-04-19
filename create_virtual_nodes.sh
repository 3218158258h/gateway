#!/usr/bin/env bash
set -euo pipefail

COUNT="${1:-1}"
BASE_DIR="${2:-/tmp/gateway-vdev}"
MAP_FILE="${3:-/tmp/gateway-vnodes-map.tsv}"

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

echo "[INFO] Creating $COUNT virtual serial pair(s) under $BASE_DIR"
for ((i=0; i<COUNT; i++)); do
  gw_port="$BASE_DIR/gw${i}"
  sim_port="$BASE_DIR/sim${i}"
  log_file="/tmp/gateway-vnodes-socat-${i}.log"

  rm -f "$gw_port" "$sim_port"

  socat -d -d \
    pty,link="$gw_port",raw,echo=0,mode=666 \
    pty,link="$sim_port",raw,echo=0,mode=666 \
    >"$log_file" 2>&1 &

  pid="$!"

  for _ in {1..30}; do
    [[ -e "$gw_port" && -e "$sim_port" ]] && break
    sleep 0.1
  done

  if [[ ! -e "$gw_port" || ! -e "$sim_port" ]]; then
    echo "[ERROR] Failed to create pair index=$i (pid=$pid). See $log_file" >&2
    exit 1
  fi

  printf "%s\t%s\t%s\t%s\n" "$i" "$gw_port" "$sim_port" "$pid" >> "$MAP_FILE"
  echo "[OK] pair[$i] gw=$gw_port <-> sim=$sim_port pid=$pid"
done

serial_devices="$(awk -F'\t' '{print $2}' "$MAP_FILE" | paste -sd, -)"

echo
echo "[NEXT] gateway.ini [device].serial_devices ="
echo "       $serial_devices"
echo "[INFO] map file: $MAP_FILE"
echo "[INFO] each pair's socat log: /tmp/gateway-vnodes-socat-<idx>.log"
