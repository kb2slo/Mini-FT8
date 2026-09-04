#!/usr/bin/env bash
# Dumps details about the last ADV build: identity (git SHA/dirty), size vs
# flash budget, DIRAM/IRAM usage (the resource RFC 0001 §4 gates the whole
# companion feature on), and whether the built binary actually embedded the
# currently-staged sidekick firmware (RFC 0001 §5.1) rather than
# trusting a stale or no-firmware build silently.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
ELF="$BUILD/mini_ft8.elf"
MAP="$BUILD/mini_ft8.map"
APP_BIN="$BUILD/mini_ft8.bin"
MERGED_BIN="$BUILD/MiniFT8_Merged_Auto.bin"
IDENTITY="$BUILD/generated/build_identity.h"
STAGE="$ROOT/components/nano_flasher/target_firmware"
PARTITIONS="$ROOT/partitions.csv"

if [[ ! -f "$ELF" ]]; then
    echo "error: $ELF not found — build the ADV app first (idf.py build)" >&2
    exit 1
fi

file_size() { stat -f%z "$1" 2>/dev/null || stat -c%s "$1"; }
kib() { python3 -c "print(f'{$1/1024:.1f} KiB')"; }

echo "== Build identity =="
if [[ -f "$IDENTITY" ]]; then
    ver=$(sed -n 's/.*MINIFT8_PRODUCT_VER "\(.*\)"/\1/p' "$IDENTITY")
    sha=$(sed -n 's/.*MINIFT8_GIT_SHA "\(.*\)"/\1/p' "$IDENTITY")
    dirty=$(sed -n 's/.*MINIFT8_GIT_DIRTY \(.*\)/\1/p' "$IDENTITY")
    kind=$(sed -n 's/.*MINIFT8_BUILD_KIND "\(.*\)"/\1/p' "$IDENTITY")
    bin=$(sed -n 's/.*MINIFT8_MERGED_BIN_NAME "\(.*\)"/\1/p' "$IDENTITY")
    dirty_note="clean"; [[ "$dirty" == "1" ]] && dirty_note="DIRTY — uncommitted changes were in the tree at build time"
    echo "  version=$ver kind=$kind sha=$sha ($dirty_note)"
    echo "  merged bin name: $bin"
else
    echo "  (no build/generated/build_identity.h)"
fi

echo
echo "== ADV firmware size =="
if [[ -f "$APP_BIN" ]]; then
    app_size=$(file_size "$APP_BIN")
    factory_hex=$(awk -F',' '/^factory,/{gsub(/ /,"",$5); print $5}' "$PARTITIONS")
    factory_size=$((factory_hex))
    pct=$(python3 -c "print(f'{100*$app_size/$factory_size:.1f}')")
    echo "  mini_ft8.bin (app only):     $app_size bytes ($(kib "$app_size"))"
    echo "  factory partition budget:    $factory_size bytes ($(kib "$factory_size")) — app uses ${pct}%"
fi
if [[ -f "$MERGED_BIN" ]]; then
    merged_size=$(file_size "$MERGED_BIN")
    echo "  MiniFT8_Merged_Auto.bin:     $merged_size bytes ($(kib "$merged_size")) — bootloader + partition table + app, flash at 0x0"
fi

echo
echo "== DIRAM / IRAM (RFC 0001 §4 budget) =="
if [[ -f "$MAP" && -n "${IDF_PATH:-}" && -f "$IDF_PATH/tools/idf_size.py" ]]; then
    python3 "$IDF_PATH/tools/idf_size.py" --format json2 "$MAP" 2>/dev/null | python3 -c '
import json, sys
data = json.load(sys.stdin)
for section in data.get("layout", []):
    name = section["name"]
    if name in ("DIRAM", "IRAM"):
        used, total, free = section["used"], section["total"], section["free"]
        pct = 100 * used / total if total else 0
        print(f"  {name}: {used}/{total} bytes used ({pct:.1f}%), {free} free")
'
else
    echo "  (need build/mini_ft8.map and a sourced esp-idf/export.sh — source it and rebuild)"
fi

echo
echo "== sidekick embed (RFC 0001 §5.1) =="
check_one() {
    local label="$1" symbol="$2" staged_file="$3"
    local start end embedded_size staged_size

    start=$(nm "$ELF" | awk -v s="_binary_${symbol}_start" '$3==s{print $1}')
    end=$(nm "$ELF" | awk -v s="_binary_${symbol}_end" '$3==s{print $1}')
    if [[ -z "$start" || -z "$end" ]]; then
        echo "  MISSING  $label — not embedded in this build (stub path: no firmware was staged when it was built)"
        return 1
    fi
    embedded_size=$(( $((16#$end)) - $((16#$start)) ))

    if [[ ! -f "$staged_file" ]]; then
        echo "  WARN     $label embedded ($embedded_size bytes) but $staged_file is gone now — can't confirm it still matches"
        return 0
    fi
    staged_size=$(file_size "$staged_file")

    if [[ "$embedded_size" == "$staged_size" ]]; then
        echo "  OK       $label — $embedded_size bytes embedded, matches currently-staged $(basename "$staged_file")"
    else
        echo "  MISMATCH $label — $embedded_size bytes embedded vs $staged_size bytes currently staged (build/ is stale — reconfigure and rebuild)"
        return 1
    fi
}

status=0
check_one "bootloader"      "bootloader_bin"      "$STAGE/bootloader.bin"      || status=1
check_one "partition-table" "partition_table_bin" "$STAGE/partition-table.bin" || status=1
check_one "sidekick"        "sidekick_bin"         "$STAGE/sidekick.bin"       || status=1

exit "$status"
