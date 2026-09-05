# RFC 0001: Companion Sidekick (Add-on, Not a Second Radio UI)

* **Status:** Draft RFC. **Major pivot 2026-09-05 — read this before anything below.** Same-chip NimBLE on ADV+QMX failed the §4 DMA gate (2026-08-31, unchanged). Two things changed since: (1) **headless-Atom-as-main is closed, hardware-confirmed** — an AtomS3 Lite bench-tested as a QMX USB host (§4.6) failed to enumerate the radio even with independent Grove 5V power; Phase 1 is **not** headless, the ADV remains the main FT8 processor exactly as it is today. (2) **The sidekick chip pivots from NanoC6 to AtomS3 Lite** (§5.1) — same ADV-hosts-and-flashes mechanism, different target, chosen for the dual-core headroom a WiFi+HTTP companion needs. **Phone path also pivots**: WiFi + a plain browser is now the primary transport (§5.0) — no app on either iPhone or Android — with BLE parked, not deleted, as a possible later addition once a native app phase exists. Internet-routability is explicitly **out of scope**; if reached from off the local network at all, that is the operator's own VPN/tunnel setup, not this project's problem. §5.2 onward below (field-flash mechanics, PORTA arbitration, version identity) was built and proven **for NanoC6** — it is the reference design for retargeting to AtomS3 Lite, not a description of what's built today for that chip. §5.3/§6 (GATT, companion app) are **parked**, kept for a possible future BLE/native-app phase, not the current plan.
* **Author / Lead:** Jeff Kalikstein, KB2SLO
* **Target File Path:** `docs/rfcs/0001-ble-companion.md`
* **Companion Target:** iPhone and Android, day one, via a plain browser — no native app required for the MVP.
* **Radio:** Cardputer ADV + QMX (USB host, unchanged). Sidekick: **AtomS3 Lite** on ADV UART (PORTA), replacing NanoC6. Distinct from one-shot CTS on the ADV ([B15](../ROADMAP.md)).
* **Ask:** Keep this document as the companion plan. Do not land NimBLE-on-ADV for a live QMX session. Do not attempt headless-Atom-as-main again without first sourcing a board that's a *proven* USB host (§4.6) — not another untested Atom SKU. Do not build BLE/GATT (§5.3/§6) as the primary transport; WiFi+browser is primary until a native-app phase is explicitly opened.

---

## 1. Context & Motivation

The Cardputer ADV is the right place to decode, autoseq, CAT, and TX. A phone is the right place for maps, QRZ lookups, grid helpers, and PSK Reporter spots. Those auxiliary tasks compete heavily with the 240×135 screen and the ESP32-S3 internal DRAM budget if attempted on-device.

USB Drive mode (`C`) is gone (ROADMAP B23 — it never actually enumerated on a host, see RFC history). Copy Files to SD remains, but B23 removed the last non-companion way to get logs off the ADV over a cable. That raises the stakes on the sidekick actually working, not lowers them. Operators in the field frequently have cellular connectivity on their phone but no laptop. A low-overhead link from a coprocessor (not the ADV) lets the phone pull logs and monitor live decodes while Mini-FT8 continues uninterrupted. **How the phone talks to the sidekick is WiFi + a plain browser** (§5.0) — settled 2026-09-05, not open. iOS Safari has no Web Bluetooth support at all (a long-standing, Apple-wide platform limitation), so a genuine "no app, works on iPhone or Android" companion cannot use BLE as its transport regardless of any other tradeoff; that fact alone decides §5.0, it isn't a preference.

**How the sidekick attaches:** the ADV does **not** run NimBLE while the QMX is up, and does not run WiFi/lwIP on the ADV either (unchanged non-goal, §2). A second MCU (AtomS3 Lite, replacing NanoC6 — §5.1) owns the phone link. The ADV talks TTL UART to it over PORTA, exactly as designed for the Nano. The phone UI is a browser page the sidekick itself serves; no native app, no App Store review, no separate iOS/Android codebases for the MVP.

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
    * **Scope clarification (2026-09-04):** this non-goal is about **DMA buffers**, and for those it is correct — external RAM cannot back USB CDC/UAC descriptors or the contiguous DMA block §4.1b measures. It has since been read more broadly than it should be. It does **not** say that no data may live in external RAM. The ft8_lib waterfall in particular (`waterfall_static_buf`, 80,538 B — the single largest DIRAM object in the build) is plain CPU-swept data, never a DMA target, and would be a legitimate PSRAM candidate on hardware that had PSRAM, subject to measuring candidate-search sweep cost against the ~13 s slot budget.
    * **Moot on this board, and that is the actual blocker:** the Stamp-S3A is an **ESP32-S3FN8** (`F` = embedded flash, `N8` = 8 MB; an `R2`/`R8` suffix would indicate embedded PSRAM). There is no PSRAM die to move anything into. `CONFIG_SOC_SPIRAM_SUPPORTED=y` in `sdkconfig` is chip-*family* capability, not a statement about this board, and `CONFIG_SPIRAM` is unset.
    * **Confirmed on hardware 2026-09-04** (`esptool.py -p <port> flash_id`), so this no longer rests on a part-number inference — and checked on *both* S3 boards, because if either had PSRAM it would have reordered I24/I26:

        | Board | `Features:` | Flash vendor | MAC |
        |---|---|---|---|
        | Cardputer ADV (Stamp-S3A) | `WiFi, BLE, Embedded Flash 8MB` | XMC (0x20) | `28:84:85:76:83:0c` |
        | AtomS3 Lite | `WiFi, BLE, Embedded Flash 8MB` | GigaDevice (0xc8) | `50:78:7d:cd:04:cc` |

        Neither reports `Embedded PSRAM`. Both are ESP32-S3 QFN56 rev v0.2, 8 MB flash, `USB mode: USB-Serial/JTAG`. So “move the waterfall to slower RAM” fails for want of hardware on *every board this project owns*, not for want of a sound idea.
    * **Aside, since these boards are indistinguishable over USB:** `flash_id` reports the chip, and both are the same part; in USB-Serial/JTAG mode both enumerate as Espressif's generic `303A:1001` with no board string. The **MAC** is the only reliable discriminator (recorded above). The differing flash vendor is a production-batch artifact, not a model identifier — do not key anything off it.
