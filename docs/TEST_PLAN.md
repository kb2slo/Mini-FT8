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

**Run all three harnesses, not two.** `tx_e2e` is a separate CMake project and is easy to forget: a B22
include-path miss in it survived an entire branch of local `idf.py build` + `host_mock` runs and only
surfaced in CI. The commands below are the full local set.

| Harness | Command | Covers |
| --- | --- | --- |
| `host_mock` (20 binaries) | `make -C host_mock && host_mock/host_test*` | See table below. CI globs the binaries rather than listing them, so a new test runs as soon as the Makefile builds it |
| `tests/tx_e2e` | `cmake -S tests/tx_e2e -B tests/tx_e2e/build && cmake --build tests/tx_e2e/build -j4 && (cd tests/tx_e2e/build && ctest --output-on-failure)` | L1 encoder, TX state machine, poll timing, timer isolation, golden WAV RX decode, telemetry decode overflow, TA format — 7 tests |
| Firmware build | `idf.py build` | `main/` under `-Werror`; merged image; must be **warning-free** |
| Sidekick build | CI job **Sidekick (ESP32-C6)** | Companion firmware compiles and stages |
| README audit | CI job **README audit** | A PR removing a `UIMode` enumerator or changing a key binding under `main/` must touch `README.md` |

### `host_mock` coverage

| Binary | Covers |
| --- | --- |
| `host_test` | Autoseq engine: JSON-driven QSO, Field Day, beacon, reincarnation, deadlock, freetext scenarios |
| `host_test_unique_callsign` | Unique-callsign touch dedupe and promote |
| `host_test_beacon_cancel` | Beacon-off cancels a queued CQ |
| `host_test_adif_merge` | ADIF merge export, the logger's 10-minute dedupe window, and record formatting: byte-for-byte layout against a real field record, the omit rules for empty grid / unset reports / empty comment, `<tag:N>` lengths matching their values, and a round-trip back through `adif_parse()` |
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
| `host_test_datetime_field` | STATUS date/time editor: cursor movement over separators, digit overwrite, and strict range validation. Carries regression cases for the dates `mktime` used to silently roll over, plus an exhaustive sweep of every day in a leap and non-leap year |
| `host_test_screen_model` | Screen navigation: key to screen, R never toggling, the seven plain toggles, M/N/O sharing MENU across three pages, P cycling stats to log to RX, and `C` staying inert after B23 |
| `host_test_menu_model` | MENU layout arithmetic (page/key round-trip for all 18 rows), row identity and order, inline-edit character classes and filter, and the long-edit rules (per-kind case handling and the ignore-list cap). Carries regression cases for both defects B32 fixed |

### What automation cannot see

Named explicitly so nobody mistakes a green run for coverage. None of the following has a harness:

- **The UI rendering.** No test covers any `draw_*` function; what each screen actually *paints* is
  unverified. Screen *navigation* is covered by `host_test_screen_model`, and the MENU is now
  partly covered: `host_test_menu_model` pins layout, row order, and the edit filter, and
  `menu_assert_model_in_sync()` logs at startup if the label/action table drifts off the model. What stays
  uncovered is whether a row's *label* and *action* actually belong together — that pairing is still only
  verifiable by eye, which is why section 2 below checks label and effect together.
- **The main loop and slot state machine.** `app_task_core0`, `tx_tick`, `check_slot_boundary`.
- **Any field-only path**: USB host / UAC audio, CAT, CDC, display and SPI, GPS, DS3231, SD card, flash.
- **Timing.** Slot alignment, decode-window deadlines, TX start latency.

## Operator field checks

Real hardware. An agent proposes these and never marks them passed.

Standing rig: Cardputer ADV + QMX (or QMX+) over USB-C. The Cardputer's USB-C is **either** ESP serial/JTAG
**or** USB host for the radio, never both — live logs with the radio attached need the console UART on
**G4 (TX) / G5 (RX)**, and that path is off when `GNSS_LoRa:ON`.

Naming trap: the `D` screen is **Delete Files**, even though its enum is `UIMode::DEBUG`. The debug *log*
viewer is `P`, second page.

