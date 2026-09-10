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

**Where the on-screen log lives: `P`, then `.`** — the PERF screen's second page. `;` returns to the stats
page, `.` pages further down the log. Every "the log shows …" expectation below means that screen.

Naming trap, and it has already cost bench time: the `D` screen is **Delete Files**, not a debug log, even
though its enum is `UIMode::DEBUG`. Pressing `1`–`6` there **deletes a file immediately, with no
confirmation**. Do not go looking for log output on `D`.

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
| 0.9 | **If `S`→`2` ever fails, press it again before power-cycling** | It must be able to succeed on a retry. Before B36 one failed USB host install was terminal for the boot: `Audio start fail` repeated until a power cycle. The log (`P` then `.`) now names the reason — `USB host retry after …`, `USB host timeout (…)`, `USB host install: …`, `UAC already started` — instead of only `Audio start fail` |

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
| `2` | `Send FreeText` | Row flashes ~0.5 s; log (`P` then `.`) shows `Queued: <text>`; the FreeText transmits next slot |
| `3` | `F:<text>` | Opens the long-edit screen for FreeText; typed letters appear **upper case**; `` ` `` cancels leaving the old value, Enter saves |
| `4` | `Call:<call>` | Inline edit; **typed letters appear uppercase**; Enter saves, `` ` `` cancels |
| `5` | `Grid:<grid>` | Inline edit; **uppercase**; accepts 4/6/8 char; a bad grid logs `Grid format: AA00/AA00aa/AA00aa00` and does not save |
| `6` | battery/sleep line | Enters Charge Mode |

#### MENU P2 (`N`)

| Key | Label reads | Press it and expect |
|---|---|---|
| `1` | `Offset:<src>` | Cycles Random / RX / Cursor, wrapping after 3 |
| `2` | `Fixed:<hz>` | Inline edit, **digits only** — letters must be rejected with no visible change. `▲``▼``◀``▶` step ±100/±10 and clamp to 200–3000. `` ` `` restores the value you started with |
| `3` | `Radio:<name>` | Cycles QMX / QDX. If audio was streaming and the backend differs, audio stops and log (`P` then `.`) shows `Audio stop <radio>` |
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
| 3.3 | Tap while a QSO with that station is already active | log (`P` then `.`) shows `QSO in progress`; no duplicate queued |
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
| 5.7 | Enter `2026-02-30` and press Enter | log (`P` then `.`) shows `Invalid date/time`; **date line unchanged**; clock not set |
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

## Sidekick (AtomS3 Lite)

A second device with its own build and its own USB-C. `sidekick/` is a separate `idf.py` project — ESP-IDF
locks target and sdkconfig per project, so it cannot share the ADV's configure even though both are
`esp32s3` now.

### S0. Back up the part before flashing it, once per physical device

Irreversible otherwise: a factory AtomS3 Lite ships with M5Stack firmware, and nothing else restores it.

```bash
esptool.py --chip esp32s3 -b 460800 \
    read_flash 0x0 0x800000 sidekick/stock_backup/atoms3_stock_backup.bin