* Ship on-chip `ENABLE_BLE` in official `dev` / tagged binaries
* Treat two ATOMs/Nanos (no USB-host brain) as a QMX radio
* Use a Morserino-32 or M32 Pocket as the BLE coprocessor (classic 4-pin header or Pocket USB). Leave those boxes as CW. I17 (Pocket as a Mini-FT8 *host*) is Ideas, not this path.
* Use IR or the headphone jack as the companion pipe

The Cardputer remains the single source of truth and operation. The phone acts strictly as a **spectator and librarian**.

**Reaffirmed, not merely carried over, 2026-09-05:** a chat-only detour explored a headless Atom *replacing* the ADV as the main processor, which would have made this non-goal incoherent (there'd be no Cardputer left to be "the" operator surface, so the phone/browser would have had to become one — a genuine, un-shipped scope change, not a wording problem). §4.6 closes that detour on hardware grounds. With the ADV confirmed as remaining the main processor, this non-goal is back in force exactly as written, unconditionally, no asterisk. If headless-as-main is ever revisited on different, proven hardware, this non-goal is the first thing that would need a real, deliberate rewrite — not an assumption to carry over quietly.

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

### 4.1c The §4.1b numbers are stale — always-on host costs ~6K (2026-09-04)

**§4.1b was measured 2026-08-31. B18's always-on USB host (`80b9ebe`) landed 2026-09-02.** Its 44K "boot floor" therefore describes firmware that did *not* hold the USB host from boot, and no longer describes this tree. Anyone planning CTS headroom against 44K is planning against a number that no longer exists.

Re-measured on hardware 2026-09-04, same PERF **DM** **L** method, boot with nothing pressed: **38K**. So the always-on host costs roughly **6K of the largest contiguous DMA block**, held continuously from power-on.

Three things follow, and the third is the one that matters:

- **CTS still starts.** `H → 1` was re-confirmed working at 38K the same day. B18 did not break CTS; it ate part of the margin.
- **The cost lands only when the radio is idle.** `S → 2` installs the host anyway, so B18 changes *when*, not *whether*. Its 6K is spent precisely in the state CTS needs.
- **6K does not scale the wall.** §4.1b's finding is that each stack wants a ~40K-class *contiguous* hole to start, and `S → 2` then `H → 1` failed at ~29K. Recovering 6K does not turn 29K into 40K. **Going on-demand will not unlock B17**, and should not be justified on memory grounds. Its real justifications are correctness (see §5.5) and matching the intended design.

Attribution caveat: four days and several commits (`nano_flasher`, PORTA work) separate the two measurements, so B18 is the prime suspect, not a proven cause. The clean A/B is one build with the boot-time `uac_host_ensure_started()` removed — boot, read **L**. 44K confirms it.

### 4.2 What “fits” means

| Claim | Verdict |
|---|---|
| NimBLE on the ADV while QMX UAC/CDC is up | **Does not fit.** §4.1b |
| Old ~50 KB continuous NimBLE (second UI) on a live QMX session | Does **not** fit |
| Spectator NimBLE 20–30 KB on the same S3 as QMX | **Rejected** after measurement |
| Wi-Fi + lwIP on the ADV to replace BLE | Worse internal RAM, not better |
| USB hub on ADV (`CONFIG_USB_HOST_HUBS_SUPPORTED` is off) | More host DMA; not enabled |
| Another ESP32-S3 / PSRAM board | Same ~512 KB **internal** SRAM; USB still needs DIRAM |
| CoreS3 OTG + proto + ATOM | Same UART architecture; "unproven QMX host" flagged here **confirmed false for AtomS3 Lite**, 2026-09-05 — see §4.6. Not revisited without a different, proven-host board |
| UART to a second MCU; that MCU runs WiFi + browser-served HTTP (was: NimBLE) | **The path**, pivoted 2026-09-05. Sidekick: AtomS3 Lite (was NanoC6). ADV heap stays the QMX heap either way — the second MCU's radio choice doesn't touch it |
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

### 4.6 Headless-Atom-as-main: closed on hardware, 2026-09-05

A chat-only exploration (never landed in this file, so no revert needed elsewhere) asked whether an AtomS3-family board could *replace* the ADV as the main FT8 processor, with the Nano/sidekick unchanged — "headless": no screen, no keyboard, an ordinary smartphone browser as the only operator surface. The single hard requirement for that to work at all is the main board acting as USB **host** to a QMX/QDX, exactly as the ADV already does.

**Before any bench test**, the specific candidate — AtomS3 Lite — was checked against its own vendor documentation (`docs.m5stack.com`, fetched directly): *"The onboard USB Type-C port enables program download and serial communication"* — a description limited entirely to device-mode uses — plus a power circuit described as *"5V to 3.3V"*, one-directional step-down with no mentioned boost/VBUS-source path. USB host mode requires sourcing VBUS out to the hosted device; nothing in that description suggests this board can. This matched a gap already on record: this RFC's own §4.2 table has carried *"CoreS3 + proto + ATOM ... new board + OTG proof"* since before this pivot — Atom-class USB-host capability was flagged as unverified, not assumed, from the start.

**Bench-tested 2026-09-05, and it failed.** A minimal POC (`test_apps/atoms3_qmx_host_poc/`, not shipped) installed the USB Host Library on an AtomS3 Lite and attempted the exact CAT sequence `main/radio_control_qmx.cpp` already uses and has proven on the ADV: `MD6; FR0; FT0; FA00007074000;` — entirely receive-side, no `TX;`, safe with no antenna. Pass condition was the QMX's **own screen** showing the new frequency (chosen specifically because this operator's bench has no way to watch UART/serial logs while the AtomS3's one USB-C port is busy being a host — the same port can't simultaneously present a debug console). The QMX's display never changed. Retested with the AtomS3 Lite additionally powered independently via its Grove 5V pin (ruling out "it just needed its own power rail" as an explanation) — same result. **Confirmed, not inferred: this specific board cannot act as a USB host for a QMX.**