### 0. Smoke — do this first, it gates the rest

**Sync the clock before expecting any decode.** FT8 slots are 15 s and the decoder
needs UTC within roughly a second; an unsynced clock produces an empty R list that looks exactly like a dead
audio path. On a cold unit with no DS3231 backup, decodes will not appear no matter what else is right.

There is an ordering constraint here that is easy to trip over: **phone sync needs the radio unplugged from
USB-C** (BLE and USB host cannot both own that port), while **audio needs it plugged in**. So the clock step
comes first, with the radio disconnected.

| # | Do | Expect |
|---|---|---|
| 0.1 | Power on, radio **not** connected | Boots to the `R` screen, no crash loop |
| 0.2 | `S`, read the Time line suffix | ` R` = DS3231, ` G` = GPS, ` P` = phone. **No suffix means the clock is not from a trusted source** — continue to 0.3 |
| 0.3 | If unsynced, `H` then `1` ("Start sync"), pair from nRF Connect / LightBlue to `Mini-FT8-<call>` | Sync completes; `S` Time line now reads correct UTC with a ` P` suffix |
| 0.4 | Alternatives if no phone: `G` and wait for a GPS fix, or `S` `5` / `6` to set date and time by hand | Time line shows correct UTC (` G` for GPS; manual entry shows no suffix) |
| 0.5 | Confirm the time against a known-good clock | Within about a second of UTC |
| 0.6 | Connect the radio to USB-C | — |
| 0.7 | `S` then `2` | Audio starts; waterfall moves; status shows `Sync to QMX` |
| 0.8 | Return to `R`, wait 1–2 slots | Decodes appear; countdown bar animates in step with the slot |
| 0.9 | **If `S`→`2` ever fails, press it again before power-cycling** | It must be able to succeed on a retry. Before B36 one failed USB host install was terminal for the boot: `Audio start fail` repeated until a power cycle. The `D` log now names the reason — `USB host retry after …`, `USB host timeout (…)`, `USB host install: …`, `UAC already started` — instead of only `Audio start fail` |

If 0.7 fails, stop — everything below assumes audio. If 0.7 works but 0.8 shows nothing, **re-check 0.2**
before suspecting the audio or decode path; an unsynced clock is the more common cause and looks identical.

Once a DS3231 is fitted and set, it holds time across power cycles, so 0.2 should read ` R` on later boots
and steps 0.3–0.5 can be skipped.

### 1. Screen reachability

Press each key from the `R` screen and confirm the screen appears and its title is right. Then press the
same key again (or `R`) and confirm you return to `R`.

`R` · `T` · `B` · `M` · `N` · `O` · `Q` · `D` · `S` · `G` · `P` · `H`

`C` must do **nothing** — USB Drive was removed (B23). A visible reaction to `C` is a bug.

### 2. MENU — every item, every page

**This section is the one that matters most right now.** The whole menu was rewritten from a positional
list plus a page/key `if` chain into a single table (B32). Each item's text, action, and edit behaviour used
to be defined in three separate places; if any pairing got crossed in the rewrite, it shows up here as a row
whose *label* is right but whose *action* belongs to a different row. **Check the label and the effect
together, item by item — not just that something happened.**

For each row: press the key, confirm the effect, and confirm no *other* row changed.

#### MENU P1 (`M`)

| Key | Label reads | Press it and expect |
|---|---|---|
| `1` | `CQ Type:<type>` | Cycles CQ / SOTA / POTA / QRP / FD / FreeText, wrapping after 6 presses back to `CQ` |
| `2` | `Send FreeText` | Row flashes ~0.5 s; `D`-log shows `Queued: <text>`; the FreeText transmits next slot |
| `3` | `F:<text>` | Opens the long-edit screen for FreeText; typed letters appear **upper case**; `` ` `` cancels leaving the old value, Enter saves |
| `4` | `Call:<call>` | Inline edit; **typed letters appear uppercase**; Enter saves, `` ` `` cancels |
| `5` | `Grid:<grid>` | Inline edit; **uppercase**; accepts 4/6/8 char; a bad grid logs `Grid format: AA00/AA00aa/AA00aa00` and does not save |
| `6` | battery/sleep line | Enters Charge Mode |

