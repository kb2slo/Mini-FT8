# RFC 0003: Morserino M32 Pocket Port & Zero-App BLE Time Sync

* **Status:** Phase 0 accepted. Track A (CTS) is Backlog [B15](../ROADMAP.md) on ADV+QMX. Track B (Pocket) is Ideas [I17](../ROADMAP.md), **de-prioritized** until USB-host + QMX is proven on the desk (§3.1). Now is B10.
* **Author / Lead:** Jeff Kalikstein, KB2SLO
* **Path:** `docs/rfcs/0003-m32-pocket-port-and-ble-time.md`
* **Hardware in scope:** Cardputer ADV (QMX UAC+CAT native USB-C host). M32 Pocket is a second target **if** §3.1 passes. One-shot BLE CTS is ADV first; Pocket CTS only after I17 USB host works.
* **Does not cover:** RFC 0001 companion GATT / iOS app (I3), clock-obvious UX (I15), sleep-RTC compensation (`RTC_COMPENSATION.md`)

Chat is intake; `docs/ROADMAP.md` is truth. Do not start I17 firmware while it is de-prioritized. No new logic in `main.cpp` ([STYLE.md](../STYLE.md), [RFC 0002](0002-extract-and-boundaries.md)).

---

## 1. Context & Motivation

The Morserino **M32 Pocket** is a QRP field tool on ESP32-S3: 1.9″ TFT, TLV320 codec, 3.5 mm TRRS, rotary encoder + FN, 14500 cell, 8 MB flash, **no PSRAM**. We wanted Mini-FT8 on Pocket to be the same **QMX on USB-C** product as ADV (UAC+CAT). Stock Pocket USB-C is **not** that host port (§3.1). Until a dongle path enumerates a QMX on the desk, track B stays parked. Track A (CTS on ADV) does not wait on that.

FT8 needs slot time within about 1 s. Off-grid SOTA/POTA without a laptop or GPS is the gap. This RFC is a **zero-soldering** path (an OTG/charge dongle is extra kit, not a board mod):

1. **BLE CTS time**, one-shot, then BLE off. iOS is zero-app (native Time Service). Android is an off-the-shelf CTS GATT server (nRF Connect), not a Mini-FT8 app.
2. **Board ops** so one firmware tree can target ADV and Pocket without growing `main.cpp`.
3. **Dual-boot bundle** on 8 MB: stock Morserino CW in one OTA slot, Mini-FT8 in the other, one `0x0` image.

---

## 2. Non-goals

We will **not**:

* Require soldering, extra UART GPS, or any Pocket PCB change. An external USB-C OTG / charge dongle is allowed as field kit; it is not assumed to work until §3.1 is proven.
* Require a MicroSD card on the Pocket
* Keep BLE up during FT8 RX/TX. NimBLE exists only for the Sync Time action, then is deinitialized. This RFC does not land RFC 0001 companion GATT.
* Patch Morserino-32 application source; dual-boot is partition-isolated
* Change the Cardputer ADV `idf.py` / Launcher / `partitions.csv` path
* Add PlatformIO as a product build (the tree is ESP-IDF 5.5.1; the only `platformio.ini` is a vendored M5 example)
* Auto-nudge the clock (I15). Operator-initiated ±100 ms / ±1 s is in scope as a later UI, not in `main.cpp` policy
* Give CTS a grid; Maidenhead stays manual or RFC 0001
* Enable PSRAM to make NimBLE fit
* Mirror the 56-key ADV UI on the encoder as a “driver”
* Ship a Mini-FT8 Android (or iOS) app for time. Android uses nRF Connect or any app that advertises SIG CTS. RFC 0001 remains the companion product.
* Use Pocket TRRS analog audio or the key/PTT jack as the Mini-FT8 radio. If §3.1 fails, we stop — we do not silently switch to analog FT8 in this RFC.

---

## 3. Hardware divergence

