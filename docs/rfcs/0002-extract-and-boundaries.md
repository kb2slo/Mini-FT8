# RFC 0002: Extract, style, and boundaries

* **Status:** Done. Amended 2026-09-04 with §5.1 (module placement: `main/` file vs `components/` directory), which the original campaign left unstated.
* **Author:** Jeff Kalikstein, KB2SLO
* **Covers:** extract / style / `ft8_lib` / radio contract (pre-I5). Roadmap Done. §2 was Done-when. Remainder of §7 contributor test is Backlog B14. Was I11 / B3 / B9.
* **Does not cover:** I3 companion, I6 multi-mode rename, I8/I9/I10 QMX product features

## 1. Why

`main.cpp` is the product: slot loop, UI, Station, ADIF, CAT, audio, and radio policy in one file. That blocks three things we actually want:

1. Safe change — new logic without a 6k-line merge and without untested behavior.
2. Upstream `ft8_lib` — ingest Karlis’s updates and send clean patches back.
3. Other radios — QMX/QMX+ must not be assumed in the core, so a maintainer can add a radio without editing the slot loop.

This RFC is the campaign plan and the Done-when. `docs/STYLE.md` is the short coding standard. `docs/ROADMAP.md` pointed here while this was Now. Chat is intake; those files are truth.

## 2. Outcomes (done when)

1. **Written standard.** `docs/STYLE.md` is the coding rule for *our* C/C++. New and extracted code follows it. Vendored `M5*` / `ft8_lib` do not.
2. **Module ratchet.** New behavior does not land as more `main.cpp`. It gets a named unit, a header that is the API, and a host test if it is parse / format / policy. `main.cpp` may only shrink.
3. **Vendor boundary.** `ft8_lib` is a pinned upstream (SHA) plus Mini-FT8 wrappers. Protocol patches are rebaseable commits on our fork. Bump path is 3-way merge + `tx_e2e` golden RX/encode. Karlis’s files look like Karlis’s files.
4. **Radio boundary.** Core talks `radio_control` **ops + capabilities**. QMX CAT, TX power/SWR, `MD8`, `PS0;`, and UAC profile live in the QMX backend. `main.cpp` does not `if (QMX)`. QMX and QMX+ are one backend until CAT differs.
5. **Host tests stay green.** Field-only paths (USB/CDC/CAT, UAC timing/DRAM, display/SPI, full-slot audio) are unchanged unless a dedicated follow-up says otherwise.

## 3. Non-goals

- Rewrite in Rust. Parked. Companion (I3) or a greenfield `miniFTx` (I6) may choose Rust later; this firmware does not.
- Repo-wide clang-format / astyle / rename of `main.cpp`.
- Class hierarchies for Station, ADIF, or decode sort.
- New radios (FTX-1, FT-817) or QMX product features (I8/I9/I10) in this campaign. Those wait until the radio object can refuse them.
- Mergeability with `upstream` (`wcheng95/Mini-FT8`).
- Making KH1/QDX “field proven.” They keep compiling. Only QMX/QMX+ is hardware we have.

## 4. Coding standard

The living text is [STYLE.md](../STYLE.md). Baseline is ESP-IDF’s guide where it matches this tree. We do not adopt Google function naming or a full LLVM restyle.

## 5. Structure rules

See [STYLE.md](../STYLE.md) (structure section). Summary:

- File size is a ratchet (~500 lines for new files). `main.cpp` may only shrink.
- Require a module, not a class. Inheritance only for a closed set of variants (radio backends, later modes).
- Radio keeps and widens `radio_control_ops_t`; no `class Radio`.
- No new logic in `main.cpp`. Dead code goes with the extract. Fixes that belong in an extract (B5) go with it; unrelated fixes stay out.
- **Module placement** (added 2026-09-04, see §5.1): plain `.cpp`/`.h` in `main/` by default; `components/<name>/` only when another component's `REQUIRES`/`PRIV_REQUIRES` names it, or when more than one `idf.py` project compiles it.

### 5.1 Module placement, corrected (2026-09-04)

