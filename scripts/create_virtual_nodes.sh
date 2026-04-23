#!/usr/bin/env bash
set -euo pipefail

COUNT="1"
BASE_DIR="/tmp/gateway-vdev"
MAP_FILE="/tmp/gateway-vnodes-map.tsv"
TYPE_SPEC="ble_mesh"

print_usage() {
  cat <<'USAGE'
Usage:
  ./scripts/create_virtual_nodes.sh [COUNT] [BASE_DIR] [MAP_FILE] [--types TYPE1,TYPE2,...]

Examples:
  ./scripts/create_virtual_nodes.sh 3 /tmp/gateway-vdev
  ./scripts/create_virtual_nodes.sh 6 /tmp/gateway-vdev /tmp/gateway-vnodes-map.tsv --types ble_mesh,lora

Type aliases:
  none|0, lora|1, ble_mesh|ble|2
USAGE
}

normalize_type() {
  local raw="$1"
  local lower
  lower="$(echo "$raw" | tr '[:upper:]' '[:lower:]')"
  case "$lower" in
    0|none) echo "none:0" ;;
    1|lora) echo "lora:1" ;;
    2|ble|ble_mesh|ble-mesh) echo "ble_mesh:2" ;;
    *)
      return 1
      ;;
  esac
}

POSITIONAL=()
while (($# > 0)); do
  case "$1" in
    --types)
      shift
      if (($# == 0)); then
        echo "[ERROR] --types requires a value" >&2
        print_usage
        exit 1
      fi
      TYPE_SPEC="$1"
      shift
      ;;
    -h|--help)
      print_usage
      exit 0
      ;;
    *)
      POSITIONAL+=("$1")
      shift
      ;;
  esac
done

if ((${#POSITIONAL[@]} >= 1)); then COUNT="${POSITIONAL[0]}"; fi
if ((${#POSITIONAL[@]} >= 2)); then BASE_DIR="${POSITIONAL[1]}"; fi
if ((${#POSITIONAL[@]} >= 3)); then MAP_FILE="${POSITIONAL[2]}"; fi
if ((${#POSITIONAL[@]} > 3)); then
  echo "[ERROR] too many positional args" >&2
  print_usage
  exit 1
fi

if ! command -v socat >/dev/null 2>&1; then
  echo "[ERROR] socat not found. Install it first (e.g. apt-get install socat)." >&2
  exit 1
fi

if ! [[ "$COUNT" =~ ^[0-9]+$ ]] || [[ "$COUNT" -le 0 ]]; then
  echo "[ERROR] count must be a positive integer" >&2
  exit 1
fi

IFS=',' read -r -a RAW_TYPES <<< "$TYPE_SPEC"
if ((${#RAW_TYPES[@]} == 0)); then
  echo "[ERROR] --types is empty" >&2
  exit 1
fi

TYPE_NAMES=()
TYPE_CODES=()
for raw_type in "${RAW_TYPES[@]}"; do
  token="$(echo "$raw_type" | xargs)"
  if [[ -z "$token" ]]; then
    continue
  fi
  if ! normalized="$(normalize_type "$token")"; then
    echo "[ERROR] unsupported type: $token" >&2
    print_usage
    exit 1
  fi
  TYPE_NAMES+=("${normalized%%:*}")
  TYPE_CODES+=("${normalized##*:}")
done

if ((${#TYPE_NAMES[@]} == 0)); then
  echo "[ERROR] --types has no valid type token" >&2
  exit 1
fi

mkdir -p "$BASE_DIR"
: > "$MAP_FILE"

echo "[INFO] Creating $COUNT virtual serial pair(s) under $BASE_DIR"
echo "[INFO] type sequence (round-robin): ${TYPE_NAMES[*]}"
for ((i=0; i<COUNT; i++)); do
  type_idx=$(( i % ${#TYPE_NAMES[@]} ))
  type_name="${TYPE_NAMES[$type_idx]}"
  type_code="${TYPE_CODES[$type_idx]}"
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

  printf "%s\t%s\t%s\t%s\t%s\t%s\n" "$i" "$type_name" "$type_code" "$gw_port" "$sim_port" "$pid" >> "$MAP_FILE"
  echo "[OK] pair[$i] type=${type_name}(${type_code}) gw=$gw_port <-> sim=$sim_port pid=$pid"
done

serial_devices="$(awk -F'\t' '{print $4}' "$MAP_FILE" | paste -sd, -)"

echo
echo "[NEXT] gateway.ini [device].serial_devices ="
echo "       $serial_devices"
echo "[INFO] map file: $MAP_FILE"
echo "[INFO] each pair's socat log: /tmp/gateway-vnodes-socat-<idx>.log"
echo "[INFO] map columns: index, type_name, type_code, gw_port, sim_port, socat_pid"