| Component | Cardputer ADV | M32 Pocket |
|---|---|---|
| MCU | ESP32-S3FN8 | ESP32-S3 (`pocketwroom`), no PSRAM |
| Display | 1.14″ ST7789, **240×135** | 1.9″ ST7789; Morserino tree **170×320**, plus `pocketwroom-170x240` |
| Audio (FT8) | QMX UAC on USB-C host; ES8311 local | **Blocked on §3.1.** TLV320 / TRRS stay Morserino CW / headphones, not the Mini-FT8 radio |
| Radio attach | USB-C **host** (DFP): QMX UAC+CAT | Native USB **PHY** on the same C connector, but the port is wired as a **device/sink**. QMX host needs an OTG (and, to charge at the same time, a powered) dongle — desk proof, not assumed |
| Input | 56-key matrix | Encoder (push) + FN (case cut-out) |
| Storage | Internal FAT + optional MicroSD | 8 MB flash only |
| Time today | GPS / LoRa GNSS cap / DS3231 / manual | No GPS in the zero-mod rule |

FT8 audio and CAT on ADV come from the QMX USB-C cable. The Pocket TRRS jack is not the Mini-FT8 radio attach. (Morserino’s own FAQ still applies if someone uses that jack for CW: a TRS radio cable can damage the TLV320.)

### 3.1 Pocket USB-C (QRP Labs schematic)