**Scope of what this closes.** It is not narrowly about flashing a sidekick over USB — that would be a minor, recoverable loss (the existing desk-flash-over-its-own-USB-C fallback, already documented and proven, covers it). It is the same underlying capability, and the same shared software path (`nano_flasher`'s field-flash and `stream_uac.cpp`'s QMX CAT hosting both depend on `usb_host_cdc_acm`) — so this closes **any** use of this specific board as a USB host, which is the one thing "headless-as-main" cannot do without.

**What this does not close.** The ADV's own USB-host capability (Stamp-S3A module) is unaffected and remains fully proven — this finding is about a *different* chip carrier, not about ESP32-S3 USB-OTG host mode in general. A board built on the exact same Stamp-S3A module the ADV already uses would inherit that proven behavior; whether one is sourceable standalone (screen-less, keyboard-less) is a separate, open, ongoing thread — track it before reopening headless-as-main, and do not substitute another untested Atom SKU for it without repeating this same bench test first.

**What this pivots to instead:** Phase 1 is not headless. The ADV remains the main processor, unchanged. The sidekick chip itself still moves — to AtomS3 Lite (§5.1) — but as a *companion* to the ADV, never as its USB host replacement. AtomS3 Lite's device-mode USB-C (proven all session, via ordinary `esptool.py` flashing) is exactly what a sidekick needs; its host-mode absence is irrelevant to that role, since a sidekick never hosts anything over USB.

---

## 5. Proposed Firmware Architecture

### 5.0 Phone path — WiFi + browser is primary, decided 2026-09-05

Hardware split is locked: **ADV (QMX + Mini-FT8, unchanged) + sidekick (AtomS3 Lite) on UART**. What the phone uses is now decided for the MVP:

