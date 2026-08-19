# Mini-FT8 roadmap (fork)

Living plan for this fork. How to use it, and the rest of `docs/`, is in [README.md](README.md).

Architecture details stay in `AUTOSEQ_ARCHITECTURE.md` and `AUTOSEQ_INACTIVE_QUEUE.md`. Extract/style/boundaries: [RFC 0002](rfcs/0002-extract-and-boundaries.md). Long features get an RFC; this file only points at them.

Ship on `origin/main`. Talk through changes before a local commit (see [README.md](README.md)). Do not open `upstream` PRs unless explicitly asked.

## Now

| ID | Item | Done when |
|---|---|---|
| RFC 0002 | Extract, style, and boundaries | Outcomes in [RFC 0002](rfcs/0002-extract-and-boundaries.md) §2. No open extract slice. Slices A–B and D–G done. Slice C (`ft8_lib` pin) parked. Was I11. |

## Backlog

| ID | Item | Why / constraint |
|---|---|---|
| B4 | SD `Station.txt` clobbers flash | `storage_sync_station_from_sd()` on boot. **Partial:** skip import when internal `Station.txt` already exists. Still no timestamp/opt-in if flash is empty and SD is stale. Rides RFC 0002 slice D (Station extract). |
| B7 | Main loop never blocks on FATFS | QSO browse/copy and Station save on a worker or time-sliced reads. Slot/TX keep running. After those have a module to move (RFC 0002); not a separate Now. |
| B8 | `DECODE_HEAP` every decode | `ESP_LOGW` enter/exit; UART time on a tight loop. Demote or gate. |
| B10 | See callsigns while TX HUD would cover the R list | Queue a QSO when the HUD would otherwise eat the decode list. **UX deferred until this is Now:** repeated `R` toggle vs dedicated `H` (HUD) vs always-visible list + banner. Abort must stay visible. Same unique-callsign tap rules as D8. Field-only display; not CI. Distinct from CQ-only filter (I1). Was I14. |
| B11 | QMX+ AUX GPS into ADV Grove | Route QMX+ GPS NMEA **out AUX** over a 2.5mm-to-Grove cable into Cardputer PORTA; USB-C stays QMX UAC+CAT. Explore pinout/levels/baud, fix firmware if PORTA GPS does not lock with QMX+ selected, document the operator path (G-screen, STATUS `G`, `GNSS_LoRa` off so PORTA is used). Distinct from a GPS puck on Grove and from the LoRa-1262 GNSS cap. Field-only; ADV + QMX+. Do not mix into RFC 0002. |
| B12 | Keep-launcher QSO logging | QSO logging failed after first test of the keep-launcher script. Circle back to revisit. |
| B6 | Held key / splash / USB Drive skip some loop work | Last among Backlog; circle back later. Aug 15 code audit, never field-observed. The original note (`last_key` skips slot/TX) is stale on normal RX: those ticks already run earlier in the loop. Remaining: startup splash and USB Drive (`C`) still `continue` without slot/TX ticks; a held key still skips pending time-sync and some HUD flash ticks. Confirm on ADV (hold a key across a slot) before changing the loop. Field-only. |

## Ideas

Not scheduled. Move to Backlog with a Done-when before implementing. Large product shifts get an RFC in `docs/rfcs/` before code.

