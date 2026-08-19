# RFC 0001: Optional BLE Companion (Add-on, Not a Second Radio UI)

* **Status:** Draft RFC (Phase 0 Review)
* **Author / Lead:** Jeff Kalikstein, KB2SLO
* **Target File Path:** `docs/rfcs/0001-ble-companion.md`
* **Companion Target:** iPhone first (Android later once the iOS GATT surface is stabilized)
* **Ask:** Review and merge this RFC document to establish consensus on safety constraints, GATT design, RAM gates, and non-goals before Phase 1 firmware code PRs are submitted.

---

## 1. Context & Motivation

The Cardputer ADV is the right place to decode, autoseq, CAT, and TX. A phone is the right place for maps, QRZ lookups, grid helpers, and PSK Reporter spots. Those auxiliary tasks compete heavily with the 240×135 screen and the ESP32-S3 internal DRAM budget if attempted on-device.

While USB Drive mode (`C`) moves `.adi` log files to a computer, it is awkward on iOS via Apple Files. Operators in the field frequently have cellular connectivity on their phone but no laptop. A low-overhead BLE link that operates without a Wi-Fi Access Point allows the phone to pull logs and monitor live decodes while Mini-FT8 continues running uninterrupted.

KB2SLO will lead the firmware GATT implementation and the iOS application in the open against this specification.

---

## 2. Non-Goals (Explicit Bounding)

This project is **not** a revival of the V2.0.x BLE Terminal / native-client stack.

We will **not**:
* Mirror or replace `R` / `T` / `S` screens or menus on the phone
* Stream the waterfall over BLE
* Expose JSON RPC endpoints for core commands
* Require Wi-Fi, a hotspot, or AP mode on either device
* Make BLE mandatory at boot
* Block QMX USB-host CAT (the precise failure mode that forced the previous BLE removal)
* Turn on SPIRAM / PSRAM to “make room” for NimBLE. USB CDC and UAC need **internal DMA-capable** RAM. External RAM does not fix the V2.0.4 failure mode.
* Ship `ENABLE_BLE` on in official `dev` / tagged binaries until §4 gates pass on QMX + FT8 hardware.

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

Flash is not the constraint. Internal DIRAM is. PSRAM is off (`CONFIG_SPIRAM` unset) on purpose for this product.

### 4.1 Snapshot (BLE off, 2026-08-19)

`idf.py size` on the then-current `main` app map (`ENABLE_BLE` not in the tree):

| Region | Used | Remain | Total |
|---|---:|---:|---:|
| **DIRAM** | 247,547 (**72%**) | **94,213** | 341,760 |
| IRAM (16 KB bank) | 16,384 (100%) | 0 | 16,384 |

DIRAM mix: BSS ~160 KB, IRAM-resident `.text` ~70 KB, `.data` ~17 KB.

That **94 KB remain is heap at link time**. Runtime takes stacks and buffers from it (`app_core0` 12 KB, USB/UAC tasks, waterfall blit ~8.6 KB, audio pipeline, copy-to-SD worker 12 KB while a copy runs, and any future Station-save worker). A live QMX session is therefore well below 94 KB free. PERF (`8B` / `IN` / `DM`) and `log_mem_caps` are the live view; `DECODE_HEAP` (roadmap B8) is a noisy decode-time probe, not a CI substitute.

Refresh this snapshot in the Phase 1 PR if the radio/USB/decode stack has moved. Stale remain figures are worse than none.

### 4.2 What “fits” means

| Claim | Verdict |
|---|---|
| Spectator GATT, compile-time off, runtime off, USB/CDC **before** NimBLE | Architecturally compatible with this budget |
| Old ~50 KB continuous NimBLE (second UI) on a live QMX session | Does **not** fit. Would cut link remain to ~44 KB before radio tasks and DMA buffers |
| Spectator NimBLE in the **20–30 KB** static+heap range | Plausible; **must be measured**, not assumed |
| Companion ON in the default release binary | Not until §4.4 gates pass |