```

`0x800000`, not the Nano's `0x400000` — a 4 MB read of an 8 MB part restores garbage over the upper half.
`stock_backup/` is gitignored on purpose: a snapshot of one specific unit, not a repo asset.

### S1. Build and flash

Commands below carry no `-p` port: `idf.py` and `esptool.py` both autodetect,
and a placeholder like `/dev/cu.usbmodemXXXX` turns every line into one you have
to edit before it runs. Add `-p` only to disambiguate two boards plugged in at
once.

```bash
cd sidekick && idf.py build && idf.py flash monitor
```

| # | Expect |
|---|---|
| S1.1 | Builds for `esp32s3` with no `set-target` step |
| S1.2 | Boot banner `Mini-FT8 sidekick booting (IDF …)` over the AtomS3's own USB-C |
| S1.3 | `alive: N` every ~5 s, counting up |

### S2. Grove pin mapping — the one that fails silently

`main.c` uses `GPIO_NUM_1` / `GPIO_NUM_2` for the PORTA TX/RX pair, carried over from the NanoC6. The
AtomS3 Lite's silkscreen was checked 2026-09-08 and matches (`G`, `5V`, `G2`, `G1`), and M5Stack labels Grove
pins with GPIO numbers, so the constants are *expected* to be right — but expected is not verified, and a
wrong mapping fails silently: the beacon transmits into nowhere and it looks exactly like "the companion
link isn't implemented yet".

Connect the sidekick's Grove port to the ADV's PORTA with a straight-through cable. Nothing else on PORTA:
role arbitration locks to GPS *or* companion, so an attached GPS wins and masks the result.

| # | Do | Expect |
|---|---|---|
| S2.1 | Sidekick powered and beaconing, then open the log on the ADV — **`P` then `.`** | Within a few seconds, **one** of: `OK <version>`, or the pair `X R:<remote>` / `X L:<local>` |
| S2.2 | Read which one appeared | **Either proves the pins are right.** `OK` means the two builds are the same commit; `X R:`/`X L:` means the link works and the builds differ, which is expected whenever the ADV is running an older image than the sidekick you just flashed |
| S2.3 | If **no** line appears at all | The Grove pins are wrong, or the cable is. Check the AtomS3 Lite's Grove GPIO numbers against `PORTA_TX_PIN`/`PORTA_RX_PIN` before suspecting anything else |
| S2.4 | Confirm the ADV did not instead log `PORTA: GPS` | That means it locked onto the GPS role — something else is on the bus, or the beacon is being misread |

### S2b. Re-flash after the OTA partition change — one-time, passed 2026-09-09

Kept for the record, and for any part that has not yet crossed the partition change. A device already
running an OTA-layout image does not need it again.

The sidekick's flash map moved (RFC 0001 §5.2d): `factory` at `0x10000` became `ota_0` at `0x20000`, plus
`otadata`. A device flashed before that change is running the old layout and must be reflashed over its own
USB-C once.

| # | Do | Expect |
|---|---|---|
| S2b.1 | `cd sidekick && idf.py flash monitor` | Boots and `alive:` counts, as S1 |
| S2b.2 | Re-run S2 (Grove to PORTA, log on `P` then `.`) | Beacon still arrives — the partition move must not disturb the companion link |
| S2b.3 | `idf.py partition-table` | Shows `ota_0` and `ota_1` at 3 MB each, `nvs` still at `0x9000` |

### S2c. WiFi provisioning (SoftAP)

New in I3d. The sidekick raises its own access point when it has no working
credentials, serves a one-page form, stores what you enter, and restarts to join.

| # | Do | Expect |
|---|---|---|
| S2c.1 | Flash and watch the monitor on a device with no stored credentials | `No stored credentials`, then `Provisioning AP '<name>' up — open http://192.168.4.1/` |
| S2c.1a | **iPhone hotspot?** Settings → Personal Hotspot → **Maximize Compatibility ON** | Without it recent iPhones run the hotspot on 5 GHz, which this radio cannot see or join — it will be absent from the scan and fail manual entry |
| S2c.2 | On a phone, look for an open network `MiniFT8-SK-XXXX` | Appears. The suffix is the last two MAC bytes, so two sidekicks on one bench are distinguishable |
| S2c.2a | Join the AP and **wait** | The sign-in sheet should open by itself. iOS/Android read the portal URI from DHCP (RFC 8910) or fall back to a probe that DNS sends here |
| S2c.2b | If it does not appear, browse to any address at all — `http://example.com` | Lands on the form. Every A query resolves to the sidekick and every unknown path 302s to `/` |
| S2c.3 | Join it and open `http://192.168.4.1/` | A form with a **Network dropdown listing nearby WiFi**, a "or type one not listed" box, and Password. Some phones show a captive-portal prompt; if not, type the address |
| S2c.3a | Check the dropdown | Your network is there. Duplicates collapsed (same SSID on 2.4/5 GHz or a mesh shows once). Open networks are marked `(open)` |
| S2c.3b | Pick from the dropdown **and** leave the manual box empty | Joins the selected network |
| S2c.3c | Look at the form before touching the dropdown | **No free-text box.** It used to sit there permanently, asking a second question the operator has only one answer to |
| S2c.3c1 | Pick **Other…**, the last entry in the list | A `Network name` box appears and takes focus. Type a name and it joins that network — the path for a hidden SSID, or one out of range right now |
| S2c.3c2 | Pick **Other…**, leave the box empty, submit | A `Not saved` page: "Network required — pick one from the list, or choose **Other…** and type a name", with a working `Back to the form` link. Check the dash and ellipsis render — error pages do not go through the normal response path and were mangled once already. No join attempt: the sentinel must never reach the driver as an SSID |
| S2c.3c3 | Pick **Other…**, then switch back to a listed network | The box disappears and the listed network is what gets joined |
| S2c.3d | Turn a hotspot **on after** the sidekick booted, disconnect from the AP, wait ~15 s, reconnect | It appears without pressing anything. The cache refreshes every 10 s while no client is associated — field-hit on the first run, where the list predated the hotspot |
| S2c.3e | Click "Scan again" | The list refreshes. Your phone may briefly drop; the scan takes the radio off the AP's channel. Warned in the page text |
| S2c.3f | Read the intro line: "**2.4 GHz only** — this radio cannot see…" | A real em dash, not `â€”`. The served pages declare `charset=utf-8`; without it the browser guesses Latin-1. Our punctuation is the visible symptom, but the case that matters is an SSID with non-ASCII characters rendering into the dropdown |
| S2c.4 | Submit your WiFi details | A "Saved" page warning the AP will disappear, then the device restarts |
| S2c.5 | Watch the monitor | `Joining '<ssid>'...` then `Online as <ip>`. The `alive:` line now reads `wifi=up` |
| S2c.6 | Power-cycle | Rejoins from NVS without the AP appearing — credentials persisted |
| S2c.6a | `esptool.py --chip esp32s3 erase_region 0x9000 0x6000` (esptool, not `idf.py`; close the monitor first), then reset | Comes up in the **provisioning AP**, not reconnected. Before I3g the WiFi driver kept its own credential copy in `nvs.net80211` and the device rejoined anyway |
| S2c.7 | Throughout all of the above, check the beacon on the ADV (`P` then `.`) | Still arriving. WiFi must not disturb the companion link |
| S2c.8 | Enter a **wrong** password at S2c.4 | Five attempts logged **with a reason** (`wrong password`), then `falling back to the AP`. The AP must come back **without crashing** — that path panicked before I3f — and the page shows "Last attempt failed: wrong password" |
| S2c.8a | Enter a network that is out of range or 5 GHz | Same fallback, reason reads `network not found (5 GHz? this radio is 2.4 GHz only)` |
| S2c.8b | After the fallback AP is up, leave it a minute | No repeating join attempts in the log. The AP runs in APSTA to scan, and its station side must stay idle rather than re-attacking the failed network |
| S2c.9 | With a password containing a space or `%` | Round-trips correctly — the form is URL-encoded and the field is decoded before storage |

