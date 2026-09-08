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

Naming trap: the `D` screen is **Delete Files**, even though its enum is `UIMode::DEBUG`. The debug *log*
viewer is `P`, second page.

### 0. Smoke — do this first, it gates the rest

| # | Do | Expect |
|---|---|---|
| 0.1 | Power on | Boots to the `R` screen, no crash loop |
| 0.2 | Wait 1–2 slots | Decodes appear in the R list |
| 0.3 | `S` then `2` | Audio starts; waterfall moves; status shows `Sync to QMX` |
| 0.4 | Return to `R` | Countdown bar animates in step with the slot |

If 0.3 fails, stop — everything below assumes audio.

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
| `3` | `F:<text>` | Opens the long-edit screen for FreeText; `` ` `` cancels leaving the old value, Enter saves |
| `4` | `Call:<call>` | Inline edit; **typed letters appear uppercase**; Enter saves, `` ` `` cancels |
| `5` | `Grid:<grid>` | Inline edit; **uppercase**; accepts 4/6/8 char; a bad grid logs `Grid format: AA00/AA00aa/AA00aa00` and does not save |
| `6` | battery/sleep line | Enters Charge Mode |

#### MENU P2 (`N`)

| Key | Label reads | Press it and expect |
|---|---|---|
| `1` | `Offset:<src>` | Cycles Random / RX / Cursor, wrapping after 3 |
| `2` | `Fixed:<hz>` | Inline edit, **digits only** — letters must be rejected with no visible change. `▲``▼``◀``▶` step ±100/±10 and clamp to 200–3000. `` ` `` restores the value you started with |
| `3` | `Radio:<name>` | Cycles QMX / QDX. If audio was streaming and the backend differs, audio stops and `D`-log shows `Audio stop <radio>` |
| `4` | `IgnoreList:<prefixes>` | Long edit; space-separated prefixes |
| `5` | `C:<comment>` | Long edit; `/Radio` and `/Grid` macros expand in the displayed line |
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
| 5.6 | `6` | Edits Time in place; time line shows `G` for GPS or `R` for DS3231 source |
| 5.7 | Enter an invalid date | `D`-log shows `Invalid date/time`; clock unchanged |

### 6. BAND (`B`), QSO (`Q`), Delete (`D`), GPS (`G`), PERF (`P`), BT (`H`)

| # | Do | Expect |
|---|---|---|
| 6.1 | `B`, `1`–`6` | Selects a band slot; edit kHz; persists |
| 6.2 | `Q`, `1`–`6` | Opens that ADIF file; `◀` `▶` switch Default / SNR columns |
| 6.3 | `D` | Lists files. `1`–`6` deletes **immediately, no confirmation** |
| 6.4 | `D` on today's active log | Row shows `LOCK <file>`; the file is **not** deleted |
| 6.5 | `G` | Live GPS telemetry: source, fix, satellites, UTC, grid |
| 6.6 | `P` | Performance stats. Press `P` again for the debug log, `P` again returns to `R` |
| 6.7 | `H` then `1` | BLE time sync starts (radio unplugged from USB-C first) |

### 7. End-to-end

| # | Do | Expect |
|---|---|---|
| 7.1 | Complete one full QSO | Sequence runs to signoff without manual help |
| 7.2 | `Q` | The QSO is in today's `.adi` and readable |
| 7.3 | `O` `5` | Copy to SD reports `Copied OK`; files land on the card |
| 7.4 | Reboot | Call, grid, band, and every menu toggle survived |

## Pending — owed on `refactor/main-subdirs`

This branch is taking cleanup aggressively on the model that one significant regression pass happens before
merge, rather than a field check per commit. **These are owed before this branch merges to `main`.**

Most commits on the branch removed code that was provably never in the image (verified by linker map plus
`nm` on the ELF, with a byte-identical `mini_ft8.bin` as the check). Those need no field test. Only the
commits below changed live code.

| From | Check | Status |
| --- | --- | --- |
| B28 (ENABLE_FT4) | MENU P1 `6` toggles `Mode: FT8` / `FT4` with `*`; survives `Station.txt` save + reboot | owed |
| dead build flags | UART screen mirror still dumps the screen on G4/G5 key injection | owed |
| B29 (host protocol) | MENU P3 `5` Copy files to SD → `Copied OK`; log writes normal during TX/decode | owed |
| audio dispatch | `S` → `2` QMX audio starts and stops; decode works; radio change stops audio | owed |
| waterfall buffer | Waterfall still renders normally while streaming | owed |
| B30 (core_api removal) | R-tap a decode to reply; backtick cancel during TX; drop a QSO from the `T` list | owed |
| B31 (extern audit) | none — linkage and visibility only, byte-identical binary | n/a |
| B32 (menu table) | **All of section 2**, every item on every page. Highest-risk change on the branch: the menu was rewritten from three disconnected definitions per item into one table | owed |
| all | Sections 0, 1, 3–7 — one full pass | owed |

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