The number that killed QMX TX was not DIRAM remain. It was **no contiguous DMA block left for CDC**. Link-time DIRAM remain is necessary and insufficient.

### 4.3 Metrics (record all four, every BLE firmware PR)

| Metric | How | What it tells |
|---|---|---|
| DIRAM used / remain | `python $IDF_PATH/tools/idf_size.py build/mini_ft8.map` | Static cost of `#ifdef ENABLE_BLE` |
| 8-bit / internal free | `heap_caps_get_free_size(MALLOC_CAP_8BIT)` / `MALLOC_CAP_INTERNAL` (PERF + `log_mem_caps`) | Runtime heap after tasks |
| **Largest DMA block** | `heap_caps_get_largest_free_block(MALLOC_CAP_DMA)` | The QMX-safe figure. CDC/UAC need a contiguous internal block, not just free-byte totals |
| 8-bit minimum ever | `heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)` after a real session | Fragmentation and decode peaks |

**Field capture (cannot fake in CI):** QMX+ on USB-C, UAC streaming, FT8, Companion in the state under test. Same radio, same firmware family. Note FT8 vs FT4.

Do not treat “heap free still looks like tens of KB” as a pass if **largest DMA** collapsed.

### 4.4 Development loop (Phase 1 is this loop, not a NimBLE dump)

Each step is a PR (or a clearly labeled commit series) with a **before/after table** in the PR body. If the table is missing, the PR is not reviewable.

1. **Baseline (BLE still off).** `idf.py size` DIRAM row. On ADV + QMX: UAC up, then `log_mem_caps` (or PERF) **after streaming starts**, after one decode slot, after one TX. This sets the QMX-safe DMA-block floor. Write the numbers into this §4.1 snapshot or the PR; do not keep them only in chat.
2. **Zero-cost guard.** `#ifdef ENABLE_BLE` plumbing that does **not** compile NimBLE in the default `sdkconfig`. Confirm DIRAM remain matches baseline (delta ~0).
3. **Static cost cap.** A build with `ENABLE_BLE=y` and runtime still **OFF**. DIRAM remain vs the same SHA with `ENABLE_BLE` unset. **Stop** if the static DIRAM delta is still in the old ~50 KB second-UI class. Spectator target: well under that; treat **>30 KB static DIRAM delta** as a redesign trigger (trim NimBLE config, drop features, do not “just ship it”).
4. **Init-order probe.** Runtime ON only **after** USB host + CDC allocation succeeded. Capture largest DMA **immediately before** NimBLE init, immediately after, then after a BLE connect. CAT/`TA` must still work. Any DMA-block collapse vs pre-init is a fail, even if CAT happened to work once.
5. **Session soak.** Companion ON, phone subscribed, ~15 minutes of QMX FT8 (decode + TX). Compare min-heap and largest DMA to the BLE-off baseline from step 1. Decode count / slot timing must not regress. FATFS workers (copy-to-SD, later Station save) still allocate from this heap — do not soak only on an idle RX screen.
6. **Linker floor.** Fail the `ENABLE_BLE=y` link if DIRAM remain drops more than the cap from step 3 vs a BLE-off map of the same tree. This catches static regressions in CI. It does **not** replace the field DMA measurement.
7. **Release gate.** Official bins stay `ENABLE_BLE` **off** until steps 1–6 pass on QMX + FT8. Companion ON in a field `dev` build is opt-in, not default. FT4 stays BLE-off until this loop is repeated for FT4 (larger stream stack, heavier LDPC).

If measurement says the spectator stack still costs ~50 KB continuous, **stop**. Do not enable PSRAM. Revisit transport (I3: BLE vs USB vs Wi‑Fi) instead of weakening the radio.

### 4.5 Suggested PR table