| ID | Item | Notes |
|---|---|---|
| I1 | CQ-only R filter | Community ask; sort/filter only, after decode-sort extract (RFC 0002 slice F). |
| I2 | Accidental `C` (USB Drive) guard | Easy to enter MSC; confirm or require long-press. |
| I3 | Companion app | Phone/desktop beside the Cardputer: logs, spotting, maybe config. Started as BLE RFC: `docs/rfcs/0001-ble-companion.md` on branch `rfc/0001-ble-companion`. Transport (BLE vs USB vs Wi‑Fi) is still open; do not grow `main.cpp` into an app server. Ties to I4 (`core_api` / native client). |
| I4 | `NATIVE_CLIENT_ARCHITECTURE.md` | Referenced by `core_api.cpp`; missing. Write when `core_api` is more than a facade over `main.cpp` globals. Needed for I3. |
| I5 | More radios (FTX-1, FT-817, …) | Field + CAT; not host-CI. After radio_control is less tangled with `main`. |
| I6 | Multi-mode product (working name `miniFTx` or similar) | Stop treating FT8 as the product. FT4 should be first-class (already compile/runtime, not a second-class `ENABLE_FT4` carve-out). JS8Call next as a real mode (timing, alphabet, autoseq — not a skin). Prefer a name that is not FT-only; exact name is an RFC, not a drive-by rename of repo/binaries. Depends on RFC 0002 extracts and a mode-agnostic slot/audio core. |
| I8 | QMX band SWR curve | Field goal: SWR-vs-frequency for the **selected band**. Mini-FT8 orchestrates: `MD8;` (SWR Tune → PA at Protection **Tune %**) → step `FA` + poll `SW;` → `MD0;` + restore digi (`MD6;`). No Kenwood `PC` *set* (`PC;` is get-only wattmeter — [CAT 1_04_004](https://www.qrp-labs.com/images/qmx/manuals/cat_1_04_004.pdf)). Tune % stays an on-radio setting; `MM` only if we must change it from CAT. Hardware-prove `MD8` keys TX, `FA`/`SW;` work, then extract out of `main.cpp`. |
| I9 | QMX terminal (settings / alignment) | Separate from I8. QMX’s rich serial terminal (menus, RF/SWR/image sweeps, config) is not Kenwood CAT. Full VT-style UI on 240×135 is a large product; likely needs an RFC and may fit the companion (I3) better than Cardputer-native. Shared USB-C with UAC/CAT — exclusive session, leave digi mode. |
| I10 | QMX CAT power-off with Cardputer idle | On Charge Mode (or a later deeper sleep), send QMX `PS0;` so the radio is not left keyed/USB-hosted. QMX-only ([CAT 1_04_004](https://www.qrp-labs.com/images/qmx/manuals/cat_1_04_004.pdf)); never send on QDX/KH1. After `PS0;` CAT is dead until the operator long-presses VOL on the radio — Mini-FT8 cannot wake it. Hardware: confirm `PS0;` saves VFO/mode like the VOL shutdown. Do not mix into Charge Mode without an explicit confirm or a QMX-only path. |
| I12 | Mine public Mini-FT8 forks | Distinct survey: forks of `wcheng95/Mini-FT8` (and notable related trees) for patches worth adopting. Done when there is a written take / leave / defer list. No silent merges. Not part of RFC 0002 extracts. |
| I13 | Learn from best-in-class OSS during extracts | When doing RFC 0002 work (radio table, `ft8_lib` vendor boundary, Station, …), look at how strong public projects handle that seam. Lessons go into STYLE or the RFC, not a second plan. Not a rewrite. Distinct from mining Mini-FT8 forks (I12). |
| I15 | Make clock-out-of-sync obvious | Two layers. **Fine:** WSJT-X-style DT (seconds vs the slot) on decodes, or a slot-summary, so a systematic ~1s offset is obvious. **Gross:** cue when the clock is so far off that FT8 cannot decode (empty slots with RF/audio otherwise OK), or the date/time is obviously wrong (STATUS vs GPS/UTC). Empty-band, unplugged radio, and UAC failure look like “no decodes” — do not treat silence as proof of clock. **UX deferred until sequenced.** Do not auto-nudge the clock. Distinct from sleep-RTC compensation (`RTC_COMPENSATION.md`). After decode-list extract (RFC 0002 slice F); do not grow `main.cpp` for a layout experiment. Field-only. |
| I16 | Optional TX meter poll off | QMX HUD `PC;SW;` ~1/s during TX shares CDC with `TA`. Keep `has_wattmeter` / `has_swr`; operator can disable poll to test CAT load (late `TA`, USB glitches). Field-only. Station/HUD setting; do not mix into RFC 0002 slice B. Distinct from I8 (SWR sweep). |
| I17 | Log verbosity | Off / ADI (QSOs) / full RX. |

Field-only (do not fake in CI): USB/CDC/QMX CAT, UAC timing/DRAM, display/SPI, full-slot audio.

## Done

| ID | Item | Where |
|---|---|---|
| D1 | Low-batt halt: R overlay + STATUS beacon stays OFF | `main` `4beace4` |
| D2 | First-phase CI: host_mock + tx_e2e + IDF 5.5.1 `esp32s3` merged `MiniFT8_Merged_Auto.bin` artifact; Node 24 actions | this PR (`ci` → `main`) |
| D3 | Merge to `main` publishes rolling GitHub prerelease tag `dev` (`minift8-dev.bin`). `v*` tags stay versioned releases. | `feat/ci-main-release` |
| D4 | Internal FAT append left 0-byte ADI/RT after first-boot format; Station atomic OK | Atomic create + POSIX `O_APPEND`+fsync; WL SAFE; `FATFS_IMMEDIATE_FSYNC`. Hardware: QSO logged. |
| D5 | CLI flash that keeps Launcher | `tools/flash_keep_launcher.py` writes `mini_ft8.bin` to the device OTA slot only |
| D6 | Beacon OFF drops queued CQ | `main` `803e8cf`; host `host_test_beacon_cancel` |
| D7 | Skip firmware artifact unless the binary can change | `ci.yml` path filter; docs/roadmap-only pushes do not rebuild or retag `dev` |
| D8 | Unique remote callsign in TX queue on R-tap | `main` `4f19015`; host `host_test_unique_callsign` |
| D9 | Copy Files to SD unions `.adi` onto the card | `components/adif`; host `host_test_adif_merge`. Archive wins on duplicate key; unparseable SD file is not overwritten. Other files still byte-copy. |
| D10 | Extract/style/boundaries RFC + agent workflow tenant | `docs/rfcs/0002-extract-and-boundaries.md`; `STYLE.md`; `docs/README.md` |
| D11 | Push back before tools | Agents cite Now/RFC/STYLE before any implementation search or edit |
| D12 | Hash-first firmware filenames | `b5a9479`; CI stages hash-first merged image plus `minift8-dev.bin`. Was I7. |

## Test map

| Harness | Covers | Command |
|---|---|---|
| `host_mock/` | `autoseq.cpp`; JSON QSO / FD / beacon / reincarnation; unique R-tap callsign; ADIF merge and logger 10-min window; Station parse; decode sort; battery hold hysteresis | `cd host_mock && make && ./host_test test_qso.json` / `./host_test_unique_callsign` / `./host_test_adif_merge` / `./host_test_station` / `./host_test_decode_sort` / `./host_test_power_hysteresis` |
| `tests/tx_e2e/` | Encode, TA format, KH1 map, golden WAV RX | `cmake -S tests/tx_e2e -B tests/tx_e2e/build && cmake --build tests/tx_e2e/build && ctest --test-dir tests/tx_e2e/build --output-on-failure` |
| `idf.py build` | `main/` `-Werror`; merged bin | ESP-IDF 5.5.1, target `esp32s3` |

`core_api` still `extern`s `main.cpp` globals. TA format is `radio_ta_format()` in firmware and `tx_e2e`.
