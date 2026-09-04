#!/usr/bin/env bash
# Copies a built sidekick image into components/nano_flasher/target_firmware/
# so the ADV build embeds it (RFC 0001 §5.1). Run before `idf.py build` on the
# ADV app; without this, nano_flasher_flash_embedded() just returns
# ESP_ERR_NOT_FOUND and everything else still builds.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NANO_BUILD="$ROOT/sidekick/build"
DEST="$ROOT/components/nano_flasher/target_firmware"

if [[ ! -f "$NANO_BUILD/sidekick.bin" ]]; then
    echo "error: $NANO_BUILD/sidekick.bin not found." >&2
    echo "Build it first: (cd sidekick && idf.py build)" >&2
    exit 1
fi

mkdir -p "$DEST"
cp "$NANO_BUILD/bootloader/bootloader.bin" "$DEST/bootloader.bin"
cp "$NANO_BUILD/partition_table/partition-table.bin" "$DEST/partition-table.bin"
cp "$NANO_BUILD/sidekick.bin" "$DEST/sidekick.bin"

echo "Staged:"
ls -la "$DEST"
