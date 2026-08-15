# Mini-FT8 roadmap (fork)

Living plan for this fork. How to use it, and the rest of `docs/`, is in [README.md](README.md).

Architecture details stay in `AUTOSEQ_ARCHITECTURE.md` and `AUTOSEQ_INACTIVE_QUEUE.md`. Long features get an RFC; this file only points at them.

Work on the fork first. Upstream what is clearly useful.

## Now

First-phase CI is on `main` (this PR). Next theme is the sequencer.

| ID | Type | Item | Done when |
|---|---|---|---|
| N2 | fix | Lock autoseq; atomic beacon CQ re-arm | Public autoseq API serialized (mutex or core 0 only). Tick pop of CALLING + `start_cq` under that lock when beacon is on. `host_mock/test_beacon_lifecycle.json` expects a re-armed queue, not empty. Was B1/B2. |

## Backlog

Highest-ROI host tests while extracting: Station.txt round-trip, SD import must not clobber newer flash, ADIF 10-min dedupe + R-sort groups, beacon re-arm in the same tick as pop.

| ID | Type | Item | Why / constraint |
|---|---|---|---|
| B3 | extract | Pure functions host+firmware both link | Station parse/serialize, ADIF format + dedupe, TA format (stop reimplementing in `tx_e2e`), decode sort, power hysteresis (mV → halt/write/warn). |
| B4 | fix | SD `Station.txt` clobbers flash | `storage_sync_station_from_sd()` on boot, no timestamp. Stale SD can wipe `gnss_lora`. Flash is source of truth; SD is export unless newer **and** operator opted in. |
| B5 | fix | `sscanf(line, "date=%63s", line)` (and `time=`) | Destination overlaps scan buffer. Fix in the extracted Station parser (B3). |
| B6 | fix | Held key stalls slot | `c == last_key` skips `check_slot_boundary` / `tx_tick`. |
| B7 | extract | Main loop never blocks on FATFS | QSO browse/copy and Station save on a worker or time-sliced reads. Slot/TX keep running. |
| B8 | fix | `DECODE_HEAP` every decode | `ESP_LOGW` enter/exit; UART time on a tight loop. Demote or gate. |

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

Field-only (do not fake in CI): USB/CDC/QMX CAT, UAC timing/DRAM, display/SPI, full-slot audio.

## Done

| ID | Type | Item | Where |
|---|---|---|---|
| D1 | fix | Low-batt halt: R overlay + STATUS beacon stays OFF | `main` `4beace4` |
| D2 | ci | First-phase CI: host_mock + tx_e2e + IDF 5.5.1 `esp32s3` merged `MiniFT8_Merged_Auto.bin` artifact; Node 24 actions | this PR (`ci` → `main`) |

## Test map

| Harness | Covers | Command |
|---|---|---|
| `host_mock/` | `autoseq.cpp`; JSON QSO / FD / beacon / reincarnation | `cd host_mock && make && ./host_test test_qso.json` |
| `tests/tx_e2e/` | Encode, TA format, KH1 map, golden WAV RX | `cmake -S tests/tx_e2e -B tests/tx_e2e/build && cmake --build tests/tx_e2e/build && ctest --test-dir tests/tx_e2e/build --output-on-failure` |
| `idf.py build` | `main/` `-Werror`; merged bin | ESP-IDF 5.5.1, target `esp32s3` |

`core_api` still `extern`s `main.cpp` globals. TA format in `tx_e2e` is a copy, not the firmware function.