Source: [QRP Labs Pocket schematic](https://qrp-labs.com/images/morserino/Schematic.pdf) (production PCB). Classic Heltec M32 UART-bridge lore does **not** apply.

* **No CP2102 / CH340.** USB-C `D+` / `D−` go to the ESP32-S3 native USB PHY (`IO20` / `IO19`). Stock Morserino `ARDUINO_USB_CDC_ON_BOOT` + 1200 bps touch matches device CDC on that PHY.
* **CC1 / CC2 = 5.1 kΩ to ground.** The connector is a USB-C **UFP / sink**. It asks a PC for 5 V and presents as a gadget.
* **VBUS is an input** to the MCP73871 charger, not a host 5 V source like ADV.

ADV is a USB **host**. QMX is a USB **device**. Two sinks on a C-to-C cable do not become host/device. ESP-IDF can still put the S3 PHY in **host** mode on those pins; copper CC/VBUS will not help a straight QMX cable.

| Cable / dongle | Charge Pocket | QMX UAC+CAT |
|---|---|---|
| USB-C–C into QMX | Only if something else feeds `VBUS` | **No** |
| Simple C-to-A OTG, QMX on A (or A-to-C) | **No** (adapter wants the Pocket to *source* VBUS) | **Maybe** — PHY host + self-powered QMX |
| Powered OTG / hub splitter (5 V in, data to QMX, `VBUS` into Pocket) | **If** 5 V actually lands on Pocket `VBUS` | **Maybe** — same PHY-host path |

Phone “OTG + PD” dongles often need a PD CC controller. Pocket has **resistors**, not PD. Prefer a dumb **5 V + D+/D−** splitter. Desk proof: QMX enumerates UAC+CDC with Mini-FT8 in host mode; Pocket still charges if that is a requirement.

Until that proof, **I17 is de-prioritized.** Do not start Pocket board/encoder/dual-boot firmware. B15 (CTS on ADV) is independent and stays sequenced.

---

## 4. Time synchronization (track A)

Off-grid UTC: BLE phone first, then manual, then optional Wi-Fi SNTP later. Grid is not in this path (§2).

### 4.1 Shared contract

Both phone paths read SIG Current Time (`0x2A2B`, 10 bytes: date-time + **Fractions256**, ~3.9 ms units), apply it with `settimeofday()` (or the existing soft-RTC path) using the **read** timestamp, then disconnect and **deinit NimBLE** before the next FT8 slot. Host-test the payload parse once. Menu: **Sync Time → iPhone** | **Android**. Do not advertise and scan at the same time.

Fractions256 is necessary and not sufficient. Field pass: systematic |DT| well under 1 s vs WWV/CHU or a GPS-synced ADV after a one-shot sync.

### 4.2 iOS (zero app)

iOS does **not** advertise `0x1805` for an ESP32 Central to scan. Apple’s Accessory Design Guidelines: the iPhone is a GATT **server** for Time Service on a connection that already exists. Third-party iOS apps must not publish CTS.

**Workflow (Adafruit `BLEClientCts` / Apple dual-role):**

1. **Sync Time → iPhone.** Mini-FT8 **advertises as a peripheral** (`Mini-FT8-<call>`).
2. Operator: **Settings → Bluetooth**, tap, **Pair**.
3. On that link, firmware is a **GATT client**: discover `0x1805`, read `0x2A2B`. Bonding required; iOS Pair dialog, not silent Just Works.
4. Apply time, drop the link, tear down NimBLE, return to RX.

Do not Central-scan for iPhones.

### 4.3 Android (off-the-shelf app)

Stock Android does **not** serve CTS after a Settings Bluetooth pair, so the iOS flow will not set time on Android.

**Workflow:** the phone is the CTS **server**; Mini-FT8 is **Central**.

1. Operator opens **nRF Connect** (or any GATT-server app), starts a server that advertises Current Time Service `0x1805` and exposes Current Time `0x2A2B` (nRF Connect can fill this from the phone clock).
2. **Sync Time → Android.** Mini-FT8 scans for `0x1805`, connects, reads `0x2A2B` with the same parser as iOS, applies time, disconnects, deinit NimBLE.

No Mini-FT8 Play Store app. Document nRF Connect as the recipe we field-test; any advertiser of SIG CTS is acceptable. RFC 0001 companion is not required for this and is not scheduled here.

**Rejected for Android:** Mini-FT8 as peripheral hoping Settings-pair will expose CTS. **Not Phase 1:** Mini-FT8 hosting a writable CTS for nRF Connect to punch in a 10-byte value by hand.

### 4.4 Manual nudge and Wi-Fi SNTP

* Manual ±100 ms and ±1 s against WWV/CHU: operator-initiated. Encoder on Pocket, keys on ADV. Extracted module; host-test the step math. I15 “clock is obviously wrong” UX is separate and still Ideas.
* Wi-Fi SNTP: optional, later, menu. Bring the radio up, set time, **disable Wi-Fi** before FT8. Not a Phase-1 deliverable. Not required in the field.

### 4.5 RAM (ADV+QMX now; Pocket only after §3.1)

Not the companion; NimBLE is up only for sync. **Sync Time** is specified for ADV now. Pocket CTS (Phase 4b) only after I17 USB host works. ADV+QMX can hit the V2.0.4 failure (NimBLE steals the DMA block CDC needs). Teardown is the intent; **largest DMA block after deinit** is the proof.

Every BLE time PR inherits RFC 0001’s §4 loop (measurement recipe, not companion features):

* DIRAM used / remain (`idf_size.py`)
* 8-bit / internal free
* **Largest DMA block** (the QMX CDC figure)
* 8-bit minimum over a session

Zero-cost when compiled out. Runtime default off. USB host + CDC **before** any NimBLE init. Official bins keep CTS off until the table passes on **ADV+QMX**. Repeat the table on **Pocket+QMX** before enabling it there. If largest DMA does not return to the pre-NimBLE floor, stop. Do not enable PSRAM.

iOS and Android CTS use different GAP roles (peripheral+GATTC vs central+GATTC). Measure teardown for **each** role. They may share one NimBLE build with RFC 0001 later; they do not share a Phase 1 PR with companion GATT.

---

## 5. Board split (track B)

STYLE is not the reason. We would change [STYLE.md](../STYLE.md) in this RFC if a class HAL were the better engineering. It is not, for this split.

ADV vs Pocket is two **flash images**: different `sdkconfig`, pins, partition table, display driver. One binary never holds both boards. A `class Board` / `IDisplay` vtable is how you pick an implementation at runtime (and how you get `dynamic_cast` you cannot use: no RTTI). Here the linker already picks the implementation. That is the same choice we already made for radio: `radio_control_ops_t` (NULL op = not supported), and for ADV hardware: `components/board_cardputer_adv` as C APIs (`board_audio_*`, `board_pins.h`). Host tests stub the same functions; they do not need fake subclasses.

The work that will actually hurt is encoder UX and Pocket USB-host **if §3.1 passes**, not `drawPixel`. Wrapping those in `class Input` / `class UsbHost` does not design the key map or prove QMX enumeration.

**Do this:** `components/board_m32_pocket` plus a `board_ops_t` (or just C APIs with one component linked per target), same QMX `radio_control` backend. Core does not `if (M32)` in `main.cpp`. If the header wants the word HAL, it means that boundary, not `class Hal`.

**Do not do this:** a polymorphic Board/Display/Input hierarchy as the port’s first PR. That would be a tree-wide STYLE reversal (and it collides with I6’s parked object model) without helping QMX-on-USB-C.

If we later need **one** firmware to detect hardware at boot, that is a new RFC and then a class or a runtime ops pointer is in play — and STYLE gets updated in that same turn.

| Ops surface | ADV today | Pocket |
|---|---|---|
| Pins / I²C / I²S | `board_pins.h`, `board_audio`, `board_i2c` | Pocket USB-host / power pins; local codec only if we need it (FT8 audio is QMX UAC) |
| Display | M5 240×135 | LovyanGFX/ST7789 170×320 (and a build flag if 170×240 exists in the wild) |
| Input | Key matrix in `main` / M5 | Encoder + FN → events the UI already consumes, or a later keymap module |
| Radio | `radio_control` QMX/QDX/KH1 | **Same QMX backend** if §3.1 passes. Pocket is not a new analog radio. No `if (M32)` in `main.cpp` |
| Storage | FAT + optional SD | Flash FAT only; skip SD import paths |

Encoder UI is **product**, not pins: character-picker for call/grid, and a written map of ADV keys (`1`–`5` tap, `C`, `M`, abort) to encoder+FN. That map is an RFC 0003 Phase exit, not a driver PR.

`main.cpp` may only shrink. Call sites and wiring only.

---

## 6. Dual-boot and bundling (track B)

ADV `partitions.csv` stays factory + 3 MB FAT (8 MB). Dual-boot is **Pocket-only**. Product builds stay ESP-IDF / `idf.py`, not PlatformIO.

### 6.1 Measured flash (stock hardware + current bins)

Pocket is **8 MB** (Morserino’s own `m32pocket_accessibility.csv`: “8 MB flash”; ADV `sdkconfig` is also `FLASHSIZE_8MB`). That is 8,388,608 bytes.

| Image | Bytes | Notes |
|---|---:|---|
| Morserino Pocket **V8.2** `fw_m32p_V8.2.bin` | 2,271,904 | ~2.17 MiB. Arduino app image. |
| Mini-FT8 `minift8-dev.bin` (`288948c`, flash-at-0x0 merged through factory) | 1,136,416 | Implies factory app ≈ **1,070,880** (file − `0x10000`). Not an 8 MB padded image. |
| ADV factory **slot** (`partitions.csv`) | 5,177,344 (`0x4F0000`) | Mini-FT8 uses ~21% of that slot; 3 MB FAT after it. |
| Morserino accessibility **app0** slot | 2,883,584 (`0x2D0000`) | Their single-app 8 MB table. V8.2 Pocket bin uses 79% of that slot (~0.58 MiB spare). They reclaim the 2nd OTA slot for SPIFFS; **mainline pocketwroom still has two OTA apps**. |

Sum of the two **payloads** today: 2.17 + 1.07 ≈ **3.24 MiB**. Bootloader + tables + NVS are ~0.1 MiB. That leaves on the order of **5 MiB** for slot padding + FAT — *if* we size the two app partitions to the bins, not to 3.5+3.5.

The original draft’s two `0x380000` (3.5 MiB) slots were the scare: 7 MiB of apps, ~0.9 MiB leftover, too small for a useful FAT. That layout is **not** required.

A table in the same family as Morserino’s 8 MB accessibility file (2.75 MiB `app0`, which already fits V8.2) plus a Mini-FT8 slot ~1.75–2.75 MiB still leaves ~2–3 MiB raw FAT — same order as ADV’s 3 MB. NimBLE for CTS is flash-cheap next to those margins (hundreds of KB, not megabytes). **Fail the bundle if either `.bin` does not fit its slot**; do not assume 3.5+3.5.

Illustrative only (Phase 4 reads stock Pocket table and may keep it):

```csv
# Name,     Type, SubType, Offset,   Size
nvs,        data, nvs,     0x9000,   0x5000,
otadata,    data, ota,     0xe000,   0x2000,
nvs_ft8,    data, nvs,     0x10000,  0x6000,
app0,       app,  ota_0,   0x20000,  0x2D0000,
app1,       app,  ota_1,   0x2F0000, 0x1C0000,
fatfs,      data, fat,     0x4B0000, 0x350000,
```

`app0` = Morserino (~2.17 in a 2.75 MiB slot). `app1` = Mini-FT8 (~1.07 in a 1.75 MiB slot). FAT ~3.3 MiB. Do not flash this table on ADV. Offsets must stay aligned; this is a sketch.

### 6.2 NVS, boot picker, Arduino vs IDF

* **NVS:** two apps on one `nvs` will clobber keys. Mini-FT8-only `nvs_ft8`; Morserino keeps `nvs`.
* **Boot picker:** if default is `app0` and we do not patch Morserino, it will never see “encoder held.” GPIO check lives in **our bootloader extra** (or a tiny stub we own). Mini-FT8 menu: “Reboot to Morserino” → `esp_ota_set_boot_partition(app0)`. Encoder held at reset → bootloader forces `app1` this time.
* Arduino vs IDF in one OTA table: still a Phase 4 proof.

### 6.3 Unified image

ESP-IDF / `esptool.py merge_bin` from the Mini-FT8 build, **not** a PlatformIO extra script as the product path. CI for the Pocket target produces one `0x0` 8 MB image: bootloader + table + otadata + both apps.

```bash
esptool.py --chip esp32s3 merge_bin -o M32_Pocket_Unified_FT8.bin \
  --flash_mode dio --flash_size 8MB \
  0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0xf000 ota_data_initial.bin \
  0x20000 morserino_fw.bin \
  0x2F0000 mini_ft8.bin
```

Offsets follow the table in force, not this example.

**Restore-to-stock:** publish (or link) the official Morserino Pocket `0x0` image. Document that Morserino’s web/USB updater can overwrite bootloader + table and destroy dual-boot. Dual-boot operators do not use that updater unless they intend to go back to stock.

---

## 7. Phased implementation

One open firmware slice at a time. **Do not start Phase 2+ until B15 is Done and §3.1 has a desk pass.** I17 is parked until then. STYLE applies from day one.

| Phase | Track | Deliverable | Exit |
|---|---|---|---|
| **0 — RFC** | — | This document + Backlog B15 + Ideas I17 | Phase 0 accepted. B15 sequenced. I17 de-prioritized on §3.1 |
| **1 — CTS time** | A | Named module (not `main.cpp`). Shared `0x2A2B` parse. iPhone: advertise + GATTC. Android: scan `0x1805` + GATTC. NimBLE teardown | **B15 Done-when:** iOS Settings-pair and Android nRF Connect set time on **ADV+QMX**. BLE down before next slot. CAT/`TA` still work. Host: payload parse / Fractions256. RFC 0001 §4 table for **each** GAP role (largest DMA after deinit). Official bins still CTS-off until that table passes |
| **1b — Pocket USB host proof** | B gate | No firmware required. QMX UAC+CDC on Pocket PHY via OTG / powered splitter (§3.1) | Enumerate on the desk. Charge-while-host if that is a requirement. Fail → I17 stays parked; no analog fallback |
| **2 — Pocket board** | B | `board_m32_pocket` + display + encoder events. USB host | Starts only after B15 Done **and** 1b. IDF target builds; ADV unchanged |
| **3 — Encoder UX + QMX on Pocket** | B | Written ADV-key → encoder+FN map; QMX QSO on Pocket | Same unique-callsign / abort rules as ADV, via encoder. Reuse QMX `radio_control`. No `if (M32)` in `main.cpp` |
| **4 — Dual-boot CI** | B | Pocket partition CSV, bootloader GPIO hook, `merge_bin`, size gates | Both apps fit; Morserino still runs from `app0`; Mini-FT8 FAT/NVS isolated; restore-to-stock documented |
| **4b — CTS on Pocket** | A on B | Enable the Phase 1 module on Pocket+QMX | Repeat RFC 0001 §4 on Pocket+QMX; then CTS may turn on there |
| **5 — Optional SNTP** | A | Menu Wi-Fi NTP, then Wi-Fi off | Does not block A. Does not unpark I17 |

Central/scan is **Android-only**. No PlatformIO product target.

---

## 8. Risk analysis

| Risk | Mitigation |
|---|---|
| Central/scan vs iOS | iOS is peripheral + Settings pair + GATTC |
| Android has no native CTS | nRF Connect (or equivalent) advertises `0x1805`; Mini-FT8 Central reads `0x2A2B` |
| Dual GAP roles in one session | Menu picks iPhone vs Android; never advertise and scan together |
| NimBLE vs QMX CDC | ADV+QMX now (B15). Pocket+QMX only after §3.1. RFC 0001 §4; official bins CTS-off until each table passes |
| Pocket USB-C is a UFP/sink | Not a missing CP2102: native PHY, 5.1 kΩ CC, VBUS into MCP73871. Straight C–C to QMX is dead. OTG / powered splitter is Phase 1b. I17 parked until that desk proof. No analog fallback |
| Phone PD OTG dongles | Pocket has no PD CC controller. Prefer dumb 5 V + D+/D− splitters |
| NimBLE deinit does not return RAM | Largest DMA before/after teardown; fail the PR if it does not recover |
| Shared NVS | Second NVS partition for Mini-FT8 only |
| No FAT on 8 MB dual-app | Original 3.5+3.5 slots were the problem. Size slots to bins (§6.1); ~5 MiB remains after 2.17+1.07 payloads |
| Encoder hold in Morserino | Bootloader hook we own |
| Stock Morserino OTA | Docs + restore-to-stock image |
| Arduino vs IDF in one OTA table | Phase 4 hardware proof |
| TRRS damage | Fork README + Pocket UI warning; never recommend TRS radio cables |
| Encoder is not a keyboard | Phase 3 key map; character-picker first |
| Analog/TRRS as FT8 radio | Non-goal. QMX USB-C only |
| `main.cpp` growth | Board/radio modules only; STYLE ratchet |
| Scope glue (board + CTS + dual-boot + SNTP) | One RFC; B15 independent of I17; I17 parked on §3.1; SNTP last |

---

## 9. Firmware PR rules

1. **I17 is parked** until §3.1 / Phase 1b passes on the desk. Do not start Pocket board/encoder/dual-boot firmware while it is de-prioritized. B15 (CTS on ADV) does not wait on I17.
2. **B15 before any later Pocket firmware.** If I17 is unparked, B15 is still Done first.
3. **Phase 1 only with the RAM table** on ADV+QMX (each GAP role). Repeat on Pocket+QMX before enabling CTS there. Runtime BLE off except the sync action. Do not treat “we deinit” as a pass without numbers.
4. **No PlatformIO product target.** Pocket is an IDF board + CI image.

KB2SLO owns the RFC. Firmware stays on `origin/main` (`kb2slo/Mini-FT8`). Do not push `upstream` unless asked.