#### MENU P2 (`N`)

| Key | Label reads | Press it and expect |
|---|---|---|
| `1` | `Offset:<src>` | Cycles Random / RX / Cursor, wrapping after 3 |
| `2` | `Fixed:<hz>` | Inline edit, **digits only** — letters must be rejected with no visible change. `▲``▼``◀``▶` step ±100/±10 and clamp to 200–3000. `` ` `` restores the value you started with |
| `3` | `Radio:<name>` | Cycles QMX / QDX. If audio was streaming and the backend differs, audio stops and `D`-log shows `Audio stop <radio>` |
| `4` | `IgnoreList:<prefixes>` | Long edit; **upper case**; space-separated prefixes; stops accepting at 64 characters |
| `5` | `C:<comment>` | Long edit; **keeps lower case, unlike FreeText and IgnoreList**; `/Radio` and `/Grid` macros expand in the displayed line |
| `6` | `Mode: FT8` or `FT4` | Toggles FT8/FT4 and appends `*` when it differs from the running mode. Reboot applies it |

#### MENU P3 (`O`)

| Key | Label reads | Press it and expect |
|---|---|---|
| `1` | `RxTxLog:ON/OFF` | Toggles and persists |
| `2` | `SkipTX1:ON/OFF` | Toggles and persists |
| `3` | `Band config` | Enters the BAND config screen |
| `4` | `GNSS_LoRa:ON/OFF` | Toggles; GPS and PORTA restart on the other pin set |
| `5` | copy-to-SD line | `Copied OK`, or `Missed [n]`, or a blocked message if TX/decode/streaming is active |
| `6` | `Max Retry:<n>` | Inline edit, **digits only**; `0` is accepted |

#### Menu paging and edit-mode guards

| # | Do | Expect |
|---|---|---|
| 2.1 | `M`, then `.` twice | P1 → P2 → P3; `;` walks back; no page 4 |
| 2.2 | `M` on P1, press `M` | Leaves to `R`. On P2 or P3, `M` returns to P1 first |
| 2.3 | `N` from `R`, then `N` again | Enters P2, then leaves to `R` |
| 2.4 | `O` from `R`, then `O` again | Enters P3, then leaves to `R` |
| 2.5 | Start editing Call (`M` `4`), then press `R`, `T`, `S` | **Screen must not switch** — mode keys are disabled during edit |
| 2.6 | While editing Call, press `` ` `` | Cancels the edit; original call intact; mode keys work again |
| 2.7 | Edit Call, press Enter | Saves; reboot and confirm it persisted in `Station.txt` |
| 2.8 | Edit Max Retry (`O` `6`), type letters | Letters rejected, **screen does not flicker or redraw** |

### 3. RX (`R`)

| # | Do | Expect |
|---|---|---|
| 3.1 | `1`–`6` on a decoded line | Line flashes; a reply is queued; `T` shows the new QSO |
| 3.2 | Tap a line while a TX is in flight | The in-flight TX is **not** replaced; the tap queues behind it |
| 3.3 | Tap while a QSO with that station is already active | `D`-log shows `QSO in progress`; no duplicate queued |
| 3.4 | `▲` / `▼` when line 1 or 6 is cyan | Pages the decode list |
| 3.5 | `` ` `` during a TX | TX stops; PTT drops; HUD clears |

### 4. TX queue (`T`)

| # | Do | Expect |
|---|---|---|
| 4.1 | `T` with ≥1 active QSO | Queue lists entries with state and parity colour |
| 4.2 | `1` | Rotates to the next same-parity entry |
| 4.3 | `2`–`6` | Drops that entry; **list redraws immediately** |
| 4.4 | `;` / `.` | Pages when more than 5 entries |
| 4.5 | `` ` `` | Cancels TX |

### 5. STATUS (`S`)

