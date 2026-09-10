#!/usr/bin/env bash
# Writes a full-flash backup back onto a sidekick, so a device can be re-used
# for repeated field-flash testing (RFC 0001 §5.1) instead of being a one-shot
# part. Takes it back to exactly the state it was in when the backup was
# captured -- bootloader, partition table, app, NVS, all of it.
#
# READ THIS BEFORE RUNNING IT. The name says "stock". The backup is whatever
# happened to be on the device the day it was captured, which is not the same
# thing and on 2026-09-09 was not stock at all: it held `atoms3_qmx_host_poc`,
# the USB-host proof-of-concept from RFC 0001 §4.6. That image configures the
# S3's USB peripheral as a *host*, so it presents no USB device whatsoever --
# no CDC, no serial-JTAG. Restoring it makes the board look dead:
#
#   * no /dev/cu.usbmodem* appears, and esptool's autodetect silently falls
#     through to some other serial device on the machine
#   * plugging it into the ADV shows nothing at all, because two USB hosts
#     cannot enumerate each other
#
# It is NOT bricked -- the S3's ROM bootloader is in mask ROM and no flash
# write can touch it. Recover with:
#
#   1. plug the board into the computer
#   2. hold the reset button ~2 s until the green LED lights, then release
#   3. ls /dev/cu.usbmodem*        (a port should now exist)
#   4. cd sidekick && idf.py -p /dev/cu.usbmodemXXXX flash monitor
#   5. unplug and replug: the flash leaves the force-download bit set, so the
#      first reset lands back in the ROM ("waiting for download") until the
#      board sees a real power cycle
#
# Because of all that, this script refuses to run without --yes. It prints what
# the backup actually contains first; read that line before answering.
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

CONFIRMED=0
PORT_ARGS=()
for arg in "$@"; do
    case "$arg" in
        --yes) CONFIRMED=1 ;;
        # Port is optional: esptool autodetects. Pass one to disambiguate two
        # boards at once -- or when the device on the other end is not
        # enumerating, which is exactly the situation this script creates.
        *)     PORT_ARGS=(-p "$arg") ;;
    esac
done

if [[ ! -f "$BACKUP" ]]; then
    echo "error: no backup at $BACKUP -- capture one first (see the header of this script)" >&2
    exit 1
fi

# Say what is really in there, rather than trusting the filename. Parses the
# partition table at 0x8000 for the first app partition, then reads the
# esp_app_desc_t at its base + 0x20.
python3 - "$BACKUP" <<'PY'
import sys
d = open(sys.argv[1], 'rb').read()
app = None
off = 0x8000
while off < 0x8000 + 0xC00:
    e = d[off:off + 32]
    if e[:2] != b'\xaa\x50':
        break
    if e[2] == 0 and app is None:            # first app partition
        app = int.from_bytes(e[4:8], 'little')
    off += 32
if app is None:
    print("  !! no app partition found in the backup's table")
    sys.exit(0)
desc = d[app + 0x20:app + 0x20 + 144]
if int.from_bytes(desc[0:4], 'little') != 0xABCD5432:
    print("  !! no valid app descriptor -- this backup may be truncated")
    sys.exit(0)
name = desc[48:80].split(b'\x00')[0].decode('ascii', 'replace')
ver = desc[16:48].split(b'\x00')[0].decode('ascii', 'replace')
print(f"  backup contains: project '{name}', version '{ver}'")
if name != 'sidekick':
    print(f"  WARNING: '{name}' is not sidekick firmware. If it is a USB-host")
    print("  application it will present no USB device at all -- see this")
    print("  script's header for the hold-reset-2s recovery.")
PY

if [[ "$CONFIRMED" -ne 1 ]]; then
    echo
    echo "Refusing to write without --yes. Re-run as:"
    echo "  tools/restore_sidekick_stock.sh --yes [PORT]"
    exit 1
fi

esptool.py --chip esp32s3 "${PORT_ARGS[@]}" -b 460800 write_flash 0x0 "$BACKUP"

echo
echo "Restored. If the board now presents no serial port, that is the image, not a"
echo "brick: hold reset ~2 s until the green LED, release, then re-flash. See the"
echo "header of this script."
