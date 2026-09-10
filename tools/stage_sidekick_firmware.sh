#!/usr/bin/env bash
# Copies a built sidekick image into components/sidekick_flasher/target_firmware/
# so the ADV build embeds it (RFC 0001 §5.1). Run before `idf.py build` on the
# ADV app; without this, sidekick_flasher_flash_embedded() just returns
# ESP_ERR_NOT_FOUND and everything else still builds.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SIDEKICK_BUILD="$ROOT/sidekick/build"
DEST="$ROOT/components/sidekick_flasher/target_firmware"

if [[ ! -f "$SIDEKICK_BUILD/sidekick.bin" ]]; then
    echo "error: $SIDEKICK_BUILD/sidekick.bin not found." >&2
    echo "Build it first: (cd sidekick && idf.py build)" >&2
    exit 1
fi

mkdir -p "$DEST"
cp "$SIDEKICK_BUILD/bootloader/bootloader.bin" "$DEST/bootloader.bin"
cp "$SIDEKICK_BUILD/partition_table/partition-table.bin" "$DEST/partition-table.bin"
cp "$SIDEKICK_BUILD/sidekick.bin" "$DEST/sidekick.bin"
# otadata: without it, re-flashing a part that has already OTA'd leaves its
# otadata pointing at ota_1 while the ADV writes ota_0 — it would boot the
# stale slot. Writing the initial (erased) otadata makes the bootloader fall
# back to ota_0, which is what we just wrote.
cp "$SIDEKICK_BUILD/ota_data_initial.bin" "$DEST/ota_data_initial.bin"

echo "Staged:"
ls -la "$DEST"