| # | Do | Expect |
|---|---|---|
| 5.1 | `1` | Cycles Beacon mode; applies on leaving STATUS |
| 5.2 | `2` | Starts audio and runs the CAT sync path |
| 5.3 | `3` | Steps to the next active band |
| 5.4 | `4` | Toggles Tune; radio keys and unkeys |
| 5.5 | `5` | Edits Date in place; digits only; Enter applies to the RTC |
| 5.6 | `6` | Edits Time in place. The Time line's source suffix is ` G` GPS, ` R` DS3231, ` P` phone, or blank for saved / ESP-RTC / manual |
| 5.7 | Enter `2026-02-30` and press Enter | `D`-log shows `Invalid date/time`; **date line unchanged**; clock not set |
| 5.8 | Enter `2026-13-45`, then `2026-00-00` | Both rejected the same way. Before B34 all three of these were silently accepted and rolled over (to 2026-03-02, 2027-02-14 and 2025-11-30) |
| 5.9 | Enter `2024-02-29`, then `2025-02-29` | Leap day accepted in 2024, rejected in 2025 |
| 5.10 | While editing the date, hold `/` to the end then `,` back | Cursor steps over the `-` separators in both directions and stops at each end without sticking |
| 5.11 | Enter `24:00:00` as the time | Rejected; time line unchanged |
| 5.12 | Enter a **valid** date and time and press Enter | Accepted and applied. B34 changed this path, so confirm a good setting still reaches the clock — not only that bad ones are refused |
| 5.13 | Power-cycle after 5.12, with a DS3231 fitted | Time survives; suffix reads ` R` |

### 6. BAND (`B`), QSO (`Q`), Delete (`D`), GPS (`G`), PERF (`P`), BT (`H`)

| # | Do | Expect |
|---|---|---|
| 6.1 | `B`, `1`–`6` | Selects a band slot; edit kHz; persists |
| 6.2 | `Q`, `1`–`6` | Opens that ADIF file; `◀` `▶` switch Default / SNR columns |
| 6.3 | `D` | Lists files. `1`–`6` deletes **immediately, no confirmation** |
| 6.4 | `D` on today's active log | Row shows `LOCK <file>`; the file is **not** deleted |
| 6.5 | `G` | Live GPS telemetry: source, fix, satellites, UTC, grid |
| 6.6 | `P` | Performance stats. Press `P` again for the debug log, `P` again returns to `R` |
| 6.7 | `H` | Shows `1: Start sync`, and `2: Flash Sidekick` when a Nano is present. `1` runs the phone time sync (radio unplugged from USB-C first — see 0.3); `2` field-flashes the sidekick |

### 7. End-to-end

| # | Do | Expect |
|---|---|---|
| 7.1 | Complete one full QSO | Sequence runs to signoff without manual help |
| 7.2 | `Q` | The QSO is in today's `.adi` and readable |
| 7.5 | Work a station that never sends a grid, then read the `.adi` | The record has **no** `gridsquare` field at all — not `<gridsquare:0>`. Two such QSOs appeared in the 2026-09-08 log (N2FSM, W4MAA) |
| 7.3 | `O` `5` | Copy to SD reports `Copied OK`; files land on the card |
| 7.4 | Reboot | Call, grid, band, and every menu toggle survived |

## Pending

Nothing owed. `refactor/main-subdirs` was field-tested on 2026-09-08 and merged: sections 0–7 walked,
including every MENU item on every page in section 2, plus a 3.6 h unattended beacon run (544 TX slots
spotted by 1,195 receivers, no gap over 5 min) and a 22-QSO log on 20m.

Rows do not accumulate across merges — a newly merged branch starts this section empty. Add one the moment
a change touches a field-only path, naming the change and the exact check.

### Standing watch — B36, unproven

One item did **not** clear, and cannot be cleared by a passing run. B36 made a failed USB host install
retryable instead of terminal for the boot, but the race is not reproducible on the bench, so a clean start
proves only that the retry path was never needed. It stays unproven until a failure recovers.

**If `S`→`2` ever fails, press it again before power-cycling.** It should be able to succeed. The `D` log now
names the reason — `USB host retry after …`, `USB host timeout (…)`, `USB host install: …`,
`UAC already started` — instead of only `Audio start fail`. That line is the evidence; capture it.
