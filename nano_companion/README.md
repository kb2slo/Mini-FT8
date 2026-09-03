# nano_companion

M5Stack NanoC6 (ESP32-C6, SKU C125) firmware for the Mini-FT8 companion link.
Plan and rationale live in [`docs/rfcs/0001-ble-companion.md`](../docs/rfcs/0001-ble-companion.md); status is tracked as `I3` in [`docs/ROADMAP.md`](../docs/ROADMAP.md). Do not duplicate the plan here.

Separate `idf.py` project from the ADV app — ESP-IDF locks target and sdkconfig per project, so `esp32s3` (ADV) and `esp32c6` (this) cannot share one configure. See RFC 0001 §5.1 for how the two builds are meant to be orchestrated together later.

## Build (desk)

```bash
cd nano_companion
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

Target is pinned to `esp32c6` in `CMakeLists.txt`; no `set-target` step needed.

## Status

Skeleton only: boots, logs over its own native USB-C. No NimBLE, no UART framing, no flasher/embed integration with the ADV build yet — those are separate slices per RFC 0001 §7.
