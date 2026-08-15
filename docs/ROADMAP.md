# Mini-FT8 roadmap (fork)

Living plan for this fork. Chat is intake; **this file is the source of truth** for later implementation (human or LLM). Git history is the changelog — do not keep a narrative log here.

Architecture details stay in `AUTOSEQ_ARCHITECTURE.md` and `AUTOSEQ_INACTIVE_QUEUE.md`. Long features get an RFC; this file only points at them.

Work on the fork first. Upstream what is clearly useful.

## How to use this file

Each item has a **type**: `fix` | `extract` | `feature` | `ci` | `docs`.

- **Now** — next implementation, one theme at a time.
- **Backlog** — agreed, sequenced, not started.
- **Ideas** — not scheduled. Do not implement from this section unless moved up.
- **Done** — one line + commit when known.

Do not expand `main.cpp` (~6000 lines) without extracting a tested function. Do not mix a `feature` PR with an unrelated `fix`.

## Now

CI firmware artifacts are on branch `ci` (not merged). Merge when the first GitHub Actions binary looks right.

| ID | Type | Item | Done when |
|---|---|---|---|
| N1 | ci | Merge `ci` workflow: host_mock + tx_e2e + IDF 5.5.1 `esp32s3` build; artifact `MiniFT8_Merged_Auto.bin` at `0x0`; `v*` GitHub Release | Workflow on `main`; artifact downloads and flashes |

## Backlog

Highest-ROI host tests while extracting: Station.txt round-trip, SD import must not clobber newer flash, ADIF 10-min dedupe + R-sort groups, beacon re-arm in the same tick as pop.

| ID | Type | Item | Why / constraint |
|---|---|---|---|
| B1 | fix | Beacon CQ pop vs re-arm | Tick pops CALLING; re-arm is later `arm_from_autoseq_or_beacon()`. T can look empty while beacon is ON (worse if FATFS blocks the main loop). `host_mock/test_beacon_lifecycle.json` currently **expects** empty queue after tick. Fix under a lock; update the mock. |
| B2 | fix | Autoseq unlocked | Decode (`ft8_audio_pipeline`, core 1) vs tick/UI (core 0). Mutex around public API, or confine autoseq to core 0. Do this before or with B1. |
| B3 | extract | Pure functions host+firmware both link | Station parse/serialize, ADIF format + dedupe, TA format (stop reimplementing in `tx_e2e`), decode sort, power hysteresis (mV → halt/write/warn). |
| B4 | fix | SD `Station.txt` clobbers flash | `storage_sync_station_from_sd()` on boot, no timestamp. Stale SD can wipe `gnss_lora`. Flash is source of truth; SD is export unless newer **and** operator opted in. |
| B5 | fix | `sscanf(line, "date=%63s", line)` (and `time=`) | Destination overlaps scan buffer. Fix in the extracted Station parser (B3). |
| B6 | fix | Held key stalls slot | `c == last_key` skips `check_slot_boundary` / `tx_tick`. |
| B7 | extract | Main loop never blocks on FATFS | QSO browse/copy and Station save on a worker or time-sliced reads. Slot/TX keep running. |
| B8 | fix | `DECODE_HEAP` every decode | `ESP_LOGW` enter/exit; UART time on a tight loop. Demote or gate. |

## Ideas

Not scheduled. Move to Backlog with a Done-when before implementing.

| ID | Type | Item | Notes |
|---|---|---|---|
| I1 | feature | CQ-only R filter | Community ask; sort/filter only, after decode-sort extract (B3). |
| I2 | feature | Accidental `C` (USB Drive) guard | Easy to enter MSC; confirm or require long-press. |
| I3 | feature | BLE companion | Optional add-on. Spec: `docs/rfcs/0001-ble-companion.md` (branch `rfc/0001-ble-companion`). |
| I4 | docs | `NATIVE_CLIENT_ARCHITECTURE.md` | Referenced by `core_api.cpp`; missing. Write when `core_api` is more than a facade over `main.cpp` globals. |
| I5 | feature | More radios (FTX-1, FT-817, …) | Field + CAT; not host-CI. After radio_control is less tangled with `main`. |

Field-only (do not fake in CI): USB/CDC/QMX CAT, UAC timing/DRAM, display/SPI, full-slot audio.

## Done

| ID | Type | Item | Where |
|---|---|---|---|
| D1 | fix | Low-batt halt: R overlay + STATUS beacon stays OFF | `main` `4beace4` |
| D2 | docs | This roadmap (was quality-only) | `docs/ROADMAP.md` |

## Test map

| Harness | Covers | Command |
|---|---|---|
| `host_mock/` | `autoseq.cpp`; JSON QSO / FD / beacon / reincarnation | `cd host_mock && make && ./host_test test_qso.json` |
| `tests/tx_e2e/` | Encode, TA format, KH1 map, golden WAV RX | `cmake -S tests/tx_e2e -B tests/tx_e2e/build && cmake --build tests/tx_e2e/build && ctest --test-dir tests/tx_e2e/build --output-on-failure` |
| `idf.py build` | `main/` `-Werror`; merged bin | ESP-IDF 5.5.1, target `esp32s3` |

`core_api` still `extern`s `main.cpp` globals. TA format in `tx_e2e` is a copy, not the firmware function.