### S2d. Station mode: the status page, mDNS, and Forget WiFi

Until now a joined sidekick served nothing. The HTTP server ran only while the provisioning AP was up, so
once the device was on your network the only way to see what it was doing was a serial console — precisely
the console the finished product will not have attached. mDNS and the station-mode server are one feature
for that reason: the name would otherwise resolve to a port with nothing behind it.

**Preconditions:** joined (S2c.5 passed), and the laptop or phone you browse from on the **same subnet**.
mDNS does not cross subnets, and many guest networks isolate clients from each other. If `.local` fails,
rule that out before suspecting the firmware — S2d.5 is the control.

| # | Do | Expect |
|---|---|---|
| S2d.1 | Watch the monitor at the moment of join | `mDNS: http://minift8.local/` after `Online as <ip>` |
| S2d.2 | From a laptop on the same network: `ping -c 3 minift8.local` | Resolves to the IP reported in S2c.5 |
| S2d.3 | Browse `http://minift8.local/` | Status page. Network = your SSID, Address = that IP, Firmware = the running version, Uptime in seconds |
| S2d.4 | Wait ~30 s and reload | Uptime advanced. The page is generated per request, not cached by the phone |
| S2d.5 | Browse `http://<ip>/` directly | The same page. This separates "the server is down" from "the name did not resolve" — do it before blaming mDNS |
| S2d.6 | `dns-sd -B _http._tcp` on macOS, or any network browser | `minift8` appears as an HTTP service. Hostname resolution alone would not list it; this is the `_http._tcp` advertisement |
| S2d.7 | **Before** joining, while on the provisioning AP, browse `http://minift8.local/` | The provisioning **form**, not the status page. mDNS is advertised in both modes |
| S2d.7a | Immediately after the join, if `minift8.local` hangs from the phone or laptop you used on the AP | **Expected, and not a firmware fault.** The same hostname pointed at `192.168.4.1` while the AP was up, and mDNS records carry a 120 s TTL — a client that resolved it on the AP keeps that answer until it expires. Try the station IP directly to confirm, then wait ~2 min, toggle WiFi, or on macOS `sudo killall -HUP mDNSResponder` |
| S2d.8 | Press **Forget WiFi** and accept the confirm dialog | A "Forgotten" page; the monitor logs `Credentials forgotten — restarting into provisioning`; the `MiniFT8-SK-XXXX` AP is back within ~10 s |
| S2d.9 | Power-cycle after the forget | Still provisioning. Nothing rejoined — this is the first operator-reachable control that depends on I3g's single credential store, and the erase test in S2c.6a is the same guarantee reached a harder way |
| S2d.10 | Repeat S2d.8 but **cancel** the dialog | Nothing happens; still online. The confirm is the only thing standing between a stray tap and a re-provision |
| S2d.11 | Check the ADV beacon (`P` then `.`) at the end | Still arriving |

