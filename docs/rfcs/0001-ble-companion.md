# RFC 0001: Optional BLE Companion (Add-on, Not a Second Radio UI)

* **Status:** Draft RFC (Phase 0). **Same-chip NimBLE on ADV+QMX failed** the §4 DMA gate (2026-08-31). Companion BLE moves **off-chip**.
* **Author / Lead:** Jeff Kalikstein, KB2SLO
* **Target File Path:** `docs/rfcs/0001-ble-companion.md`
* **Companion Target:** iPhone first (Android later once the iOS GATT surface is stabilized)
* **Radio:** Cardputer ADV + QMX (USB host). First BLE brick: **M5Stack NanoC6** on ADV UART. Distinct from one-shot CTS on the ADV ([B15](../ROADMAP.md)).
* **Ask:** Keep this document as the companion plan. Do not land NimBLE-on-ADV for a live QMX session.

---

## 1. Context & Motivation

The Cardputer ADV is the right place to decode, autoseq, CAT, and TX. A phone is the right place for maps, QRZ lookups, grid helpers, and PSK Reporter spots. Those auxiliary tasks compete heavily with the 240×135 screen and the ESP32-S3 internal DRAM budget if attempted on-device.

While USB Drive mode (`C`) moves `.adi` log files to a computer, it is awkward on iOS via Apple Files. Operators in the field frequently have cellular connectivity on their phone but no laptop. A low-overhead BLE link that operates without a Wi-Fi Access Point allows the phone to pull logs and monitor live decodes while Mini-FT8 continues running uninterrupted.

**How BLE attaches:** the ADV does **not** run NimBLE while the QMX is up. A second MCU (NanoC6) owns BLE. The ADV talks TTL UART. The phone talks GATT to the Nano. KB2SLO will lead the UART framing, the Nano firmware, and the iOS application in the open against this specification.

---

## 2. Non-Goals (Explicit Bounding)

This project is **not** a revival of the V2.0.x BLE Terminal / native-client stack.

We will **not**:
* Run NimBLE on the ADV at the same time as QMX UAC/CDC (field-failed; §4.1b)
* Mirror or replace `R` / `T` / `S` screens or menus on the phone
* Stream the waterfall over BLE
* Expose JSON RPC endpoints for core commands
* Require Wi-Fi, a hotspot, or AP mode on the ADV
* Make BLE or the Nano mandatory at boot
* Block QMX USB-host CAT
* Turn on SPIRAM / PSRAM on the ADV to “make room” for NimBLE. USB CDC and UAC need **internal DMA-capable** RAM. External RAM does not fix the V2.0.4 / 2026-08-31 failure mode.
* Ship on-chip `ENABLE_BLE` in official `dev` / tagged binaries
* Treat two ATOMs/Nanos (no USB-host brain) as a QMX radio
* Use a Morserino-32 or M32 Pocket as the BLE coprocessor (classic 4-pin header or Pocket USB). Leave those boxes as CW. I17 (Pocket as a Mini-FT8 *host*) is Ideas, not this path.
* Use IR or the headphone jack as the companion pipe

The Cardputer remains the single source of truth and operation. The phone acts strictly as a **spectator and librarian**.

---

## 3. Engineering Retrospective (V2.0.4 BLE Removal)

V2.0.4 pulled BLE (`stabilize startup and remove BLE`) for real engineering reasons:

| Old Design Pitfall | Root Cause / Impact |
|---|---|
| NimBLE GATT as a full second UI | ~50 KB internal DRAM consumed continuously |
| NimBLE init before USB CDC-ACM | Silent CDC allocation failure → **QMX TX dead** |
| BLE + FT4 in one binary | LDPC decoder needed that DRAM; BLE skipped in FT4 at boot |
| Waterfall notify + JSON snapshots | Excessive CPU load and timing jitter on the decode/UAC path |

Mainline BLE re-introduction **must** treat these failure modes as hard engineering constraints.

The leftover internal RAM after linking is **not** a BLE allowance sitting next to the radio stack. It **is** the QMX heap: USB host, CDC, UAC, decode, and later FATFS workers allocate from it. A 50 KB always-on NimBLE cost that looked “fine” against a 90 KB link-time remainder is what made CDC fail.

---

## 4. Internal RAM Budget and Measurement-Driven Development

Flash is not the constraint on the ADV. Internal DIRAM is. PSRAM is off (`CONFIG_SPIRAM` unset) on purpose for this product.

### 4.1 Snapshot (BLE off, 2026-08-19)

`idf.py size` on the then-current `main` app map (`ENABLE_BLE` not in the tree):

