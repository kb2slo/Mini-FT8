# Quality roadmap

Living list of tests, known defects, CI, and how to shrink `main.cpp` without a rewrite.
Not a second architecture spec; see `AUTOSEQ_ARCHITECTURE.md` and `AUTOSEQ_INACTIVE_QUEUE.md` for sequencer design.

Work on the fork first. Pieces that are clearly useful (host CI, Station parse tests) can go upstream later.

## Current test map

| Harness | What it covers | How to run |
|---|---|---|
| `host_mock/` | `autoseq.cpp` on the desktop; JSON QSO / FD / beacon / reincarnation scenarios | `make` then `./host_test test_qso.json` (and other `test_*.json`) |
| `tests/tx_e2e/` | Encode, TA format, KH1 tone map, golden WAV RX, fake TX machine | `cmake -S tests/tx_e2e -B tests/tx_e2e/build && cmake --build tests/tx_e2e/build && ctest --test-dir tests/tx_e2e/build --output-on-failure` |
| `main/` compile | `-Werror` | `idf.py build` (ESP-IDF 5.5.1, `esp32s3`) |

`core_api` is a UI-agnostic facade; it still `extern`s `main.cpp` globals. `core_api.cpp` references `docs/NATIVE_CLIENT_ARCHITECTURE.md`, which is not in the tree.

TA format is **reimplemented** in `tx_e2e`, not linked from `radio_control_qmx.cpp`. Firmware and test can drift.

## Known defects

Status: **open** unless noted.

| Item | Notes |
|---|---|
| Beacon CQ pop vs re-arm | Tick pops CALLING; re-arm is a later `arm_from_autoseq_or_beacon()`. Not atomic. T can look empty while beacon is ON (worse when FATFS blocks the main loop). `host_mock/test_beacon_lifecycle.json` currently **expects** an empty queue after tick (one-shot design). |
| SD `Station.txt` clobbers flash | `storage_sync_station_from_sd()` on boot copies SD over flash with no timestamp. Stale SD can wipe `gnss_lora` and other keys. |
| `sscanf(line, "date=%63s", line)` | Same for `time=`. Destination overlaps the scan buffer. Often works; still wrong and untested. |
| Autoseq unlocked | Decode path (`ft8_audio_pipeline`, typically core 1) calls into autoseq; core 0 ticks/UI. `s_queue` is shared. ADIF logging was already moved off that race. |
| Held key stalls slot | `c == last_key` skips `check_slot_boundary` / `tx_tick`. |
| FATFS on the main loop | QSO browse / copy blocks the same path as slot, TX, and beacon re-arm. |
| `DECODE_HEAP` every decode | `ESP_LOGW` enter/exit; UART time on a tight loop. |
| Low-batt halt vs STATUS beacon | Halt forces beacon OFF. **Done** on `main` (`4beace4`): R overlay + STATUS redraw so `S→1` stays OFF. |

Highest-ROI new host tests: Station.txt round-trip, SD import must not clobber newer flash, ADIF 10-min dedupe + R-sort groups, beacon re-arm in the same tick as pop.

## CI

Firmware + host tests are staged on branch **`ci`** (not merged to `main` yet):

- Host: all `host_mock/test_*.json` plus `tx_e2e` CTest.
- Firmware: ESP-IDF **v5.5.1**, target **esp32s3**, `idf.py build`. Artifact `MiniFT8-Cardputer-ADV` is `MiniFT8_Merged_Auto.bin` (flash at `0x0`).
- Tags `v*` attach that image to a GitHub Release.

Field-only (do not try to fake in CI): USB/CDC/QMX CAT, UAC timing/DRAM, display/SPI, full-slot audio.

## How to iterate (not a rewrite)

Do not expand `main.cpp` (~6000 lines: UI, slot, Station, GPS, ADIF, power, MSC, decode glue) without extracting a tested function.

0. **CI on the fork** — `ci` branch; merge when the first firmware artifact looks right.
1. **Lock the sequencer** — mutex around autoseq public API, or confine all autoseq calls to core 0 (decode posts a message). Then pop CALLING and `start_cq` under that lock when beacon is on.
2. **Extract pure functions** firmware and host both link: Station parse/serialize, ADIF format + dedupe, TA format, decode sort, power hysteresis (mV → halt/write/warn).
3. **Storage policy** — flash is source of truth; SD is export. Stop blind boot import, or import only if SD is newer **and** the operator opted in.
4. **Main loop never blocks on FATFS** — QSO page and Station save on a worker, or time-slice reads. Slot/TX keep running.
5. **Features after that** — CQ-only R filter, accidental-`C` guard, BLE RFC (`docs/rfcs/0001-ble-companion.md`).

Do not start with splitting UI from radio in one PR. Extract and test the bits that keep breaking: Station, ADIF, beacon, power.