```
| Checkpoint              | DIRAM remain | 8bit free | internal free | DMA largest | 8bit min |
| BLE-off, UAC up         |              |           |               |             |          |
| ENABLE_BLE=y, runtime OFF |            |           |               |             |          |
| After NimBLE init       |              |           |               |             |          |
| Connected, 15 min QMX   |              |           |               |             |          |
```

Include the git SHA, `ENABLE_FT4`, and whether QMX was streaming.

---

## 5. Proposed Firmware Architecture

### 5.1 Build Safety & Zero Impact
* **Zero-Invasive Guard:** The entire feature is wrapped in `#ifdef ENABLE_BLE`. When disabled (default for official release binaries), zero DRAM or flash overhead is introduced.
* **Compile-Time Controls:** `ENABLE_BLE` defaults to **off** until §4 gates pass on QMX + FT8 hardware.
* **Runtime Default:** Disabled at runtime until explicitly toggled in the menu (`Companion: ON`).
* **Strict Init Sequence:** USB host + CAT CDC initializes **first**. NimBLE starts *only* after CDC allocation succeeds (or after a defined timeout when no USB radio is attached, e.g., KH1-MIC).
* **FT4 Memory Guard:** NimBLE will not start in FT4 mode unless §4 is re-run on FT4 and proven safe.
* **Linker DRAM Floor:** `ENABLE_BLE=y` builds fail the link if DIRAM remain regresses beyond the cap in §4.4 step 6.

### 5.2 GATT Specification (Minimalist Surface)

One minimalist service with notify and pull semantics. No remote execution capabilities:

| Characteristic | Direction | Payload & Protocol Design |
|---|---|---|
| `INFO` | `read` | Protocol version byte (e.g., `0x01`). Allows app schema validation. |
| `DECODE` | `notify` | Compact decode string: `CALL,GRID,SNR,DT,FREQ`. Fits within standard 20-byte ATT MTU without requiring negotiated MTU expansion. |
| `LOG_META` | `read` | Current day’s `.adi` filename, byte count, and block count. |
| `LOG_DATA` | `indicate` / `notify` | Chunked log data sent in response to explicit block requests. |
| `CTRL` | `write` | `REQ_BLOCK <n>` (fetch log chunk), `ABORT_LOG`, `CLEAR_SUBSCRIPTION`. |

> **Decoupling Guarantee (Zero Jitter):**  
> Decodes are pushed to a non-blocking FreeRTOS ring queue (`xQueueSendFromISR`, depth 10) directly from the DSP task. A low-priority NimBLE background task pops from this queue to transmit notifications. If the BLE queue fills or stutters, decodes drop silently. **The DSP/decode task never blocks or waits on BLE.**

> **Resilient Log Sync State Machine:**  
> Log transfers use a stateless offset request model (`REQ_BLOCK <n>`). If an active transfer is interrupted by a TX cycle, menu change, or temporary link drop, the phone simply re-requests missing blocks when idle. Log transfers run on a low-priority thread and yield immediately whenever firmware owns FATFS.

---

### 5.3 Operator UX on the Cardputer
* **MENU Option:** `Companion: OFF / ON`
* **Device Name:** `Mini-FT8-<call>`
* **Status Line Indicator:** A subtle `BLE` text/icon appears on screen when a phone is subscribed (informational only).
* **Stack Isolation:** Enabling Companion while QMX is streaming must never reset or re-initialize the USB host stack.

---

## 6. Companion App Scope (iOS First)

The phone application provides **value-add helpers**, not radio controls:

1. **Live Decode Snoop:** Real-time stream of calls and grids decoded by Mini-FT8.
2. **Callsign Helpers:** Prefix-to-region lookup; single tap to open QRZ.com.
3. **Grid Helpers:** 4/6-character grid calculations (lat/lon, bearing, distance from operator).
4. **Mapping:** Visual map rendering of heard grids and worked stations via native MapKit.
5. **PSK Reporter:** Direct spot uploads using the phone’s LTE data connection.
6. **One-Button `.adi` Sync:** Pulls active logs over BLE for direct export to QRZ, LoTW, or Club Log via iOS Files or Share Sheet.