Not testable with one device, so it is a known limitation rather than a row: the hostname is fixed, so a
second sidekick on the same network loses the name and mDNS silently renames it `minift8-2.local`. Folded
into I19 when there is a reason to care.

### S3. Field-flash is expected to refuse

Not a regression. The payload is now an S3 image and the family gate still requires a C6, so both gates
disagree by design until the S3 identity guard lands (I3, deferred).

| # | Do | Expect |
|---|---|---|
| S3.1 | Plug any Espressif device into the ADV's USB-C and accept the install prompt | Refuses. The log (`P` then `.`) shows `not C6 (…)` or `fw9!=hw13` |
| S3.2 | Plug in a **real NanoC6** | Refuses with `fw9!=hw13` — the payload/target gate. Before that gate this combination would have written an S3 image onto the C6 and bricked it |

## Bench run — everything owed, in the order to do it

The sections above are organised by subject. This is the walk: one pass, top to bottom, covering everything
that has never been verified on hardware. Roughly an hour. Phase order is not arbitrary — several checks
destroy the state a later one needs, and two of them are masked if run after the step that follows.

**The firmware under test is a working tree, not a commit.** The version on the status page will read
`-dirty`. That is expected and is itself a small check that you are running what you just built.

**Setup:** ADV + QMX over USB-C · AtomS3 Lite on its own USB-C for the monitor · Grove cable PORTA→PORTA
with the ADV's 5 V out selected · a phone · a laptop on the same WiFi as the phone. If the network is an
iPhone hotspot, turn **Maximize Compatibility ON** now (S2c.1a) — without it the hotspot is 5 GHz and this
radio cannot see it at all.