| Region | Used | Remain | Total |
|---|---:|---:|---:|
| **DIRAM** | 247,547 (**72%**) | **94,213** | 341,760 |
| IRAM (16 KB bank) | 16,384 (100%) | 0 | 16,384 |

DIRAM mix: BSS ~160 KB, IRAM-resident `.text` ~70 KB, `.data` ~17 KB.

That **94 KB remain is heap at link time**. Runtime takes stacks and buffers from it (`app_core0` 12 KB, USB/UAC tasks, waterfall blit ~8.6 KB, audio pipeline, copy-to-SD worker 12 KB while a copy runs, and any future Station-save worker). A live QMX session is therefore well below 94 KB free. PERF (`8B` / `IN` / `DM`) and `log_mem_caps` are the live view; `DECODE_HEAP` (roadmap B8) is a noisy decode-time probe, not a CI substitute.

### 4.1b Field DMA (2026-08-31, ADV + QMX, CTS NimBLE-in-binary)

PERF **P** screen, **DM** line, **L** = largest DMA block (KiB). Same session family as B15 CTS on `b15-cts-iphone` (NimBLE linked; not an `ENABLE_BLE` off build). Full `MEM:` byte logs were not captured (QMX owned USB-C).

| Checkpoint | DM **L** | Result |
|---|---:|---|
| Boot, no **S**, no **H** | 44K | Floor |
| **H → 1** only (BLE up, no QMX) | 26K | Advertised |
| **S → 2** only (QMX UAC, no BLE start) | 31K | Radio OK |
| **S → 2**, then **H → 1** | ~29K | **`Init fail`** (`nimble_port_init`) |
| **H → 1**, then **S → 2** | 2K | **CAT dead** |

Both init orders fail. Leftover arithmetic (BLE keeps ~18K of the big block, QMX ~13K, 18+13 < 44) does **not** describe *start*: each stack wants a ~40K-class contiguous hole. QMX after BLE and BLE after QMX both lose.

B15 one-shot CTS is “unplug QMX, sync, tear NimBLE down, **then** plug in and **S → 2**.” That is not a companion, and it is not NimBLE beside a live USB host. Parking UAC around CTS (cable stays in) is [ROADMAP B17](../ROADMAP.md), after B15; it is not this RFC. Contract: [ROADMAP B15](../ROADMAP.md).

### 4.2 What “fits” means

| Claim | Verdict |
|---|---|
| NimBLE on the ADV while QMX UAC/CDC is up | **Does not fit.** §4.1b |
| Old ~50 KB continuous NimBLE (second UI) on a live QMX session | Does **not** fit |
| Spectator NimBLE 20–30 KB on the same S3 as QMX | **Rejected** after measurement |
| Wi-Fi + lwIP on the ADV to replace BLE | Worse internal RAM, not better |
| USB hub on ADV (`CONFIG_USB_HOST_HUBS_SUPPORTED` is off) | More host DMA; not enabled |
| Another ESP32-S3 / PSRAM board | Same ~512 KB **internal** SRAM; USB still needs DIRAM |
| CoreS3 OTG + proto + ATOM | Same UART architecture; new UI + unproven QMX host. Later, not the first brick |
| UART to a second MCU; that MCU runs NimBLE | **The path.** First brick: NanoC6. ADV heap stays the QMX heap |
| Morserino / M32 Pocket as that second MCU | **Rejected.** Not a companion brick. |

The number that killed QMX TX was not DIRAM remain. It was **no contiguous DMA block left for CDC**.

### 4.3 Metrics (ADV, if anyone re-opens on-chip BLE)

| Metric | How | What it tells |
|---|---|---|
| DIRAM used / remain | `python $IDF_PATH/tools/idf_size.py build/mini_ft8.map` | Static cost of NimBLE in the ADV binary |
| 8-bit / internal free | PERF + `log_mem_caps` | Runtime heap after tasks |
| **Largest DMA block** | PERF **DM** **L**, or `heap_caps_get_largest_free_block(MALLOC_CAP_DMA)` | The QMX-safe figure |
| 8-bit minimum ever | `heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)` | Fragmentation and decode peaks |

**Field capture (cannot fake in CI):** QMX+ on USB-C, UAC streaming, FT8. Do not treat “heap free still looks like tens of KB” as a pass if **largest DMA** collapsed.

### 4.4 On-chip loop (closed)

§4.4 steps 1–7 were the original Phase 1 (CDC first, then NimBLE, soak, linker floor). B15 CTS ran the init-order probe on a NimBLE-in-binary build. **Stop.** Do not enable PSRAM. Do not trim NimBLE on the ADV hoping 31K **L** becomes enough to *start* and still leave CDC a block.

