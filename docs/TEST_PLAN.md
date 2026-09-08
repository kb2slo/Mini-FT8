# Mini-FT8 test plan (kb2slo fork)

What is verified, by whom, and what is still owed. Companion to [ROADMAP.md](ROADMAP.md): the roadmap says
what we are building, this file says how we know it works. How to use both is in [README.md](README.md).

The split that matters: **an agent can prove some things and cannot prove others.** Everything under
[Automated](#automated) an agent runs itself and must run before saying work is done. Everything under
[Operator field checks](#operator-field-checks) needs real hardware — a Cardputer ADV, a QMX, an antenna,
sometimes a second station — and an agent must never claim it passed. Green host tests and a green build are
evidence about parsing, policy, and linkage. They are not evidence that the radio transmitted.

## Automated

Run by the agent locally, and by CI on every PR. No hardware.

| Harness | Command | Covers |
| --- | --- | --- |
| `host_mock` (17 binaries) | `make -C host_mock && host_mock/host_test*` | See table below |
| `tests/tx_e2e` | CTest, CI job **Host tests** | L1 encoder, TA format, golden WAV RX decode |
| Firmware build | `idf.py build` | `main/` under `-Werror`; merged image; must be **warning-free** |
| Sidekick build | CI job **Sidekick (ESP32-C6)** | Companion firmware compiles and stages |
| README audit | CI job **README audit** | A PR removing a `UIMode` enumerator or changing a key binding under `main/` must touch `README.md` |

### `host_mock` coverage

| Binary | Covers |
| --- | --- |
| `host_test` | Autoseq engine: JSON-driven QSO, Field Day, beacon, reincarnation, deadlock, freetext scenarios |
| `host_test_unique_callsign` | Unique-callsign touch dedupe and promote |
| `host_test_beacon_cancel` | Beacon-off cancels a queued CQ |
| `host_test_adif_merge` | ADIF merge export and the logger's 10-minute dedupe window |
| `host_test_station` | `Station.txt` parse / serialize round-trip |
| `host_test_station_save_queue` | Save coalescing off the slot loop |
| `host_test_qso_browse` | Daily `.adi` filter, record page, list lines |
| `host_test_file_list` | Directory listing / paging |
| `host_test_decode_sort` | Decode list ordering |
| `host_test_radio_profile` | Radio profile table; retired `KH1_USBC`/`KH1_MIC` values fall back to QMX |
| `host_test_band_config` | Band config parse and toggle policy |
| `host_test_power_hysteresis` | Battery hold / low-batt hysteresis |
| `host_test_copy_block` | Copy-to-SD blocking policy and menu line |
| `host_test_cts_time` | CTS / phone time parsing to `timeval` |
| `host_test_tx_hud_banner` | TX HUD banner state |
| `host_test_rx_list_stale` | RX list staleness marking |
| `host_test_usb_c_presence` | USB-C presence detection policy |

### What automation cannot see

Named explicitly so nobody mistakes a green run for coverage. None of the following has a harness:

- **The UI.** No test covers any `draw_*` function, the MENU screens, key dispatch, or `UIMode` transitions.
  A menu item can be reordered, mislabelled, or wired to the wrong action with every test green.
- **The main loop and slot state machine.** `app_task_core0`, `tx_tick`, `check_slot_boundary`.
- **Any field-only path**: USB host / UAC audio, CAT, CDC, display and SPI, GPS, DS3231, SD card, flash.
- **Timing.** Slot alignment, decode-window deadlines, TX start latency.

## Operator field checks

Real hardware. An agent proposes these and never marks them passed.

Standing rig: Cardputer ADV + QMX (or QMX+) over USB-C. The Cardputer's USB-C is **either** ESP serial/JTAG
**or** USB host for the radio, never both — live logs with the radio attached need the console UART on
**G4 (TX) / G5 (RX)**, and that path is off when `GNSS_LoRa:ON`.

### Smoke (any firmware change)

1. Boots to the R screen; decodes appear within a slot or two.
2. `S` → `2` starts QMX audio streaming; waterfall moves.
3. One full QSO end to end, logged.

### By subsystem

| Area | Check |
| --- | --- |
| TX / autoseq | R-tap a decode to reply; TX fires on the right slot parity; backtick cancels an in-flight TX |
| QSO queue | `T` screen lists active QSOs; drop one with `2`–`6`; paging with `;` / `.` |
| Logging | `.adi` and RXTX lines written; QSO browser (`Q`) reads them back |
| Copy to SD | MENU P3 → `5` reports `Copied OK` or `Missed [n]` |
| Station config | MENU edits persist to `Station.txt` and survive a reboot |
| Bands | Band config (`O` → `3`) toggles and edits kHz; STATUS rotation follows |
| Radio | Cycle radios on ADV; QMX QSO still works after the switch |
| Time | GPS sync; DS3231 persist; decode-median auto-sync |
| Power | Charge Mode (`M` → `6`); low-battery TX halt and resume |
| Sidekick | Presence prompt; field-flash over USB-C; PORTA UART link |

## Pending — owed on `refactor/main-subdirs`

This branch is taking cleanup aggressively on the model that one significant regression pass happens before
merge, rather than a field check per commit. **These are owed before this branch merges to `main`.**

Most commits on the branch removed code that was provably never in the image (verified by linker map plus
`nm` on the ELF, with a byte-identical `mini_ft8.bin` as the check). Those need no field test. Only the
commits below changed live code.

| From | Check | Status |
| --- | --- | --- |
| B28 `bb356f6` | MENU P1 `6` toggles `Mode: FT8` / `FT4` with `*`; survives `Station.txt` save + reboot | owed |
| `729a952` | UART screen mirror still dumps the screen on G4/G5 key injection | owed |
| B29 `21ed004` | MENU P3 `5` Copy files to SD → `Copied OK`; log writes normal during TX/decode | owed |
| `320ddf5` | `S` → `2` QMX audio starts and stops; decode works; radio change stops audio | owed |
| `671395b` | Waterfall still renders normally while streaming | owed |
| B30 `49f4f69` / `752473f` | R-tap a decode to reply; backtick cancel during TX; drop a QSO from the `T` list | owed |
| all | One full QSO on ADV + QMX, logged and readable in `Q` | owed |

## Keeping this file true

Same rule as the roadmap: **update this file in the same turn as the work.**

- Work adds or changes a **host-testable** unit (parse, format, policy) → add or update its row under
  [`host_mock` coverage](#host_mock-coverage) in the same commit as the code.
- Work touches a **field-only** path → add a row to [Pending](#pending--owed-on-refactormain-subdirs) naming
  the commit and the exact check. "Test the radio" is not a check; "MENU P3 `5` reports `Copied OK`" is.
- Work removes code that was **provably not in the image** — linker map plus `nm`, byte-identical binary —
  say so and add no row. That evidence is the test.
- The operator runs a pending check → drop the row. Rows do not accumulate across merges; a merged branch
  starts with an empty Pending section.
- Work removes a harness or a check → remove its row, and say why in the commit message.

An agent must say, in the turn where it reports work done, which of these it did. "No test-plan change
needed" is a valid answer and must be stated explicitly, the same way the README audit works — an audit you
skipped and an audit you did are otherwise indistinguishable.