| Phase | What | Rows | Why here |
|---|---|---|---|
| 1 | `cd sidekick && idf.py build flash monitor` | S1.1–S1.3 | Everything else runs against this image |
| 2 | Clean slate — **`esptool.py`, not `idf.py`**, and close the monitor first (`Ctrl+]`) or the port is busy: `esptool.py --chip esp32s3 erase_region 0x9000 0x6000`, then reset | S2c.6a, S2c.1 | Comes up **provisioning**. This is the retest of the fix for the failure you hit in the field — before I3g the driver kept a second credential copy in `nvs.net80211` and the device rejoined anyway. It is also the only way to get a genuine no-credentials boot for phase 3 |
| 3 | Captive portal | S2c.2, S2c.2a, S2c.2b | **Never once seen working.** Do it before anything else touches the AP: the sign-in sheet only auto-opens on a *fresh* association, so first tell the phone to forget `MiniFT8-SK-XXXX` if it remembers it from an earlier run |
| 4 | The form and the scan list | S2c.3, 3a, 3b, 3c, **3d**, 3e | Run **3d (stale cache) before 3e (Scan again)**. Pressing Scan again refreshes the list and hides the exact defect 3d exists to catch — the one that made you click it last time |
| 5 | Join, persist, odd characters | S2c.4, 5, 6, 9 | S2c.9 needs a password with a space or `%`; if your real network has neither, make the hotspot's password one for this run |
| 6 | Station mode | S2d, all rows | New code, none of it seen on hardware. S2d.5 before blaming mDNS |
| 7 | Failure paths | S2c.8, 8a, 8b | Destroys the working credentials, so it goes after 6. These are the rows most likely to be skipped and most likely to be wrong: 8 is the crash that I3f fixed, and 8b is the APSTA station side re-attacking a network it already failed |
| 8 | Companion link | S2c.7, S2d.11 | Not a phase so much as a habit: glance at `P` then `.` on the ADV at each phase boundary. A WiFi change that silently kills the beacon is the failure this catches, and it will not announce itself |
| 9 | Field-flash refusal | S3.1 | S3.2 needs a real NanoC6. If it is not to hand it stays owed — do not mark the gate verified on S3.1 alone |
| 10 | Restore to stock — **only if you want the unit back** | `tools/restore_sidekick_stock.sh` | Untested since the AtomS3 rewrite, and the one irreversible step here: the 8 MB backup in `sidekick/stock_backup/` is the only copy this unit has. Re-flash the sidekick image afterwards to carry on |

Not part of the walk, because nothing you can do triggers it: **B36** below. And CI needs no bench time —
it is green on `main` at `f0c79d3`, including the `sidekick-firmware` job that was failing.

## Pending

`refactor/main-subdirs` was field-tested on 2026-09-08 and merged: sections 0–7 walked,
including every MENU item on every page in section 2, plus a 3.6 h unattended beacon run (544 TX slots
spotted by 1,195 receivers, no gap over 5 min) and a 22-QSO log on 20m.

Rows do not accumulate across merges — a newly merged branch starts this section empty. Add one the moment
a change touches a field-only path, naming the change and the exact check.

| From | Check | Status |
| --- | --- | --- |
| I3a (sidekick retarget) | S0–S2, S2b | **passed 2026-09-09** — `X R: 181cbe0-dirty L: c0e52ec` on the ADV: a validated beacon frame, so the Grove pins, UART1 on S3, the framing and the version compare all work. The mismatch was expected (different builds). S2b (reflash after the OTA partition change, beacon re-checked) passed 2026-09-09 |
| I3d–I3h (WiFi provisioning) | S2c, all rows | **owed.** Partially exercised on 2026-09-09 but not against current firmware: the AP came up, the scan found networks, and a join succeeded — but the join that crashed (I3f), the credential erase that failed to stick (I3g) and the stale scan list (I3h) were all fixed *after* that session, and the captive portal has never been seen working |
| mDNS + station-mode server | S2d, all rows | **owed** — no part of it has run on hardware |
| I3a (payload gate) | S3.2 if a real NanoC6 is to hand — the refusal is the whole point of the gate | owed |
| B46 (restore script) | Phase 10 | owed, and optional. Rewritten for `esp32s3` / `0x800000` and never run; the failure mode is losing the unit's only stock image |

### Standing watch — B36, unproven

One item did **not** clear, and cannot be cleared by a passing run. B36 made a failed USB host install
retryable instead of terminal for the boot, but the race is not reproducible on the bench, so a clean start
proves only that the retry path was never needed. It stays unproven until a failure recovers.

**If `S`→`2` ever fails, press it again before power-cycling.** It should be able to succeed. The on-screen
log (`P` then `.`) now names the reason — `USB host retry after …`, `USB host timeout (…)`, `USB host install: …`,
`UAC already started` — instead of only `Audio start fail`. That line is the evidence; capture it.
