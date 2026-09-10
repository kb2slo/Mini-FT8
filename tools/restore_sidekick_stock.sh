#!/usr/bin/env bash
# Writes a full-flash backup back onto a sidekick so it can be re-used for
# repeated field-flash testing (RFC 0001 §5.1) instead of being a one-shot
# factory part. Takes it back to exactly the state it was in when the backup
# was captured -- bootloader, partition table, app, NVS, all of it.
#
# Capture a backup once, before ever field-flashing a given device:
#   esptool.py --chip esp32s3 -b 460800 read_flash 0x0 0x800000 \
#       sidekick/stock_backup/atoms3_stock_backup.bin
#
# 0x800000, not the NanoC6 era's 0x400000: the AtomS3 Lite is an 8 MB part and
# a 4 MB read would restore garbage over its upper half.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BACKUP="$ROOT/sidekick/stock_backup/atoms3_stock_backup.bin"

# Port is optional: esptool autodetects, and requiring it made every documented
# invocation something you had to edit before running. Pass one only to
# disambiguate two boards attached at once.
PORT_ARGS=()
if [[ -n "${1:-}" ]]; then
    PORT_ARGS=(-p "$1")
fi

if [[ ! -f "$BACKUP" ]]; then
    echo "error: no backup at $BACKUP -- capture one first (see the header of this script)" >&2
    exit 1
fi

esptool.py --chip esp32s3 "${PORT_ARGS[@]}" -b 460800 write_flash 0x0 "$BACKUP"

echo "Restored. Re-plug the sidekick so it re-enumerates, then re-run tools/stage_sidekick_firmware.sh"
echo "and a field-flash test whenever you're ready."
