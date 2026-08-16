# RFC 0001: Optional BLE Companion (Add-on, Not a Second Radio UI)

* **Status:** Draft RFC (Phase 0 Review)
* **Author / Lead:** Jeff Kalikstein, KB2SLO
* **Target File Path:** `docs/rfcs/0001-ble-companion.md`
* **Companion Target:** iPhone first (Android later once the GATT surface is stabilized)
* **Ask:** Review and merge this RFC document to establish consensus on safety constraints, GATT design, and non-goals before Phase 1 firmware code PRs are submitted.

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

---

## 4. Proposed Firmware Architecture

### 4.1 Build Safety & Zero Impact
* **Zero-Invasive Guard:** The entire feature is wrapped in `#ifdef ENABLE_BLE`. When disabled (default for official release binaries), zero DRAM or flash overhead is introduced.
* **Compile-Time Controls:** `ENABLE_BLE` defaults to **off** until DRAM and USB stability are proven on QMX + FT8 hardware.
* **Runtime Default:** Disabled at runtime until explicitly toggled in the menu (`Companion: ON`).
* **Strict Init Sequence:** USB host + CAT CDC initializes **first**. NimBLE starts *only* after CDC allocation succeeds (or after a defined timeout when no USB radio is attached, e.g., KH1-MIC).
* **FT4 Memory Guard:** NimBLE will not start in FT4 mode unless DRAM headroom is re-measured and proven safe.
* **Linker DRAM Floor:** Restores an explicit DIRAM budget check at compile/link time to guarantee free internal RAM after linking does not drop below a QMX-safe floor.

### 4.2 GATT Specification (Minimalist Surface)

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

### 4.3 Operator UX on the Cardputer
* **MENU Option:** `Companion: OFF / ON`
* **Device Name:** `Mini-FT8-<call>`
* **Status Line Indicator:** A subtle `BLE` text/icon appears on screen when a phone is subscribed (informational only).
* **Stack Isolation:** Enabling Companion while QMX is streaming must never reset or re-initialize the USB host stack.

---

## 5. Companion App Scope (iOS First)

The phone application provides **value-add helpers**, not radio controls:

1. **Live Decode Snoop:** Real-time stream of calls and grids decoded by Mini-FT8.
2. **Callsign Helpers:** Prefix-to-region lookup; single tap to open QRZ.com.
3. **Grid Helpers:** 4/6-character grid calculations (lat/lon, bearing, distance from operator).
4. **Mapping:** Visual map rendering of heard grids and worked stations via native MapKit.
5. **PSK Reporter:** Direct spot uploads using the phone’s LTE data connection.
6. **One-Button `.adi` Sync:** Pulls active logs over BLE for direct export to QRZ, LoTW, or Club Log via iOS Files or Share Sheet.

*Android support will share the identical GATT specification once iOS CoreBluetooth integration is validated in field testing.*

---

## 6. Phased Implementation Strategy

| Phase | Firmware Deliverables | App / RFC Deliverables | Exit Criteria |
|---|---|---|---|
| **0 — RFC Merge** | None (Documentation only) | `docs/rfcs/0001-ble-companion.md` PR | Maintainer sign-off on non-goals, init order, and architecture |
| **1 — Safe BLE Core** | Post-CDC NimBLE init, `#ifdef` guards, queue-decoupled `DECODE` notify | iOS: Scanner, pairing, decode feed | QMX CAT works 100%; zero decode drops vs BLE-off |
| **2 — Log Sync** | `LOG_META` + block-based `.adi` chunking | iOS: One-button `.adi` pull to Files | Log transfers do not interrupt TX/RX or stall FATFS |
| **3 — Auxiliary Helpers** | Unchanged | iOS: QRZ, grid math, MapKit, PSK Reporter | Operational field tool for POTA/QRP |
| **4 — Platform Expansion**| Android client; re-evaluate FT4 DRAM budget | Android GATT integration | Independent evaluation |

*KB2SLO owns Phases 0–3. All development will occur transparently via public GitHub PRs and forks.*

---

## 7. Risk Analysis & Risk Bounding

| Risk | Mitigation |
|---|---|
| **NimBLE vs CDC-ACM (QMX TX failure)** | CDC init enforced *before* NimBLE; runtime default is `OFF`; DIRAM budget checks enforced at link time. |
| **FT4 RAM Starvation** | Feature disabled in compile/runtime configuration for FT4 until measured and validated. |
| **DSP / Decode Task Jitter** | Non-blocking FreeRTOS ISR queue decoupling. Queue overflows drop notifications silently, preserving real-time execution. |
| **FATFS / Storage Conflicts** | Log transfers automatically abort/block if storage bus is claimed by USB host mode (`C`). |
| **Interrupted ADIF Transfer** | Stateless block-request design (`REQ_BLOCK n`) allows seamless resumption if interrupted by TX or UI menu interaction. |
| **Battery / Thermal Overhead** | Advertising stops upon connection; RF Tx power restricted to low range (+0 dBm) given short operating distance. |
| **Scope Creep ("Phone as Radio")** | Strictly bounded by non-goals; zero control characteristics exposed for CAT, QSY, or TX triggering. |

---

## 8. Ask of the Mini-FT8 Maintainers

1. **Review and Merge Phase 0 (This RFC):** Accept this document into `docs/rfcs/0001-ble-companion.md` to establish consensus on the scope, safety bounds, and development roadmap.
2. **Inline Feedback:** Use GitHub PR inline comments to request adjustments to GATT payloads, FreeRTOS queue depth, or initialization steps before firmware code is written.
3. **Accept Phase 1 Firmware PR:** Allow Phase 1 as an `#ifdef ENABLE_BLE` gated PR with runtime setting defaulting to `OFF`.
4. **Field Validation:** Maintain default setting as `OFF` until KB2SLO and community testers verify zero regression on QMX CAT functionality and decode performance.
