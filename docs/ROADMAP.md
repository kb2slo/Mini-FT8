# Mini-FT8 roadmap (fork)

Living plan for this fork. How to use it, and the rest of `docs/`, is in [README.md](README.md).

Architecture details stay in `AUTOSEQ_ARCHITECTURE.md` and `AUTOSEQ_INACTIVE_QUEUE.md`. Long features get an RFC; this file only points at them.

Work on the fork first. Upstream what is clearly useful.

## Now

First-phase CI is on `main` (this PR). Next theme is the sequencer.

| ID | Type | Item | Done when |
|---|---|---|---|
| N2 | fix | Lock autoseq; atomic beacon CQ re-arm | Public autoseq API serialized (mutex or core 0 only). Tick pop of CALLING + `start_cq` under that lock when beacon is on. `host_mock/test_beacon_lifecycle.json` expects a re-armed queue, not empty. Was B1/B2. |
| N3 | fix | Unique remote callsign in TX queue on R-tap | `autoseq_on_touch`: no duplicate callsign; re-tap promotes to front; live `queue[0]` QSO ignores with HUD `QSO in progress`. Host: `host_test_unique_callsign`. |

## Backlog

Highest-ROI host tests while extracting: Station.txt round-trip, SD import must not clobber newer flash, ADIF 10-min dedupe + R-sort groups, beacon re-arm in the same tick as pop.

| ID | Type | Item | Why / constraint |
|---|---|---|---|
| B3 | extract | Pure functions host+firmware both link | Station parse/serialize, ADIF format + dedupe, TA format (stop reimplementing in `tx_e2e`), decode sort, power hysteresis (mV → halt/write/warn). |
| B4 | fix | SD `Station.txt` clobbers flash | `storage_sync_station_from_sd()` on boot. **Partial:** skip import when internal `Station.txt` already exists. Still no timestamp/opt-in if flash is empty and SD is stale. |
| B5 | fix | `sscanf(line, "date=%63s", line)` (and `time=`) | Destination overlaps scan buffer. Fix in the extracted Station parser (B3). |
| B6 | fix | Held key stalls slot | `c == last_key` skips `check_slot_boundary` / `tx_tick`. |
| B7 | extract | Main loop never blocks on FATFS | QSO browse/copy and Station save on a worker or time-sliced reads. Slot/TX keep running. |
| B8 | fix | `DECODE_HEAP` every decode | `ESP_LOGW` enter/exit; UART time on a tight loop. Demote or gate. |
| B9 | extract | `ft8_lib` as an updatable dependency | Vendored under `components/ft8_lib/` with no upstream SHA; ESP static-FFT and FD/nonstd/Dxpedition patches live in Karlis’s files. Submodule (or our fork) plus Mini-FT8 wrappers for buffers/`fft_wrapper`; keep protocol patches rebaseable. Done when a documented 3-way merge from `kgoba/ft8_lib` plus `tx_e2e` golden RX/encode is the bump path. |

## Ideas

Not scheduled. Move to Backlog with a Done-when before implementing. Large product shifts get an RFC in `docs/rfcs/` before code.

| ID | Type | Item | Notes |
|---|---|---|---|
| I1 | feature | CQ-only R filter | Community ask; sort/filter only, after decode-sort extract (B3). |
| I2 | feature | Accidental `C` (USB Drive) guard | Easy to enter MSC; confirm or require long-press. |
| I3 | feature | Companion app | Phone/desktop beside the Cardputer: logs, spotting, maybe config. Started as BLE RFC: `docs/rfcs/0001-ble-companion.md` on branch `rfc/0001-ble-companion`. Transport (BLE vs USB vs Wi‑Fi) is still open; do not grow `main.cpp` into an app server. Ties to I4 (`core_api` / native client). |
| I4 | docs | `NATIVE_CLIENT_ARCHITECTURE.md` | Referenced by `core_api.cpp`; missing. Write when `core_api` is more than a facade over `main.cpp` globals. Needed for I3. |
| I5 | feature | More radios (FTX-1, FT-817, …) | Field + CAT; not host-CI. After radio_control is less tangled with `main`. |
| I6 | feature | Multi-mode product (working name `miniFTx` or similar) | Stop treating FT8 as the product. FT4 should be first-class (already compile/runtime, not a second-class `ENABLE_FT4` carve-out). JS8Call next as a real mode (timing, alphabet, autoseq — not a skin). Prefer a name that is not FT-only; exact name is an RFC, not a drive-by rename of repo/binaries. Depends on extract (B3) and a mode-agnostic slot/audio core. |
| I7 | ci | Hash-first build identity | In progress on `feat/build-identity`. `a9f64c2-minift8-dev.bin` (dirty: `-dirty-`); PERF line `dev a9f64c2*`. CI zip must contain that name, not `MiniFT8_Merged_Auto.bin`. |
| I8 | feature | QMX band SWR curve | Field goal: SWR-vs-frequency for the **selected band**. Mini-FT8 orchestrates: `MD8;` (SWR Tune → PA at Protection **Tune %**) → step `FA` + poll `SW;` → `MD0;` + restore digi (`MD6;`). No Kenwood `PC` *set* (`PC;` is get-only wattmeter — [CAT 1_04_004](https://www.qrp-labs.com/images/qmx/manuals/cat_1_04_004.pdf)). Tune % stays an on-radio setting; `MM` only if we must change it from CAT. Hardware-prove `MD8` keys TX, `FA`/`SW;` work, then extract out of `main.cpp`. |
| I9 | feature | QMX terminal (settings / alignment) | Separate from I8. QMX’s rich serial terminal (menus, RF/SWR/image sweeps, config) is not Kenwood CAT. Full VT-style UI on 240×135 is a large product; likely needs an RFC and may fit the companion (I3) better than Cardputer-native. Shared USB-C with UAC/CAT — exclusive session, leave digi mode. |

Field-only (do not fake in CI): USB/CDC/QMX CAT, UAC timing/DRAM, display/SPI, full-slot audio.

## Done

| ID | Type | Item | Where |
|---|---|---|---|
| D1 | fix | Low-batt halt: R overlay + STATUS beacon stays OFF | `main` `4beace4` |
| D2 | ci | First-phase CI: host_mock + tx_e2e + IDF 5.5.1 `esp32s3` merged `MiniFT8_Merged_Auto.bin` artifact; Node 24 actions | this PR (`ci` → `main`) |
| D3 | ci | Merge to `main` publishes rolling GitHub prerelease tag `dev` (`minift8-dev.bin`). `v*` tags stay versioned releases. | `feat/ci-main-release` |
| D4 | fix | Internal FAT append left 0-byte ADI/RT after first-boot format; Station atomic OK | Atomic create + POSIX `O_APPEND`+fsync; WL SAFE; `FATFS_IMMEDIATE_FSYNC`. Hardware: QSO logged. |

## Test map

| Harness | Covers | Command |
|---|---|---|
| `host_mock/` | `autoseq.cpp`; JSON QSO / FD / beacon / reincarnation; unique R-tap callsign | `cd host_mock && make && ./host_test test_qso.json` / `./host_test_unique_callsign` |
| `tests/tx_e2e/` | Encode, TA format, KH1 map, golden WAV RX | `cmake -S tests/tx_e2e -B tests/tx_e2e/build && cmake --build tests/tx_e2e/build && ctest --test-dir tests/tx_e2e/build --output-on-failure` |
| `idf.py build` | `main/` `-Werror`; merged bin | ESP-IDF 5.5.1, target `esp32s3` |

`core_api` still `extern`s `main.cpp` globals. TA format in `tx_e2e` is a copy, not the firmware function.