This campaign never wrote down *when* an extract becomes a `components/<name>/` directory versus a plain `.cpp`/`.h` pair in `main/`, and the slices below went both ways under the same accepted standard: `adif` (D/E), `station` (D), `qso_browse` (H), and later `band_config` and `usb_c_presence` became components, while `decode_sort.h` (F) and `radio_profile.h` (B14 groundwork) landed as plain headers in `main/` between those batches. Host-testability was not the discriminator — `host_mock/Makefile` compiles by path and already carries `-I../main`, and it host-tests `decode_sort`, `radio_profile`, and `power_hysteresis` (which lives *inside* `components/board_cardputer_adv/`, not its own component) exactly as it host-tests the standalone components.

The rule now in [STYLE.md](../STYLE.md) has two prongs: **the build forces it** (another component names it in `REQUIRES`/`PRIV_REQUIRES`, or more than one `idf.py` project compiles it), **or it is a boundary we deliberately hold**, named on a closed list with its reason. Everything else is a plain file in `main/`.

A first pass demoted only `station`, `band_config`, and `usb_c_presence` — the three nothing depended on — and left the rest as an open question, because `adif` and `qso_browse` turned out to be held by `storage_service` and `file_list` respectively. That question is now closed: the cascade was followed all the way down in the same branch.

**Demoted to `main/` (10 modules, 23 files):**

| Module | Was held by | Notes |
|---|---|---|
| `station` (+ `station_save_queue`) | — | nothing depended on it |
| `band_config` | — | nothing depended on it |
| `usb_c_presence` (+ `_host`) | — | `main` gained `usb` |
| `storage_service` | — | `--wrap=tud_msc_inquiry_cb` link option moved to `main/CMakeLists.txt` (`LINK_OPTIONS` is a global build property, so it works identically); its `idf_component.yml` dependency on `espressif/esp_tinyusb` moved into `main/idf_component.yml` |
| `file_list` (+ `file_list_queue`) | — | released `qso_browse` |
| `cts_ble` | — | `main` gained `bt`, `nvs_flash`; released `cts_time` |
| `external_rtc` | — | `main` gained `esp_driver_i2c`; `board_cardputer_adv` stays a component, so `main`'s existing `REQUIRES` still covers it |
| `adif` | `storage_service` | released once its holder moved |
| `qso_browse` | `file_list` | released once its holder moved |
| `cts_time` | `cts_ble` | released once its holder moved |

**Kept (7), each with a stated reason:**

| Component | Prong | Reason |
|---|---|---|
| `M5Cardputer`, `M5GFX`, `M5Unified`, `ft8_lib` | 2 | Vendored. §6 vendor boundary. |
| `board_cardputer_adv` | **1** | `test_apps/cardputer_adv_audio_keyboard` is a second `idf.py` project that requires it. Also the I24 board seam. |
| `ui` | 2 | The display seam I24 needs for a headless host; 4200+ lines, 8 files. |
| `nano_flasher` | 2 | `EMBED_FILES` + generated header + conditional defines: payload packaging, not source layout. |

`components/` went from 17 directories to 7. Every survivor is now either mechanically forced or on the named list — there is no longer a component whose existence traces back to nothing more than which day it was extracted.

## 6. Vendor boundary (`ft8_lib` first)

Third-party source we did not write:

