#!/usr/bin/env bash
# Writes a full-flash backup back onto a Nano so it can be re-used for
# repeated field-flash testing (RFC 0001 §5.1) instead of being a one-shot
# green part. Takes the Nano back to exactly the state it was in when the
# backup was captured — bootloader, partition table, factory app, NVS, all
# of it (a full 0x0-0x400000 read/write, not just the app partition).
#
# Capture a backup once, before ever field-flashing a given Nano, with:
#   esptool.py --chip esp32c6 -p PORT -b 460800 read_flash 0x0 0x400000 \
#       sidekick/stock_backup/nano_stock_backup.bin
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BACKUP="$ROOT/sidekick/stock_backup/nano_stock_backup.bin"
PORT="${1:-}"

if [[ -z "$PORT" ]]; then
    echo "usage: $0 /dev/cu.usbmodemXXXX" >&2
    exit 1
fi

if [[ ! -f "$BACKUP" ]]; then
    echo "error: no backup at $BACKUP — capture one first (see header of this script)" >&2
    exit 1
fi

esptool.py --chip esp32c6 -p "$PORT" -b 460800 write_flash 0x0 "$BACKUP"

echo "Restored. Re-plug the Nano so it re-enumerates, then re-run tools/stage_nano_firmware.sh + a field-flash test whenever you're ready."
