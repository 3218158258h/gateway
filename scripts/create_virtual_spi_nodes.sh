#!/usr/bin/env bash
set -euo pipefail

COUNT="${1:-1}"
BASE_DIR="${2:-/tmp/gateway-spi-vdev}"
MAP_FILE="${3:-/tmp/gateway-vnodes-spi-map.tsv}"
MODE="${4:-${SPI_NODE_MODE:-auto}}"

if ! [[ "$COUNT" =~ ^[0-9]+$ ]] || [[ "$COUNT" -le 0 ]]; then
  echo "[ERROR] count must be a positive integer" >&2
  exit 1
fi

if [[ "$MODE" != "auto" && "$MODE" != "real" && "$MODE" != "pseudo" ]]; then
  echo "[ERROR] mode must be one of: auto | real | pseudo" >&2
  exit 1
fi

mkdir -p "$BASE_DIR"
: > "$MAP_FILE"

real_devs=()
for dev in /dev/spidev*; do
  [[ -e "$dev" ]] || continue
  real_devs+=("$dev")
done

create_real_links() {
  echo "[INFO] Creating $COUNT SPI gateway node link(s) from real /dev/spidev* under $BASE_DIR"
  for ((i=0; i<COUNT; i++)); do
    gw_node="$BASE_DIR/spi-gw${i}"
    target_dev="${real_devs[$i]}"
    rm -f "$gw_node"
    ln -s "$target_dev" "$gw_node"
    printf "%s\t%s\t%s\t%s\t%s\n" "$i" "spi" "$gw_node" "$target_dev" "real" >> "$MAP_FILE"
    echo "[OK] spi-node[$i] gw=$gw_node -> $target_dev mode=real"
  done
  echo
  echo "[INFO] map file: $MAP_FILE"
  echo "[INFO] map columns: index, iface, gw_node, target_dev, mode"
}

create_pseudo_pairs() {
  if ! command -v socat >/dev/null 2>&1; then
    echo "[ERROR] socat not found. Install it first (e.g. apt-get install socat)." >&2
    exit 1
  fi

  echo "[WARN] Falling back to pseudo SPI PTY pairs."
  echo "[WARN] pseudo mode cannot pass SPI ioctl (SPI_IOC_*); only byte-stream integration test."

  for ((i=0; i<COUNT; i++)); do
    gw_node="$BASE_DIR/spi-gw${i}"
    sim_node="$BASE_DIR/spi-sim${i}"
    log_file="/tmp/gateway-vnodes-spi-socat-${i}.log"

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
      echo "[ERROR] Failed to create SPI-like pair index=$i (pid=$pid). See $log_file" >&2
      exit 1
    fi

    printf "%s\t%s\t%s\t%s\t%s\t%s\n" "$i" "spi" "$gw_node" "$sim_node" "$pid" "pseudo" >> "$MAP_FILE"
    echo "[OK] spi-pair[$i] gw=$gw_node <-> sim=$sim_node pid=$pid mode=pseudo"
  done

  echo
  echo "[INFO] map file: $MAP_FILE"
  echo "[INFO] map columns: index, iface, gw_node, sim_node, socat_pid, mode"
  echo "[INFO] socat log: /tmp/gateway-vnodes-spi-socat-<idx>.log"
}

if [[ "$MODE" == "real" ]]; then
  if [[ "${#real_devs[@]}" -lt "$COUNT" ]]; then
    echo "[ERROR] real mode requested, but only found ${#real_devs[@]} /dev/spidev* device(s), count=$COUNT" >&2
    exit 1
  fi
  create_real_links
  exit 0
fi

if [[ "$MODE" == "auto" && "${#real_devs[@]}" -ge "$COUNT" ]]; then
  create_real_links
  exit 0
fi

create_pseudo_pairs
