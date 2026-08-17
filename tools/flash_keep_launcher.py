#!/usr/bin/env python3
"""Flash mini_ft8.bin into Launcher's OTA app slot.

Reads the *device* partition table (not this repo's partitions.csv). Writes only
the app image. Never writes bootloader, partition table, factory (Launcher),
otadata, or fatfs.

First install Mini-FT8 once from Launcher so an OTA slot exists and is sized.
After that, this is the daily CLI path that leaves Launcher in factory.

Must be USB Serial/JTAG (Launcher menu or Mini-FT8). Leave Launcher USB/MSC.
Radio unplugged from Cardputer USB-C.
"""

from __future__ import annotations

import argparse
import glob
import os
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
PTABLE_OFFSET = 0x8000
PTABLE_LEN = 0xC00
APP_TYPE = 0x00
OTA_SUBTYPE_MIN = 0x10
OTA_SUBTYPE_MAX = 0x1F  # ota_0 .. ota_15
# This Cardputer's Mini-FT8 Launcher slot (PMan name). Prefer when present.
PREFERRED_OTA = "bt4000"


def die(msg: str, code: int = 1) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(code)


def require_idf() -> Path:
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        die(
            "IDF_PATH is unset. In this shell:\n"
            "  source \"$HOME/.espressif/python_env/idf5.5_py3.9_env/bin/activate\"\n"
            "  . \"$HOME/esp/esp-idf/export.sh\""
        )
    path = Path(idf_path)
    gen = path / "components" / "partition_table" / "gen_esp32part.py"
    if not gen.is_file():
        die(f"IDF_PATH does not look like ESP-IDF: {path}")
    return path


def load_gen_esp32part(idf_path: Path):
    sys.path.insert(0, str(idf_path / "components" / "partition_table"))
    import gen_esp32part  # type: ignore

    return gen_esp32part


def find_port() -> str | None:
    try:
        from serial.tools import list_ports
    except ImportError:
        list_ports = None
    if list_ports is not None:
        for p in list_ports.comports():
            hwid = (p.hwid or "").lower()
            desc = (p.description or "").lower()
            dev = p.device or ""
            if "303a:1001" in hwid or "jtag" in desc:
                return dev
            if "usbmodem" in dev.lower() or "usbserial" in dev.lower():
                return dev
    matches = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/tty.usbmodem*"))
    return matches[0] if matches else None


def esptool_cmd() -> list[str]:
    exe = sys.executable
    return [exe, "-m", "esptool"]


def run(cmd: list[str], **kw) -> subprocess.CompletedProcess:
    print("+", " ".join(cmd))
    return subprocess.run(cmd, check=True, **kw)


def read_partition_table(port: str, gen) -> object:
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tmp:
        out = tmp.name
    try:
        run(
            esptool_cmd()
            + [
                "--chip",
                "esp32s3",
                "-p",
                port,
                "read_flash",
                hex(PTABLE_OFFSET),
                hex(PTABLE_LEN),
                out,
            ]
        )
        data = Path(out).read_bytes()
    finally:
        Path(out).unlink(missing_ok=True)
    try:
        return gen.PartitionTable.from_binary(data)
    except Exception as exc:
        die(f"could not parse partition table from the device: {exc}")


def type_name(gen, ptype: int) -> str:
    for name, val in gen.TYPES.items():
        if val == ptype:
            return name
    return f"0x{ptype:x}"


def subtype_name(gen, ptype: int, subtype: int) -> str:
    mapping = gen.SUBTYPES.get(ptype, {})
    for name, val in mapping.items():
        if val == subtype:
            return name
    return f"0x{subtype:x}"


def is_ota_app(p) -> bool:
    return p.type == APP_TYPE and OTA_SUBTYPE_MIN <= (p.subtype or 0) <= OTA_SUBTYPE_MAX


def is_factory_app(p) -> bool:
    return p.type == APP_TYPE and (p.subtype or 0) == 0


def print_table(gen, table) -> None:
    print("Device partition table:")
    print(f"  {'name':<16} {'type':<8} {'subtype':<10} {'offset':<10} {'size'}")
    for p in table:
        print(
            f"  {p.name:<16} {type_name(gen, p.type):<8} "
            f"{subtype_name(gen, p.type, p.subtype or 0):<10} "
            f"0x{p.offset:06x}   0x{p.size:x}"
        )