| Path | Status |
|---|---|
| **WiFi + plain browser (no app), sidekick-hosted** | **Primary, decided.** Decisive, not just preferred: iOS Safari has no Web Bluetooth support at all — a long-standing Apple-wide limitation, not a bug that might be fixed — so a genuine no-app companion working on *both* iPhone and Android cannot use BLE regardless of any other tradeoff. |
| Sidekick-as-AP (I19's original shape, phone joins it) | In scope, mode TBD vs STA below. No credential bootstrap needed — the sidekick's own network is fixed/known, nothing to configure. |
| Sidekick-as-STA joining phone Personal Hotspot or home WiFi | In scope, real reason to prefer it over AP-only for the field case: joining a Personal Hotspot means the *phone* is the one sharing its own cellular connection — standard, Apple-documented behavior — rather than the phone trying to be a WiFi client of the sidekick while hoping iOS keeps LTE (I19's original, undocumented DHCP-based risk). Credential bootstrap for this direction: SoftAP provisioning (below), not BLE. |
| BLE GATT + native app (§5.3 / §6) | **Parked, not deleted.** No role in the MVP given the Safari constraint above. Revisit only if a native-app phase is explicitly opened later; the GATT design in §5.3 stays as reference for that. |
| Credential bootstrap for STA modes | **SoftAP provisioning** (ESP-IDF's `wifi_provisioning`, `scheme_softap.h` — verified present in this project's IDF 5.5.1 install): sidekick temporarily becomes its own setup AP, operator's browser (phone *or* Mac, device-agnostic) joins it, submits the real target SSID/password via a plain page, sidekick reboots as STA. No app, no BLE. Reuses AP-mode capability the sidekick needs anyway for the row above — not new radio work. |
| Internet-routable (port-forward/DDNS, cloud tunnel, VPN mesh) | **Out of scope for this project entirely**, decided 2026-09-05, not just deferred. Local-network reachability only. If an operator wants remote/internet access, that is their own VPN/tunnel to set up — Mini-FT8 does not attempt it, and does not take on the auth/exposure risk of a licensed station's control surface being internet-reachable by default. |
| HTTP-over-BLE, Wi-Fi on the ADV | Out (unchanged). |

I19's original DHCP-no-gateway field test was never actually run — superseded by the Safari/Web-Bluetooth finding above before it became necessary, since AP-vs-STA-vs-hotspot is now a mode choice within the WiFi path, not a competing candidate against GATT.

### 5.1 Split: ADV + AtomS3 Lite (pivoted 2026-09-05, was NanoC6)

| Piece | Role |
|---|---|
| **ADV** | Mini-FT8, QMX USB host, decode, CAT, UI. **No NimBLE**, no WiFi/lwIP for companion (unchanged non-goal). UART TX of decode/log bytes from a low-priority task (never the DSP task). |
| **AtomS3 Lite** | Phone link: WiFi (AP and/or STA, §5.0) + a browser-served HTTP UI. USB-C CDC for desk flash and optional ADV-log bridge. UART from ADV over PORTA, unchanged mechanism from the Nano design. |

**Why AtomS3 Lite over NanoC6, specifically:** dual-core ESP32-S3 (240 MHz ×2) vs the C6's single core (160 MHz) is real, not marginal, headroom for running WiFi + lwIP + an HTTP server concurrently with PORTA UART parsing — a common ESP-IDF pattern pins the network stack to one core, leaving the other free. NanoC6's WiFi 6 / 802.15.4 extras buy nothing for a local HTTP server at FT8 data rates. **What did *not* change the decision:** AtomS3 Lite cannot act as a USB host (§4.6) — irrelevant here, since a sidekick never hosts anything over USB; its own USB-C only needs device mode (flashing), already proven all session via plain `esptool.py`.

**Board:** M5Stack AtomS3 Lite. ESP32-S3FN8 (per `esptool flash_id`, confirmed 2026-09-04: no embedded PSRAM), 8 MB flash, HY2.0-4P Grove (`GND/5V/G2/G1` — identical pinout to the Nano's, confirmed against both boards' schematics, so the existing PORTA cable and wiring need no change), USB-C (device mode only), 24.0×24.0×9.5 mm. 8 MB gives real headroom for a WiFi+lwIP+HTTP binary, which will be meaningfully larger than the Nano's ~100-line beacon-only firmware; the Nano's 4 MB budget concerns in the historical section below no longer apply.

**Dev:** ESP-IDF 5.5.x, `idf.py set-target esp32s3` (was `esp32c6`), same `sidekick/` project location (sibling root next to `main/`, `components/`, `test_apps/`). Strap pin for download mode is **GPIO0** on ESP32-S3 (not the C6's GPIO9) — same family as the ADV's own Stamp-S3A, so the same desk bring-up procedure the ADV itself uses applies, not the Nano-specific GPIO9 instructions in the historical section below.

**Retargeting work, tracked as its own item ([ROADMAP I3](../ROADMAP.md), amended):** `sidekick/`'s `sdkconfig`/`CMakeLists.txt` need the target change; `components/nano_flasher/nano_flasher.cpp:116`'s `if (target != ESP32C6_CHIP)` needs to also accept `ESP32S3_CHIP` (the ADV remains the proven host in that relationship — this is a small, well-scoped widening, not a redesign); the WiFi/HTTP application logic itself is new work, not a port of anything that existed for the Nano (which never had WiFi at all). §5.2 onward below is the **Nano-era reference design** for the mechanics that do carry over unchanged in shape (ADV hosts and flashes the sidekick over USB-C, then the sidekick moves to PORTA for the ongoing link) — re-verify each specific fact against the new chip rather than assuming it, the same discipline this project has applied to every other hardware claim.

**Build integration (pivot from "separate project"):** `tools/stage_nano_firmware.sh` builds the `esp32c6` `sidekick` project and stages its outputs into `components/nano_flasher/target_firmware/`; the ADV build then embeds the resulting `sidekick.bin` (plus bootloader and partition table) into the ADV app image via `EMBED_FILES`, and CI's `nano-firmware` job does the equivalent before the ADV job runs. This is a **data payload**, not code the ADV executes: the S3 never runs C6 instructions. The embedded bytes exist only so the ADV can push them out over USB-C to a Nano's ROM serial bootloader (§5.4). Two build outputs ship from one `idf.py build`: the ADV merged image (as today) and, unchanged, a standalone Nano `.bin` for desk flash via `esptool.py` when field-flash isn't used or fails. Flash budget: the embedded `.bin` must fit the ADV's `partitions.csv` remainder — measure before merging, same discipline as §4's DIRAM measurement.

**UART is enough:** compact `CALL,GRID,SNR,DT,FREQ` lines are kilobits per 15 s slot. 115200 baud is plenty. Do not `uart_write` from the decode task. Software credits if a fat `.adi` dump can overrun the Nano RX buffer (four wires, no RTS/CTS unless more EXT pins are stolen).

**I4 / `core_api`:** still a facade. A real UART consumer is part of sequencing I3, not a hand-wave in `main.cpp`.

### 5.2 Physical (ADV + Nano) — NANO-ERA REFERENCE DESIGN, not yet re-verified for AtomS3 Lite

**Everything from here through the end of §5.2c was built and proven on real hardware for NanoC6.** It's kept in full, not deleted, because the mechanism it describes (ADV hosts and field-flashes the sidekick over USB-C, sidekick then moves to PORTA for the ongoing UART link, version identity via the shared `esp_app_desc_t` scheme) is the intended shape for AtomS3 Lite too. But it has not been re-verified on that chip. Known specific facts below that are **known to change** on ESP32-S3: the strap pin is GPIO0, not GPIO9 (§5.1); the button-hold disproof (§5.2 below) is a C6-specific finding about C6's ROM behavior and should not be assumed to apply or not apply to S3 without checking; flash size is 8 MB not 4 MB. Treat every other specific claim below (VID/PID handling aside — that's ADV-side and chip-agnostic) as needing the same "verify, don't assume" pass before relying on it for AtomS3 Lite.

**Boot presence (B18):** the factory Nano speaks on **its USB-C** (Espressif CDC / USB-Serial-JTAG). ADV USB-C is USB **host** — same jack as QMX. A Grove ping cannot see a factory Nano (PORTA probe field-failed 2026-09-02: silent Grove, then idle-high false positive). See §5.5.

**Companion UART (I3, not B18):** PORTA Grove **G1/G2** (UART1). GPS puck and Nano are **either/or** on PORTA. Exclusive with [B11](../ROADMAP.md) (QMX+ AUX GPS into PORTA). LoRa GNSS stays UART2 and can coexist. This is the **runtime** link (decode feed, once BLE is up) and is distinct from the USB-C **flash** step below — a Nano is plugged into ADV USB-C to receive firmware, then moved to PORTA Grove for the ongoing companion link.

**Field flash over USB-C (pivot, §5.1):** a factory (unflashed) Nano on ADV USB-C is still recognized by B18 presence (`303A:*`). The install prompt (§5.4) drives the ADV, acting as USB host, through the Nano's ROM serial bootloader protocol (sync + flash-begin/flash-data, the same handshake `esptool.py` uses) to write the embedded `.bin`. This needs the Nano's native USB Serial/JTAG peripheral to support the same auto-reset-into-bootloader handshake `esptool.py` uses from a PC. `components/nano_flasher/` (2026-09-03) builds and links this correctly — `esp_loader_*` + `esp32_usb_cdc_acm_port` wired to a real Green Nano attach event, embedded `sidekick` bootloader/partition-table/app confirmed byte-exact in the shipped ADV image via `nm`. **Proven on real hardware (2026-09-03):** ADV field-flashed a genuine factory M5NanoC6 over its own USB-C host link; `idf.py monitor` on the Nano's own USB-C afterward showed it booting `sidekick` (not the stock `nanoc6_factorytest` firmware it shipped with). First attempt failed silently (a host-lifecycle bug — `cdc_acm_host_install()` was called after a full USB host teardown with nothing to attach to); fixed by explicitly reinstalling the host and calling `usb_c_presence_yield_device()` before handing off to the flasher, same as UAC already does for QMX. `tools/restore_nano_stock.sh` (full 4 MB flash backup/restore) re-greened the same physical part for a clean retest, which passed. Fallback (desk flash on the Nano's own USB-C) remains available and unchanged if a future unit doesn't cooperate.

**Two flash mechanisms, not two configurations of one** (2026-09-03 design decision; entry mechanism corrected 2026-09-04): a **green/factory Nano has no code that can answer a software download-mode request**, so its *only* entry point is the native-USB ROM bootloader — USB-C is mandatory for the first flash, not just preferred. **Seed flash** = ROM bootloader over USB-C CDC (`esp-serial-flasher` + the custom USB-CDC-host transport, already built and proven on hardware). Use `esp-serial-flasher` (Espressif's own chip-flashes-chip library, already in the component-registry pattern this tree uses for `espressif/usb_host_cdc_acm`) rather than hand-rolling the ROM protocol.

**Update flash over PORTA (design corrected + verified 2026-09-04, not yet built):** the original plan here — sidekick implementing "its own RTC-scratch reboot-into-UART-download-mode handler, no external strapping needed" — was wrong. Checked against Espressif's own esptool docs for this chip: ESP32-C6 has **no software-only path** into UART download mode at all. GPIO9 must be physically low at the exact moment of reset; no RTC-memory trick, watchdog-reset trick, or any other application-level mechanism substitutes for it. This is *why* the USB-C path works at all — the native USB Serial/JTAG peripheral has DTR/RTS specially wired internally to GPIO9/EN for exactly this, and PORTA's plain 4-wire Grove connector (GND/5V/G1/G2) has no equivalent.

**Button-hold self-restart, tried and disproved (2026-09-04):** the plan was: the M5NanoC6 has exactly one on-board button, wired to GPIO9 (M5Stack's own docs: "hold down the GPIO9 button and then connect the data cable" for desk bring-up). `sidekick`, already running and powered via PORTA, would read that same GPIO9 pin as a plain input and self-call `esp_restart()` on a sustained press — the operator's finger still physically holding GPIO9 low at that exact reset, landing the chip in ROM download mode as if a cable had just been plugged in fresh. Built and bench-tested on real hardware: the button read correctly (progress logged cleanly at 500ms/1000ms/.../2500ms) and `esp_restart()` fired as expected — but the reboot banner showed `rst:0xc (SW_CPU),boot:0xd (SPI_FAST_FLASH_BOOT)`. A software (`SW_CPU`) reset does **not** cause the ROM to re-sample GPIO9 at all; it boots straight back into the app regardless of the button. Corroborated, not just a fluke: Espressif's own esptool disables its (different, more aggressive) `--after watchdog-reset` trick specifically on ESP32-C6, citing full system freezes requiring a power cycle to recover — the same underlying limitation, not something a cleaner implementation would route around. Removed from `sidekick` and the ADV's BT screen (git history has the full attempt) rather than left in as a UI option that would reliably fail.

**Future phase — custom bootloader responder (preferred over wiring):** the second-stage bootloader (`bootloader.bin` — ours, not the ROM) runs on every boot including a software reset (confirmed by the disproof above: `esp_bootloader_get_description` executed right after the `SW_CPU` reset). A modified bootloader could check an RTC-memory flag the app sets before calling `esp_restart()`, and if set, skip normal boot and run its own minimal `esp_loader`-protocol responder (sync, flash-begin, flash-data, flash-end, checksums) over PORTA instead — sidesteps ROM strap-sampling entirely, since it's reached through normal code execution, not hardware pin state. `nano_flasher_flash_embedded_uart()` (`components/nano_flasher/`) needs no changes for this — it already talks generic `esp_loader` protocol and doesn't care what's listening on the other end, so it's already correct, tested-by-design infrastructure for whichever trigger mechanism ends up working. Real scope, not a quick follow-up: genuine protocol-server engineering running in the bootloader's constrained early-boot environment, and meaningfully higher-stakes than app-level code — a bug there affects every boot, not just the update path, not just the failed-update path. Own design pass before starting, not a drive-by.

**Alternative future phase, less preferred:** a dedicated 5th wire between ADV and Nano carrying GPIO9 (or GPIO9+EN, mirroring the DTR/RTS pair esptool itself uses over USB) would let the ADV trigger download mode directly via genuine hardware strap control. Simpler in concept than the bootloader responder, but means leaving a clean Grove-to-Grove cable for a custom connector/wiring — a real physical complication to the enclosure and cabling that the bootloader-responder idea avoids entirely. Worth falling back to only if the bootloader responder turns out impractical.

USB-C stays the only flash path until one of the above is actually built.

Power in the field (when UART is sequenced): PORTA 5 V / GND. ADV power switch **ON**. Desk log-bridge: power the Nano from **its** USB-C only.

**Conflicts:** PORTA G1/G2 is Grove GPS **or** companion UART — one device (KH1 CAT, the third historical tenant, dropped I21 2026-09-04). Firmware console on **G4/G5** can be a USB–TTL (or the Nano as a byte pump) for live ADV logs while QMX owns USB-C; that is a **second** UART. One Grove on the Nano is one UART: companion **or** log bridge unless multiplexed.

**Enclosure:** no official ADV+Nano dock. Remix an ADV backpack STL; 24×12 bay; keep USB-C free for QMX; do not bury the Nano ceramic antenna against the battery slab.

**Not the first brick:** AtomS3 Lite (larger, stronger antenna, 8 MB — fallback if Nano range is sad). CoreS3 + proto + ATOM (slick 54 mm cube; new board + OTG proof). Module Gateway H2 (Thread RCP, not a ready BLE companion). IR (ADV and Nano are TX-only). **Rejected:** any Morserino-32 / M32 Pocket as the UART BLE box.

### 5.2b Version identity and update detection (I3, 2026-09-04)

Both paths below — USB-C pre-flash and PORTA runtime — answer the same question ("is this Nano already running `sidekick`, and if so, which build") from the same source, not two separate mechanisms: every ESP-IDF app embeds an `esp_app_desc_t` (256 bytes, magic word `0xABCD5432`, `project_name[32]`, `version[32]`) at a fixed, linker-guaranteed offset — the literal first thing in the DROM segment (ESP-IDF's own linker script: *"Should be the first. App version info. /\* DO NOT PUT ANYTHING BEFORE THIS \*/"*). Verified empirically against a real `sidekick` build (esp32c6, ESP-IDF 5.5.1): absolute flash offset `0x10020` (app partition base `0x10000` + `0x20` — an 8-byte segment header sits before the data; easy to miscount, as a first pass here did). This offset holds for this target/IDF-version/partition-table/no-secure-boot combination — re-derive it the same way (don't assume) if any of those change.

**Versioning scheme:** `sidekick` adopts the ADV's own build-identity approach (`tools/gen_build_identity.cmake` — exact git SHA + dirty flag) instead of ESP-IDF's default `git describe`-based `PROJECT_VER`. A describe string is tag-relative and fuzzy; SHA+dirty gives an exact "is this literally the same build" comparison, which is what both checks below need.

**USB-C (pre-flash, install-prompt moment):** before offering to flash, the ADV reads `project_name`/`version` directly off the Nano's flash via `esp_loader_flash_read()` — no app cooperation needed, works even against a crashed or not-yet-booted Nano, since it goes through the ROM bootloader rather than asking the running app anything. Three outcomes:
- `project_name` doesn't read as `sidekick` → not installed. Offer to flash — same action regardless of what's actually there (stock `nanoc6_factorytest`, garbage, a different firmware entirely); re-flashing is idempotent and safe either way (§8), so a one-directional check is sufficient. This does **not** positively confirm stock M5Stack firmware, only "not recognized as ours."
- `project_name` matches and SHA+dirty matches what's embedded in this ADV build → up to date, no prompt.
- `project_name` matches, SHA+dirty differs → out of date, offer to update.

**PORTA (ongoing, once it's the daily companion link):** the handshake reply (sync byte `0xC6` — never collides with NMEA's `$`-prefixed sentences, so PORTA can still auto-detect GPS vs. companion; fixed 115200 baud, since both ends are ours and don't need GPS's auto-baud dance) carries the same `project_name`/version, sourced at runtime via `esp_app_get_description()` — no second versioning concept, same data, just read a different way once the app is actually running and reachable over Grove rather than only reachable via the ROM bootloader.

Both transports funnel into one shared version-compare function on the ADV side; only the fetch differs by transport.

### 5.2c PORTA role arbitration + companion version beacon (2026-09-04)

`main/porta.cpp` owns UART1 (G1/G2) outright and implements the auto-detect from §5.2b: probe at a baud (starting from the persisted GPS baud hint, since a companion Nano is found regardless — it only ever speaks 115200), watch each frame for either a checksum-valid NMEA sentence (`$...*HH`) or a validated companion beacon, lock to `kGps` or `kCompanion` on whichever is seen first, flip probe baud on a `kUnknown` timeout exactly like GPS's own existing auto-baud dance. No physical presence signal exists on PORTA (unlike USB-C's B18), so a locked role re-arms to `kUnknown` after ~10s of silence — the only available way to notice the operator swapped the physical device.

`gps.cpp` keeps its NMEA-parsing internals completely unchanged; it gained a "fed" mode (`gps_start_fed()` / `gps_ingest()`) so it can be driven by bytes `porta.cpp` already read, instead of self-polling `uart_read_bytes()`. The GNSS_LoRa (UART2) path is untouched — `gps_start(pins)` still owns its own UART exactly as before; arbitration only applies to PORTA/UART1, where a companion Nano is actually possible.

**Companion beacon, not a bare sync byte.** A single unvalidated `0xC6` is too weak to trust as "companion present" — indistinguishable from a stray noise byte. `sidekick/main/main.c` instead transmits a full frame roughly once a second: `0xC6` + `version[32]` (from `esp_app_get_description()`, NUL-padded) + a 1-byte XOR checksum over the preceding 33 bytes. `porta.cpp` only locks `kCompanion` once the whole frame validates; a checksum failure is discarded as noise and scanning resumes, matching how GPS detection already requires a full NMEA checksum rather than a bare `$`. A partial frame stuck mid-collection for >250ms (a bit error, not slow arrival — 34 bytes at 115200 baud is ~3ms) is abandoned rather than blocking GPS detection indefinitely.

**Version comparison, not just presence.** On a validated beacon, `porta.cpp` compares the received version against this ADV's own embedded `sidekick` build via a new `nano_flasher_embedded_version()` getter (the same version-comparison data `nano_flasher_flash_embedded()` already uses for the USB-C path, reachable now without a USB-C session) and logs a clear match/mismatch — `porta_get_companion_version()` / `porta_companion_version_matches()` expose the result. **Deliberately simplified versioning (2026-09-04 decision):** rather than track ADV and sidekick versions independently and compare two different git states, `tools/git_version.cmake` is now shared by both `tools/gen_build_identity.cmake` (ADV) and `sidekick/CMakeLists.txt` — same repo, same commit, same override-aware logic, so an exact string match is the whole test. This closed a real, latent CI bug in passing: CI checks out a synthesized merge commit for a PR, not `github.event.pull_request.head.sha`, so the ADV firmware job pins `MINIFT8_GIT_SHA` explicitly — but the sidekick build job never received that same pin before this change, so a CI-built pair could have silently disagreed on "the same commit"'s version string even though a local build never would have.

**Deliberately not built here:** no auto-update over PORTA. A version mismatch is flagged (logged; state exposed for a future UI), not acted on — reflashing stays a manual step over the existing, hardware-proven USB-C path (§5.2). Auto-updating over PORTA would need a bootloader-entry hook on sidekick, a command channel back from the ADV (this beacon is one-way, sidekick → ADV only), and `esp-serial-flasher`'s UART transport instead of the USB-CDC transport already proven — real scope, deliberately deferred rather than folded in here, especially given PORTA's 4-wire no-flow-control link is meaningfully less reliable than the direct USB-C link the current flash path already uses.

**Other known, accepted gaps:** trailing bytes in the same UART read past a role-lock point are dropped, not re-routed (self-heals on the next second's GPS sentence or companion beacon); PORTA-arbitrated GPS baud isn't persisted back to station config the way GPS's own self-owned auto-baud is (worst case, one extra probe-flip cycle at next boot); the 10s re-arm window is a first guess, not yet field-tuned; the companion role doesn't re-validate subsequent beacons while already locked (liveness alone keeps the re-arm timer honest, but a mid-session sidekick reflash without a PORTA disconnect wouldn't be noticed until the next re-arm).

### 5.3 GATT Specification — PARKED 2026-09-05, not the current plan

WiFi + browser is the primary phone path (§5.0); iOS Safari's lack of Web Bluetooth support rules BLE out for a no-app MVP on iPhone specifically, not just deprioritizes it. Kept below as reference for a possible future native-app phase, not something to build now.

Hypothesis only until I19 is resolved. One minimalist service with notify and pull semantics. No remote execution. The ADV never exposes these characteristics.

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

* **B18 (done):** 2 s toast on USB-C attach/detach (not a blocking modal). QMX/QDX, Green Nano, or raw VID/PID. Empty jack: no toast.
* **Install prompt (I3 pivot, new):** on a **factory/unflashed** Green Nano (distinguishable from an already-flashed companion Nano — exact detection TBD, e.g. a version characteristic once the Nano runs companion firmware vs silence from stock Espressif firmware), the toast becomes a prompt: "Install companion firmware? Y/N". On yes, ADV parks the QMX host path the same way it does for **C** / CTS (§5.5 step 4), flashes the embedded `.bin` over the ROM bootloader, shows progress, then reinstalls the host. On no or timeout, behaves like today's toast. Never auto-flashes without confirmation. Never flashes while QMX is the attached device (VID/PID gates this).
* Companion / Nano UART link **OFF / ON** (menu or BT screen) stays I3. Default off.
* ADV advertising name is irrelevant; the **Nano** advertises (name TBD, e.g. `Mini-FT8-<call>`).
* Status: Nano present / phone subscribed. Informational only.
* Enabling the UART link must never reset USB host. **S → 2** attaches UAC/CDC to the live host.

### 5.5 USB-C host + presence (B18)

ADV USB-C is one PHY. Hub is off. Host is the default owner of that PHY.

1. After display init, install USB **host** with the QMX ISO FIFO split (so later **S → 2** can stream). No UAC class driver and no CDC-ACM open until **S → 2**.
2. A presence client reads VID/PID on attach and notices detach. Classify (host-tested table; firmware only maps the enum):
   * **QMX/QDX** — `0x0483` / `0xA34C` (same pair `stream_uac` opens for CAT). One ID; we cannot tell QMX from QDX.
   * **Green Nano** — Espressif VID `0x303A` (USB-Serial-JTAG / TinyUSB CDC).
   * **Other** — any other gadget; toast the IDs.
   * **Empty** — no event.
3. Host **stays up**. **S → 2** installs UAC/CDC and probes devices already on the bus (no root-port power cycle — QMX firmware does not survive a dropped link).
4. **Park** (full uninstall, same bar as `uac_ensure_host_uninstalled`): USB Drive (**C**), and CTS start (`H → 1` then `1`). Reinstall host when Drive exits and when CTS ends (Idle / Success / Failed / abort). Do not auto-start UAC. B15 still unplugs the radio for CTS; B17 (cable stays in) is not this slice.
5. UI per §5.4. Do not start NimBLE.

Operator path: Nano or QMX on ADV USB-C at boot → toast → unplug/plug → toast. Radio session is still **S → 2**.

**Presence requires the USB host stack — there is no cheaper detection path (confirmed 2026-09-04).** `usb_c_presence_start()` (`main/usb_c_presence_host.cpp`) calls `usb_host_client_register()`: it is a USB Host Library *client*, receiving attach/detach through `client_event_cb` and then opening the device to read VID/PID. That requires `usb_host_install()` to have already run. There is no VBUS or D+ sense line in firmware — a grep for VBUS/boost/5V-enable across `main/` and `board_cardputer_adv` finds nothing — and the ADV *sources* VBUS in host mode rather than sensing it.

**Consequence:** boot-time presence toasts (B18) and an on-demand USB host are mutually exclusive as designed. Presence can stay and the host is held from boot (today), or the host comes up only when something needs it (**S → 2**, nano flash, USB Drive) and plug/unplug toasts are lost. There is no third option on this hardware without a schematic-level sense line.

**This is also why USB Drive (`C`) cannot enumerate (field-proven 2026-09-04).** Because the host is installed at boot and never released until `C` asks for it, the OTG core has been in host mode since power-on on every attempt. Instrumented firmware showed the failure precisely: `uac_ensure_host_uninstalled()` returns OK, `tinyusb_driver_install()` returns OK, `tud_connect()` runs, the screen shows "Waiting for computer..." — and **zero** TinyUSB gadget events ever arrive, while `system_profiler SPUSBDataType` on the host stays empty in both plug orders. The Mac never sees a device at all, so MSC, SCSI INQUIRY and descriptors are all downstream of the real fault. See [ROADMAP B23](../ROADMAP.md).

---

## 6. Companion App Scope (iOS First — if GATT wins) — PARKED 2026-09-05

Superseded for the MVP: the browser page the sidekick serves (§5.0) covers "value-add helpers" without a native app on either platform. Kept below only as reference should a native-app phase be explicitly opened later.

Skip this section’s App Store app if I19’s Safari path passes and we lock that instead. The phone still provides **value-add helpers**, not radio controls. It talks to the **Nano**, not to the ADV.

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

I3 is sequenced ([ROADMAP.md](../ROADMAP.md) Now), amended 2026-09-05 for the chip pivot. Phone path is **decided** (§5.0, WiFi + browser) — no longer an open branch this table needs to carry.

| Phase | Firmware | App / RFC | Exit |
|---|---|---|---|
| **0 — RFC** | None | This file, including §4.1b, §4.6 | Sign-off: on-chip BLE is out; sidekick UART is the coprocessor; **phone path decided** (WiFi + browser, §5.0) |
| **1a — Same-chip NimBLE** | B15 CTS probe | — | **Closed, failed** (§4.1b) |
| **1a′ — Headless Atom-as-main** | AtomS3 Lite USB-host bench POC | — | **Closed, failed, hardware-confirmed** (§4.6). Not revisited without a different, proven-host board. |
| **1b — USB-C presence** | B18: always-on host; attach/detach toasts; **S → 2** probes existing UAC (no bus reset); park for CTS | — | **Done.** Empty = no toast; **S → 2** streams without QMX reboot; Grove ping **rejected**. |
| **1c — Sidekick build embed + field flash (retarget in progress)** | `esp32s3` sidekick project in-tree (§5.1, was `esp32c6`); ADV build embeds its `.bin`; install prompt (§5.4) flashes over USB-C ROM bootloader | — | Was proven for NanoC6 (2026-09-03); needs re-verification on AtomS3 Lite: `.bin` fits `partitions.csv` remainder (8 MB now, easier), field-flash succeeds or falls back to desk `esptool.py`, `nano_flasher.cpp:116`'s chip-type check widened to accept `ESP32S3_CHIP` |
| **1 — UART + sidekick WiFi/HTTP** | Grove UART on the now-flashed sidekick, feeding a WiFi AP/STA + browser-served HTTP UI. ADV: queue + UART TX, not `main.cpp` policy | None — plain browser, iPhone or Android | QMX CAT/`TA` unchanged with sidekick powered; decode lines visible in a browser with no app installed; ADV **DM L** stays in the QMX-only ballpark |
| **2 — Log Sync** | `LOG_META`-equivalent + blocks over UART then HTTP | Browser: `.adi` pull | No slot stall; FATFS worker |
| **3 — Helpers** | Unchanged | QRZ, grid, mapping, PSK Reporter — web-rendered, not native | Field tool |
| **4 — Native app (parked)** | Same sidekick, GATT added if opened later (§5.3) | iOS/Android native app | Only pursued if browser UX proves insufficient in the field |

*KB2SLO owns this. Public GitHub. Sidekick firmware source lives in this tree and is built by the ADV build (§5.1), but ships as two artifacts: the merged ADV image (sidekick `.bin` embedded for field flash) and a standalone sidekick `.bin` for desk flash.*

---

## 8. Risk Analysis & Risk Bounding

| Risk | Mitigation |
|---|---|
| **NimBLE vs CDC-ACM on ADV** | Do not run NimBLE on the ADV with QMX up. Nano owns BLE. |
| **Treating 94 KB DIRAM remain as BLE headroom** | §4.2 / §4.1b |
| **UART in the decode task** | Queue; low-priority drain |
| **Sidekick 8 MB + Wi-Fi/HTTP** | AtomS3 Lite's 8 MB gives real headroom over the Nano's 4 MB; measure the actual binary before assuming, same discipline as everywhere else in this RFC |
| **AtomS3 Lite antenna** | Datasheet-described as larger/stronger than the Nano's ceramic antenna; unmeasured for this specific WiFi-AP/STA use, verify in the field |
| **PORTA vs LoRa GNSS** | Companion UART is PORTA; LoRa GNSS is UART2 and can coexist. Don’t put GPS puck and Nano on PORTA together. |
| **Grove ping as presence** | **Rejected.** Factory Nano is silent on Grove; PORTA has idle-high pull-ups. Presence is USB-C VID/PID (B18). |
| **B18 host vs later UAC** | Host up from boot with QMX FIFO. **S → 2** attaches UAC/CDC only. Fail if that second `usb_host_install` is required. |
| **B18 vs C / CTS** | Park host (uninstall) for Drive and CTS start; reinstall host after, not UAC. |
| **5VOUT vs Nano USB-C** | One 5 V source |
| **FATFS / USB Drive** | Same as before: abort log pull |
| **Scope creep ("Phone as Radio")** | Non-goals (§2), reaffirmed 2026-09-05 after the headless detour closed: ADV stays the single source of truth, phone/browser is spectator + librarian only, zero CAT/QSY/TX from the sidekick's WiFi UI |
| **Assuming an unverified Atom SKU can USB-host a QMX** | **Rejected, hardware-confirmed** (§4.6). Any future headless-as-main attempt repeats this exact bench test on the specific candidate board before writing application code, not after |
| **DSP jitter** | Nano absorbs BLE; ADV does not wait |
| **Embedded Nano `.bin` blows the ADV flash budget** | Measure against `partitions.csv` remainder before merging; fail the build loudly rather than silently truncate |
| **ROM-bootloader auto-reset over USB-CDC doesn't work from ADV** | **Resolved.** Proven on real hardware 2026-09-03 (`components/nano_flasher/`): field-flashed a factory M5NanoC6, confirmed booting `sidekick` via monitor on its own USB-C afterward. Fallback (desk flash, §5.2) stays available regardless. |
| **Field-flash bricks a Nano mid-write** | ROM bootloader flash is fail-safe (the ROM stays resident until a valid app is written); worst case is retry, not a bricked board. Confirm before shipping the prompt to operators |

---

## 9. Ask of the Mini-FT8 Maintainers

1. Accept §4.1b: on-chip companion BLE is **rejected** on ADV+QMX.
2. Accept §4.6: headless-Atom-as-main is **closed, hardware-confirmed** — AtomS3 Lite cannot USB-host a QMX. Phase 1 is not headless. The ADV remains the main FT8 processor, unchanged, indefinitely unless a *different*, proven-USB-host board is sourced and separately bench-tested.
3. Accept §5.1/§5.0 pivot: sidekick chip moves from **NanoC6 to AtomS3 Lite**, chosen for dual-core WiFi+HTTP headroom. Phone path is **decided**: WiFi + a plain browser, no app, iPhone and Android from day one — decisive because iOS Safari has no Web Bluetooth support, not merely preferred. GATT/native-app work (§5.3/§6) is parked, not deleted.
4. Accept that internet-routability is **explicitly out of scope** for this project. Local-network reachability only; remote access is the operator's own VPN/tunnel to set up.
5. Accept §5.2 onward as a **Nano-era reference design**, not a description of what's built today for AtomS3 Lite — the retargeting work (ROADMAP I3, amended) is tracked separately and re-verifies each hardware-specific claim rather than assuming it carries over.
6. Do not merge ADV NimBLE-on-while-QMX. B15 one-shot CTS is a separate product decision (ROADMAP).
7. Field, once I3's retarget lands: embedded `.bin` fits flash budget on the new chip; field-flash prompt installs and boots the sidekick over USB-C, or falls back cleanly to desk flash; sidekick powered, QMX streaming, ADV **DM L** and CAT/`TA` unchanged; decode lines visible in a browser on an unmodified iPhone and an unmodified Android phone, no app installed on either.
