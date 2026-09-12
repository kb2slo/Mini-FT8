#!/usr/bin/env bash
# Copies a built sidekick image into components/sidekick_flasher/target_firmware/.
#
# **You do not need to run this.** Since B52 the ADV's own build produces the
# payload (components/sidekick_flasher/CMakeLists.txt), so `idf.py build` alone
# is enough and cannot embed a stale one.
#
# It survives for CI, which builds the sidekick in one job and uploads these
# four files as an artifact for another. Deliberately does *not* invoke
# `idf.py` itself: CI runs this in a plain shell where the ESP-IDF environment
# is not on PATH -- it only exists inside the esp-idf action -- so a build here
# fails. That was tried and broke CI on 2026-09-12.
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
