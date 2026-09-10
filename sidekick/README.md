# sidekick

M5Stack AtomS3 Lite (ESP32-S3FN8) firmware for the Mini-FT8 companion link.

Retargeted from the NanoC6 (ESP32-C6, SKU C125) on 2026-09-08 — see I3 for why: the AtomS3 Lite has the dual-core headroom the decided WiFi + browser phone path needs (RFC 0001 §5.0/§5.1). A sidekick never needs to be a USB host, so §4.6's finding that the AtomS3 Lite cannot host a QMX does not apply here.
Plan and rationale live in [`docs/rfcs/0001-ble-companion.md`](../docs/rfcs/0001-ble-companion.md); status is tracked as `I3` in [`docs/ROADMAP.md`](../docs/ROADMAP.md). Do not duplicate the plan here.

Separate `idf.py` project from the ADV app — ESP-IDF locks target *and sdkconfig* per project, so the two cannot share one configure even now that both are `esp32s3`. See RFC 0001 §5.1 for how the two builds are meant to be orchestrated together later.

## Build (desk)

```bash
cd sidekick
idf.py build
idf.py flash monitor
```

Target is pinned to `esp32s3` in `CMakeLists.txt`; no `set-target` step needed. The partition table is `partitions.csv`: two 3 MB OTA slots, no factory (RFC 0001 §5.2d). With `otadata` erased the bootloader boots `ota_0`, which is what the ADV's USB-C bootstrap writes. Flash size is pinned to 8 MB in `sdkconfig.defaults` — without it the build takes ESP-IDF's 2 MB default and writes a wrong flash-size field into the bootloader header. `sdkconfig` is gitignored and regenerated, so delete it after changing the defaults or the old value sticks. `PROJECT_VER` is also pinned there — exact git SHA + dirty flag, not ESP-IDF's default `git describe` — so the ADV can compare an exact build identity (RFC 0001 §5.2b), not a fuzzy tag-relative string.

## Field-flash from the ADV (RFC 0001 §5.1)

`../tools/stage_nano_firmware.sh` copies a build here's `build/` into `../components/nano_flasher/target_firmware/`, where the ADV build embeds it and the ADV can write it to a factory ("Green") Nano over its own USB-C host port. Proven on real hardware 2026-09-03.

**Before ever field-flashing a specific physical Nano, back it up** — this lets you re-green it afterward instead of the part being a one-shot:

```bash
esptool.py --chip esp32s3 -b 460800 \
    read_flash 0x0 0x800000 stock_backup/atoms3_stock_backup.bin
```

Note both changes from the NanoC6 form: `--chip esp32s3`, and `0x800000` because the AtomS3 Lite has 8 MB
where the Nano had 4 MB. Reading only the first 4 MB would produce a backup that silently restores garbage
over the upper half.

`../tools/restore_sidekick_stock.sh` writes it back afterward (port optional; it autodetects). `stock_backup/` is gitignored on purpose (vendor firmware, and a snapshot of one specific unit) — this is a personal safety net you (re)create per device, not a repo asset.

## Version identity (RFC 0001 §5.2b)

Every ESP-IDF app embeds an `esp_app_desc_t` (`project_name`, `version`) as the literal first bytes of its DROM segment — for this build, absolute flash offset **`0x20020`** (was `0x10020` before the OTA partition change moved the app from `factory` at `0x10000` to `ota_0` at `0x20000`). Re-derive if target, IDF version, partition table or secure boot changes — **re-verified after the esp32s3 retarget on 2026-09-08 and unchanged**: S3 and C6 both put the bootloader at `0x0` and the app at `0x10000`, and the descriptor is 0x20 into the image. Checked by reading the built `sidekick.bin`, not assumed. The ADV reads that off a Nano's flash before offering to install (`esp_loader_flash_read`, no app cooperation needed) to tell "not installed" from "installed, up to date" from "installed, needs updating" — and once `sidekick` is on PORTA as the daily companion link, it answers the same question at runtime via `esp_app_get_description()` in the handshake reply.

## Status

Boots, logs over its own native USB-C, and beacons its build version on PORTA once a second (RFC 0001 §5.2c).

**Retargeted to `esp32s3` 2026-09-08; PORTA companion link confirmed on AtomS3 Lite hardware 2026-09-09.**
The build is green, the app-descriptor offset is re-verified, and the beacon reaches the ADV. Of the three
NanoC6 carry-overs, the first is now settled and two remain open:

- **Grove pin numbers — CONFIRMED on hardware 2026-09-09.** The ADV logged `X R: 181cbe0-dirty L: c0e52ec`,
  a checksum-valid beacon frame, so `GPIO_NUM_1`/`GPIO_NUM_2` are right on the AtomS3 Lite and no change is
  needed. The version mismatch is not a fault — it means the frame arrived and parsed, which is the thing
  being tested. Reasoning that predicted it, kept because it generalises to the next M5Stack part: `main.c` uses `GPIO_NUM_1` / `GPIO_NUM_2`
  for the PORTA TX/RX pair, carried over from the Nano. Silkscreen checked on the AtomS3 Lite 2026-09-08 and
  it is the same layout, left to right: `G`, `5V`, `G2`, `G1`. M5Stack labels Grove pins with their GPIO
  numbers rather than port ordinals (the original ATOM Lite's Grove is silkscreened `G26`/`G32` for
  GPIO26/GPIO32), so `G1`/`G2` should be GPIO1/GPIO2 and the constants should need no change. Neither is an
  S3 strapping pin (those are GPIO0/3/45/46), and UART1 routes through the GPIO matrix on S3 as it did on
  C6, so there is no fixed-pin constraint. Direction is set by the *ADV's* wiring, not the sidekick's chip:
  straight-through cable, ADV receives on G1 and transmits on G2, so the sidekick transmits on G1 and
  receives on G2. None of that is proof — TEST_PLAN S2 is what settles it.
- **`PORTA_SYNC_BYTE 0xC6`.** Chosen to read as "C6" when the chip was one. It is now just an arbitrary
  non-`$` sync value, and it must keep matching `kCompanionSync` in `main/porta.cpp`. Do not "fix" it on one
  side alone.
- **PORTA can never flash a stock target — settled 2026-09-09, see B42.** The ROM bootloader listens only on
  UART0's default pads (GPIO43/44 on S3), and Grove is GPIO1/GPIO2, so the two ends can never meet. This
  retires the question below rather than answering it: the C6 download-mode failure was masking a deeper
  blocker that the retarget does not lift. A custom second-stage bootloader (RFC 0001 §5.2 "future phase")
  is the only path.
- **The self-restart-into-bootloader finding below is C6-specific** and has not been retested on S3. The
  strapping pin differs (GPIO0, not GPIO9), so "USB-C is the only flash path" is an open question again
  rather than a settled one.

No NimBLE or UART framing yet; the WiFi/HTTP application is new work, not a port (the Nano build never had
WiFi). Those are separate slices per RFC 0001 §7.