- Pin is the git submodule `components/ft8_lib/vendor` → [kb2slo/ft8_lib](https://github.com/kb2slo/ft8_lib) (fork of [wcheng95/ft8_lib](https://github.com/wcheng95/ft8_lib) / [kgoba/ft8_lib](https://github.com/kgoba/ft8_lib)). Clone Mini-FT8 with `--recurse-submodules`.
- Mini-FT8 wrappers only, in `components/ft8_lib/` (not in the submodule): `common/fft_wrapper`, `common/monitor` (static BSS arenas), IDF `CMakeLists.txt`, `common/stpcpy_compat` (Windows host only). Do not compile `vendor/common/monitor.c`.
- Protocol patches live as rebaseable commits on `kb2slo/ft8_lib`: Wei’s Field Day (`d8a41e6`, `bb3d94d`, also [kgoba#54](https://github.com/kgoba/ft8_lib/pull/54)) and DXpedition type 0.1 (`5d095f4`). Nonstd is already upstream (`9fec6ca`). Do not push DXpedition onto Wei’s `master` or onto #54.
- Contribute back only Karlis-shaped fixes (correctness, protocol). Never “make it compile on ESP32-S3.”
- Do not clang-format or re-indent that tree.
- **Bump:**
  1. `git -C components/ft8_lib/vendor fetch origin`
  2. `git -C components/ft8_lib/vendor fetch https://github.com/kgoba/ft8_lib.git master`
  3. 3-way merge kgoba into the fork (`git -C components/ft8_lib/vendor merge FETCH_HEAD`). Keep protocol commits rebaseable; do not fold in ESP glue.
  4. Push the fork, then move the submodule pin (`git add components/ft8_lib/vendor`).
  5. `tx_e2e` golden RX/encode must be green before the pin moves. Goldens fail → no bump.

M5 stays “do not format, do not fork unless we must.” It is not in this campaign’s bump path.

## 7. Radio boundary

Keep and widen `radio_control_ops_t`. Do not replace it with a class hierarchy.

**Ops** (every radio, for a QSO): `ready`, `on_audio_start`, `sync_frequency_mode`, `begin_tx`, `set_tone_hz`, `end_tx`, `set_tune`, `set_time`. Null pointer = not supported, not a crash.

**Capabilities** (flags/struct the core and HUD ask): `has_wattmeter`, `has_swr`, `can_set_time`, `audio_is_uac`, `can_md8_tune`, `can_ps0`, and the audio-source binding. No `if (backend == QMX)` in `main.cpp` or in the dispatcher.

**One `.cpp` per radio.** New radio = fill ops + capabilities, register one table row. No edit to the slot loop.

**TX power/SWR** move from the QMX `if` in `radio_control.cpp` onto ops/capabilities.

**QMX and QMX+** are one backend.

**I8 / I10** stay Ideas until the QMX backend can expose `can_md8_tune` / `can_ps0` and the core only calls through that.

Contributor test: a second radio can be added without touching `main.cpp`. We do not have to ship that radio.

## 8. Execution order

Restyle and rename only the unit you are already changing.

| Slice | What | Status |
|---|---|---|
| A | This RFC + `STYLE.md` + agent workflow tenant | Done (D10) |
| B | Widen radio ops/capabilities; move power/SWR and QMX `if`s out of dispatcher/`main` | Done. ADV + QMX+ on desk. Pre-I5. |
| C | Pin `ft8_lib`, wrappers, documented bump path; goldens gate the pin | Done. Submodule `components/ft8_lib/vendor` → `kb2slo/ft8_lib` @ `f211146`. Host goldens + `idf.py build` green. Field: QSO on ADV+QMX. Was B9. |
| D | Station parse/serialize + `sscanf` date/time overlap; host round-trip | Done. `main/station.cpp` (was `components/station` until §5.1 demoted it 2026-09-04); `host_test_station`. Field: load/save/reboot on ADV. Was B3/B5. |
| E | ADIF 10-min *logger* dedupe into `adif` (merge already shipped; `main/adif.cpp` since §5.1) | Done. Host: window, refresh, cap. Merge still ignores the window. Was B3 remainder. |
| F | TA format (kill `tx_e2e` copy), decode sort, power hysteresis | Done. Shared `radio_ta_format`; host decode sort and battery hold. Field: QSO on ADV+QMX+. Was B3 remainder. |
| G | Dead-code pass on each extracted unit | Done. D–F APIs all live; unused `#include <cstring>` dropped from Station. |
| H | QSO browse parse/format (daily `.adi` filter, record page, list lines) | Done. `main/qso_browse.cpp` (was `components/qso_browse` until §5.1); `host_test_qso_browse`. Field: Q screen on ADV. Directory list FATFS is a worker (B7). Record page read is time-sliced in `main`. Unblocked B7. |

One open slice at a time. **No open extract.** C (`ft8_lib` pin) is done. Campaign §2 is met. Remainder of §7 (add a radio without editing `main.cpp`) is Backlog B14.

Campaign Done-when is §2. This RFC is Roadmap Done. Remainder of §7 is Backlog B14.

## 9. What this is not

Not a second product plan. Ideas stay in `ROADMAP.md`. This RFC does not schedule I1–I10 except to say I5/I8/I10 depend on slice B.
