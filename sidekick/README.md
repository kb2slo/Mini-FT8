# sidekick

M5Stack NanoC6 (ESP32-C6, SKU C125) firmware for the Mini-FT8 companion link.
Plan and rationale live in [`docs/rfcs/0001-ble-companion.md`](../docs/rfcs/0001-ble-companion.md); status is tracked as `I3` in [`docs/ROADMAP.md`](../docs/ROADMAP.md). Do not duplicate the plan here.

Separate `idf.py` project from the ADV app — ESP-IDF locks target and sdkconfig per project, so `esp32s3` (ADV) and `esp32c6` (this) cannot share one configure. See RFC 0001 §5.1 for how the two builds are meant to be orchestrated together later.

## Build (desk)

```bash
cd sidekick
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

Target is pinned to `esp32c6` in `CMakeLists.txt`; no `set-target` step needed. `PROJECT_VER` is also pinned there — exact git SHA + dirty flag, not ESP-IDF's default `git describe` — so the ADV can compare an exact build identity (RFC 0001 §5.2b), not a fuzzy tag-relative string.

## Field-flash from the ADV (RFC 0001 §5.1)

`../tools/stage_nano_firmware.sh` copies a build here's `build/` into `../components/nano_flasher/target_firmware/`, where the ADV build embeds it and the ADV can write it to a factory ("Green") Nano over its own USB-C host port. Proven on real hardware 2026-09-03.

**Before ever field-flashing a specific physical Nano, back it up** — this lets you re-green it afterward instead of the part being a one-shot:

```bash
esptool.py --chip esp32c6 -p /dev/cu.usbmodemXXXX -b 460800 \
    read_flash 0x0 0x400000 stock_backup/nano_stock_backup.bin
```

`../tools/restore_nano_stock.sh /dev/cu.usbmodemXXXX` writes it back afterward. `stock_backup/` is gitignored on purpose (vendor firmware, and a snapshot of one specific unit) — this is a personal safety net you (re)create per device, not a repo asset.

## Version identity (RFC 0001 §5.2b)

Every ESP-IDF app embeds an `esp_app_desc_t` (`project_name`, `version`) as the literal first bytes of its DROM segment — for this build, absolute flash offset `0x10020` (re-derive if target/IDF-version/partition-table/secure-boot ever change). The ADV reads that off a Nano's flash before offering to install (`esp_loader_flash_read`, no app cooperation needed) to tell "not installed" from "installed, up to date" from "installed, needs updating" — and once `sidekick` is on PORTA as the daily companion link, it answers the same question at runtime via `esp_app_get_description()` in the handshake reply.

## Status

Boots, logs over its own native USB-C. Field-flashed from the ADV successfully; no NimBLE or UART framing yet — those are separate slices per RFC 0001 §7.