*Android support will share the identical GATT specification once iOS CoreBluetooth integration is validated in field testing.*

---

## 7. Phased Implementation Strategy

| Phase | Firmware Deliverables | App / RFC Deliverables | Exit Criteria |
|---|---|---|---|
| **0 — RFC Merge** | None (Documentation only) | This file | Sign-off on non-goals, init order, GATT bounds, and **§4 RAM loop** |
| **1 — Safe BLE Core** | Post-CDC NimBLE init, `#ifdef` guards, linker DIRAM cap, queue-decoupled `DECODE` notify | iOS: Scanner, pairing, decode feed | §4.4 steps 1–6 table in the PR; QMX CAT works; no decode-drop regression vs BLE-off; largest DMA holds vs pre-NimBLE |
| **2 — Log Sync** | `LOG_META` + block-based `.adi` chunking | iOS: One-button `.adi` pull to Files | Log transfers do not interrupt TX/RX or stall FATFS; repeat §4.3 capture during an active pull |
| **3 — Auxiliary Helpers** | Unchanged | iOS: QRZ, grid math, MapKit, PSK Reporter | Operational field tool for POTA/QRP |
| **4 — Platform Expansion** | Android client; re-run §4 on FT4 | Android GATT integration | Independent evaluation; FT4 BLE still off unless the FT4 table passes |

*KB2SLO owns Phases 0–3. All development will occur transparently via public GitHub PRs and forks.*

Phase 1 is not “land NimBLE then see.” It is the measurement loop in §4.4. A Phase 1 PR without the RAM table is incomplete.

---

## 8. Risk Analysis & Risk Bounding

| Risk | Mitigation |
|---|---|
| **NimBLE vs CDC-ACM (QMX TX failure)** | CDC init **before** NimBLE; runtime default `OFF`; official bins `ENABLE_BLE` off; §4 DMA-block capture; linker DIRAM cap |
| **Treating 94 KB DIRAM remain as BLE headroom** | §4.2: that remain **is** the QMX heap. Budget NimBLE against DMA largest-block, not against link remain alone |
| **Static NimBLE still ~50 KB** | §4.4 step 3 stop-ship at >30 KB DIRAM delta; redesign or drop; do not enable PSRAM |
| **FT4 RAM starvation** | BLE disabled for FT4 until §4 is repeated on FT4 |
| **DSP / Decode task jitter** | Non-blocking FreeRTOS ISR queue decoupling. Queue overflows drop notifications silently, preserving real-time execution |
| **FATFS / Storage conflicts** | Log transfers abort/block if storage is claimed by USB Drive (`C`) or a copy/save worker |
| **Interrupted ADIF transfer** | Stateless `REQ_BLOCK n` resume |
| **Heap competition (copy-to-SD / Station save)** | Soak with those paths in mind; they take stacks from the same 94 KB-derived heap |
| **Battery / thermal overhead** | Advertising stops on connection; RF TX power low (+0 dBm) |
| **Scope creep ("Phone as Radio")** | Non-goals; zero CAT/QSY/TX control characteristics |

---

## 9. Ask of the Mini-FT8 Maintainers

1. **Review and Merge Phase 0 (This RFC):** Accept this document to establish consensus on scope, safety bounds, RAM gates, and development order.
2. **Inline Feedback:** Use GitHub PR comments for GATT payloads, queue depth, init order, or the §4 caps **before** firmware code is written.
3. **Phase 1 only with a RAM table:** An `#ifdef ENABLE_BLE` PR is acceptable to *start* the loop. It is not acceptable to merge NimBLE-on behavior without §4.4 numbers. Runtime stays `OFF`; official binaries stay `ENABLE_BLE` off until the gates pass.
4. **Field Validation:** ADV + QMX+, UAC streaming. CAT/`TA` and decode performance vs BLE-off. Largest DMA block is a first-class pass/fail, not a footnote.
