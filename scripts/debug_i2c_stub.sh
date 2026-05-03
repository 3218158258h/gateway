#!/usr/bin/env bash
set -euo pipefail

BUS_ID="${1:-10}"
CHIP_ADDR="${2:-0x50}"
MAP_FILE="${3:-/tmp/gateway-debug-i2c-map.tsv}"
CHIP_HEX="$(printf "%02x" "$((CHIP_ADDR))")"

if ! command -v modprobe >/dev/null 2>&1; then
  echo "[ERROR] modprobe not found" >&2
  exit 1
fi
if ! command -v i2cdetect >/dev/null 2>&1; then
  echo "[ERROR] i2c-tools not found (need i2cdetect/i2cset)" >&2
  exit 1
fi

# 依赖内核 i2c-stub，需 root 权限
before_buses=()
if compgen -G "/dev/i2c-*" >/dev/null; then
  while IFS= read -r p; do
    before_buses+=("$p")
  done < <(ls -1 /dev/i2c-* 2>/dev/null || true)
fi

modprobe i2c-stub chip_addr="$CHIP_ADDR" bus_num="$BUS_ID"

bus_has_chip() {
  local bus="$1"
  local out
  out="$(i2cdetect -y "$bus" 2>/dev/null || true)"
  grep -Eq "(^|[[:space:]])${CHIP_HEX}([[:space:]]|$)" <<< "$out"
}

pick_bus=""
if [[ -e "/dev/i2c-$BUS_ID" ]] && bus_has_chip "$BUS_ID"; then
  pick_bus="$BUS_ID"
else
  after_buses=()
  if compgen -G "/dev/i2c-*" >/dev/null; then
    while IFS= read -r p; do
      after_buses+=("$p")
    done < <(ls -1 /dev/i2c-* 2>/dev/null || true)
  fi

  # 先尝试“新出现”的总线
  for dev_path in "${after_buses[@]}"; do
    seen=0
    for old_path in "${before_buses[@]}"; do
      if [[ "$dev_path" == "$old_path" ]]; then
        seen=1
        break
      fi
    done
    if [[ "$seen" -eq 0 ]]; then
      bus="${dev_path##*/i2c-}"
      if bus_has_chip "$bus"; then
        pick_bus="$bus"
        break
      fi
    fi
  done

  # 若内核忽略 bus_num（例如固定挂到 i2c-0），则退化为全量扫描
  if [[ -z "$pick_bus" ]]; then
    for dev_path in "${after_buses[@]}"; do
      bus="${dev_path##*/i2c-}"
      if bus_has_chip "$bus"; then
        pick_bus="$bus"
        break
      fi
    done
  fi
fi

if [[ -z "$pick_bus" ]]; then
  echo "[ERROR] i2c-stub loaded but no bus with chip_addr=$CHIP_ADDR found" >&2
  exit 1
fi

dev="/dev/i2c-$pick_bus"

: > "$MAP_FILE"
printf "%s\t%s\t%s\t%s\t%s\n" "$pick_bus" "i2c" "stub" "$dev" "$CHIP_ADDR" >> "$MAP_FILE"
echo "[OK] i2c-stub bus=$pick_bus dev=$dev chip_addr=$CHIP_ADDR"
echo "[INFO] map file: $MAP_FILE"
echo "[INFO] map columns: bus_id, iface, mode, dev, chip_addr"
if [[ "$pick_bus" != "$BUS_ID" ]]; then
  echo "[WARN] requested bus_id=$BUS_ID not honored by kernel, fallback bus_id=$pick_bus"
fi
echo "[INFO] verify: i2cdetect -y $pick_bus"