### 4.5 Suggested table (historical)

```
| Checkpoint              | DIRAM remain | 8bit free | internal free | DMA largest | 8bit min |
| BLE-off, UAC up         |              |           |               |             |          |
| After NimBLE init       |              |           |               |             |          |
| Connected, 15 min QMX   |              |           |               |             |          |
```

§4.1b is the filled-in field answer (PERF **L** only).

---

## 5. Proposed Firmware Architecture

### 5.1 Split: ADV + NanoC6

| Piece | Role |
|---|---|
| **ADV** | Mini-FT8, QMX USB host, decode, CAT, UI. **No NimBLE** for companion. UART TX of decode/log bytes from a low-priority task (never the DSP task). |
| **NanoC6** | NimBLE: iPhone CTS client and/or companion GATT server (§5.3). USB-C CDC for desk flash and optional ADV-log bridge. |

**First brick:** [M5Stack NanoC6](https://docs.m5stack.com/en/core/M5NanoC6) (SKU C125). ESP32-C6FH4, 4 MB flash, Grove, USB-C CDC, ~24×12×9.5 mm. BLE-only IDF (no Wi-Fi/Thread/Matter on that image) is expected to fit; C6 BLE controller is not in ROM so the binary is fatter than the same sketch on S3. Dual-OTA + Wi-Fi on 4 MB is a later measurement, not a promise.

**Dev:** ESP-IDF 5.5.x, `idf.py set-target esp32c6`, **separate project** (not this tree’s ADV target). Hold **GPIO9**, then plug USB-C. Arduino + M5Unified is acceptable for bring-up.

**UART is enough:** compact `CALL,GRID,SNR,DT,FREQ` lines are kilobits per 15 s slot. 115200 baud is plenty. Do not `uart_write` from the decode task. Software credits if a fat `.adi` dump can overrun the Nano RX buffer (four wires, no RTS/CTS unless more EXT pins are stolen).

**I4 / `core_api`:** still a facade. A real UART consumer is part of sequencing I3, not a hand-wave in `main.cpp`.

### 5.2 Physical (ADV + Nano)

Power: EXT **pin 6 `5VOUT`**, **pin 4 GND**. ADV power switch **ON**. Do not use pin 2 `5VIN`. Desk log-bridge: power the Nano from **its** USB-C only (do not fight `5VOUT`).

Data (cross TX/RX):

| ADV EXT 2.54-14P | Nano Grove |
|---|---|
| Pin 12 **G13** UART TX | RX (white **G1** or yellow **G2** — assign in firmware) |
| Pin 14 **G15** UART RX | TX |
| Pin 4 GND | Black GND |
| Pin 6 5VOUT | Red 5V (field only) |

**Conflicts:** G13/G15 are `GNSS_LoRa` UART2. PORTA Grove **G1/G2** is KH1 / Grove GPS — do not steal it if those are in use. Firmware console on **G4/G5** can be a USB–TTL (or the Nano as a byte pump) for live ADV logs while QMX owns USB-C; that is a **second** UART. One Grove on the Nano is one UART: companion **or** log bridge unless multiplexed.

**Enclosure:** no official ADV+Nano dock. Remix an ADV backpack STL; 24×12 bay; keep USB-C free for QMX; do not bury the Nano ceramic antenna against the battery slab.

**Not the first brick:** AtomS3 Lite (larger, stronger antenna, 8 MB — fallback if Nano range is sad). CoreS3 + proto + ATOM (slick 54 mm cube; new board + OTG proof). Module Gateway H2 (Thread RCP, not a ready BLE companion). IR (ADV and Nano are TX-only). **Rejected:** any Morserino-32 / M32 Pocket as the UART BLE box.

### 5.3 GATT Specification (on the Nano)

One minimalist service with notify and pull semantics. No remote execution. The ADV never exposes these characteristics.

| Characteristic | Direction | Payload & Protocol Design |
|---|---|---|
| `INFO` | `read` | Protocol version byte (e.g., `0x01`). Allows app schema validation. |
| `DECODE` | `notify` | Compact decode string: `CALL,GRID,SNR,DT,FREQ`. Fits within standard 20-byte ATT MTU without requiring negotiated MTU expansion. |
| `LOG_META` | `read` | Current day’s `.adi` filename, byte count, and block count. |
| `LOG_DATA` | `indicate` / `notify` | Chunked log data sent in response to explicit block requests. |
| `CTRL` | `write` | `REQ_BLOCK <n>` (fetch log chunk), `ABORT_LOG`, `CLEAR_SUBSCRIPTION`. |

> **Decoupling:** Decodes go ADV decode path → non-blocking queue → UART task → Nano → BLE notify. If the iPhone stutters, the Nano absorbs it. **The DSP/decode task never blocks on UART or BLE.**

> **Log sync:** Stateless `REQ_BLOCK <n>`. Phone re-requests after a drop. ADV FATFS stays on a worker; yield when TX or USB Drive owns storage.

CTS time on the Nano (read iPhone `0x2A2B`, print epoch on UART, ADV `settimeofday`) can share this brick. That does **not** replace B15 one-shot NimBLE on the ADV unless B15 is dropped.

### 5.4 Operator UX on the Cardputer

* Companion / Nano link **OFF / ON** (menu or BT screen). Default off.
* ADV advertising name is irrelevant; the **Nano** advertises (name TBD, e.g. `Mini-FT8-<call>`).
* Status: Nano present / phone subscribed. Informational only.
* Enabling the UART link must never reset USB host.

---

## 6. Companion App Scope (iOS First)

The phone application provides **value-add helpers**, not radio controls. It connects to the **Nano**, not to the ADV.

1. **Live Decode Snoop:** Real-time stream of calls and grids decoded by Mini-FT8.
2. **Callsign Helpers:** Prefix-to-region lookup; single tap to open QRZ.com.
3. **Grid Helpers:** 4/6-character grid calculations (lat/lon, bearing, distance from operator).
4. **Mapping:** Visual map rendering of heard grids and worked stations via native MapKit.
5. **PSK Reporter:** Direct spot uploads using the phone’s LTE data connection.
6. **One-Button `.adi` Sync:** Pulls active logs over BLE for direct export to QRZ, LoTW, or Club Log via iOS Files or Share Sheet.

*Android support will share the identical GATT specification once iOS CoreBluetooth integration is validated in field testing.*

---

## 7. Phased Implementation Strategy

I3 stays **Ideas** until sequenced. Do not start Nano/ADV UART firmware until the roadmap says so.

| Phase | Firmware | App / RFC | Exit |
|---|---|---|---|
| **0 — RFC** | None | This file, including §4.1b | Sign-off: on-chip BLE is out; Nano UART is the plan |
| **1a — Same-chip NimBLE** | B15 CTS probe | — | **Closed, failed** (§4.1b) |
| **1 — UART + Nano BLE** | Separate `esp32c6` project: NimBLE + Grove UART. ADV: queue + UART TX, not `main.cpp` policy | iOS: scan Nano, pair, decode feed | QMX CAT/`TA` unchanged with Nano powered; decode lines on the phone; ADV **DM L** stays in the QMX-only ballpark |
| **2 — Log Sync** | `LOG_META` + blocks over UART then BLE | iOS: `.adi` pull | No slot stall; FATFS worker |
| **3 — Helpers** | Unchanged | QRZ, grid, MapKit, PSK Reporter | Field tool |
| **4 — Android / other bricks** | Same GATT on Nano | Android | Optional AtomS3 Lite if Nano RF is weak |

*KB2SLO owns this. Public GitHub. Nano firmware is not an ADV `idf.py` target.*

---

## 8. Risk Analysis & Risk Bounding

| Risk | Mitigation |
|---|---|
| **NimBLE vs CDC-ACM on ADV** | Do not run NimBLE on the ADV with QMX up. Nano owns BLE. |
| **Treating 94 KB DIRAM remain as BLE headroom** | §4.2 / §4.1b |
| **UART in the decode task** | Queue; low-priority drain |
| **Nano 4 MB + Wi-Fi/OTA** | BLE-only image first; measure before dual-OTA |
| **Nano ceramic antenna** | Face out of the sled; AtomS3 Lite fallback |
| **G13/G15 vs LoRa GNSS** | Document; don’t enable both |
| **5VOUT vs Nano USB-C** | One 5 V source |
| **FATFS / USB Drive** | Same as before: abort log pull |
| **Scope creep ("Phone as Radio")** | Non-goals; zero CAT/QSY/TX on GATT |
| **DSP jitter** | Nano absorbs BLE; ADV does not wait |

---

## 9. Ask of the Mini-FT8 Maintainers

1. Accept §4.1b: on-chip companion BLE is **rejected** on ADV+QMX.
2. Accept §5: first hardware is **ADV + NanoC6** over EXT UART. I3 stays Ideas until sequenced.
3. Do not merge ADV NimBLE-on-while-QMX. B15 one-shot CTS is a separate product decision (ROADMAP).
4. Field later (when I3 is Now): Nano powered, QMX streaming, ADV **DM L** and CAT/`TA` match Nano-off.