def pick_ota(table, name: str | None):
    ota = [p for p in table if is_ota_app(p)]
    if not ota:
        die(
            "no OTA app partition on this device. That is Mini-FT8's native "
            "factory-only layout (Launcher is not resident).\n"
            "Flash Launcher at 0x0, install Mini-FT8 once from Launcher "
            "(SD or WUI), then rerun this script.\n"
            "Do not use `idf.py flash` or `idf.py app-flash` on a Launcher unit."
        )
    if name:
        for p in ota:
            if p.name == name:
                return p
        die(f"OTA partition '{name}' not found. Have: {', '.join(p.name for p in ota)}")
    for p in ota:
        if p.name == PREFERRED_OTA:
            return p
    if len(ota) == 1:
        return ota[0]
    for p in ota:
        if p.name == "ota_0":
            print(f"note: multiple OTA slots ({', '.join(p.name for p in ota)}); using ota_0")
            print("      pass --partition NAME to choose another")
            return p
    print(f"note: multiple OTA slots; using {ota[0].name}")
    return ota[0]


def flash_ota(port: str, part, image: Path) -> None:
    size = image.stat().st_size
    if size > part.size:
        die(
            f"{image} is {size} bytes but '{part.name}' is only {part.size} "
            f"(0x{part.size:x}). Install Mini-FT8 once from Launcher so PMan "
            "sizes the slot, or enlarge that OTA partition in PMan."
        )
    # write-flash erases only the image sectors, not the whole OTA slot.
    run(
        esptool_cmd()
        + [
            "--chip",
            "esp32s3",
            "-p",
            port,
            "write_flash",
            "--flash_mode",
            "dio",
            "--flash_freq",
            "80m",
            "--flash_size",
            "8MB",
            hex(part.offset),
            str(image),
        ]
    )


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-p", "--port", help="USB serial port (default: auto-detect)")
    ap.add_argument(
        "--partition",
        help="OTA partition name (default: bt4000 if present, else the only ota_* slot, else ota_0)",
    )
    ap.add_argument(
        "--bin",
        type=Path,
        default=REPO / "build" / "mini_ft8.bin",
        help="app image (default: build/mini_ft8.bin, not the merged image)",
    )
    ap.add_argument("--no-build", action="store_true", help="do not run idf.py build first")
    ap.add_argument(
        "--dry-run",
        action="store_true",
        help="print the device table and chosen slot; do not write flash",
    )
    args = ap.parse_args()

    if args.bin.resolve() != (REPO / "build" / "mini_ft8.bin").resolve():
        if "merged" in args.bin.name.lower() or args.bin.name.startswith("MiniFT8"):
            die(f"{args.bin} looks like a merged image. Use mini_ft8.bin only.")

    idf_path = require_idf()
    gen = load_gen_esp32part(idf_path)

    port = args.port or find_port()
    if not port:
        die(
            "no USB serial device. Leave Launcher USB/MSC, unplug the radio, "
            "and plug the Cardputer in so /dev/cu.usbmodem* appears."
        )

    if not args.no_build and not args.dry_run:
        run(["idf.py", "-C", str(REPO), "build"])
    elif not args.no_build and args.dry_run:
        print("note: --dry-run skips idf.py build")

    table = read_partition_table(port, gen)
    print_table(gen, table)
    factory = [p for p in table if is_factory_app(p)]
    target = pick_ota(table, args.partition)

    if is_factory_app(target) or target.offset == 0 or target.name in {"nvs", "otadata", "phy_init"}:
        die(f"refusing to write '{target.name}' at 0x{target.offset:x}")

    print()
    print(f"Port:      {port}")
    print(f"Image:     {args.bin}")
    if factory:
        print(f"Launcher:  {factory[0].name} @ 0x{factory[0].offset:x} (not written)")
    print(f"Target:    {target.name} @ 0x{target.offset:x} size 0x{target.size:x}")

    if args.dry_run:
        print("dry-run: no flash write")
        return

    if not args.bin.is_file():
        die(f"missing {args.bin}. Run without --no-build, or idf.py build first.")

    flash_ota(port, target, args.bin)
    print()
    print("Wrote Mini-FT8 to the OTA slot. Launcher in factory is unchanged.")
    print("Reset: Mini-FT8 if that slot is the boot default; catch Launcher on the splash.")


if __name__ == "__main__":
    main()
