# RFC 0002: Extract, style, and boundaries

* **Status:** Accepted for execution
* **Author:** Jeff Kalikstein, KB2SLO
* **Covers:** extract / style / `ft8_lib` / radio contract (pre-I5). This RFC is Roadmap Now; §2 is Done-when. Was I11 / B3 / B9.
* **Does not cover:** I3 companion, I6 multi-mode rename, I8/I9/I10 QMX product features

## 1. Why

`main.cpp` is the product: slot loop, UI, Station, ADIF, CAT, audio, and radio policy in one file. That blocks three things we actually want:

1. Safe change — new logic without a 6k-line merge and without untested behavior.
2. Upstream `ft8_lib` — ingest Karlis’s updates and send clean patches back.
3. Other radios — QMX/QMX+ must not be assumed in the core, so a maintainer can add a radio without editing the slot loop.

This RFC is the campaign plan and the Done-when. `docs/STYLE.md` is the short coding standard. `docs/ROADMAP.md` only points here while this is Now. Chat is intake; those files are truth.

## 2. Outcomes (done when)

1. **Written standard.** `docs/STYLE.md` is the coding rule for *our* C/C++. New and extracted code follows it. Vendored `M5*` / `ft8_lib` do not.
2. **Module ratchet.** New behavior does not land as more `main.cpp`. It gets a named unit, a header that is the API, and a host test if it is parse / format / policy. `main.cpp` may only shrink.
3. **Vendor boundary.** `ft8_lib` is a pinned upstream (SHA) plus Mini-FT8 wrappers. Protocol patches are rebaseable commits on our fork. Bump path is 3-way merge + `tx_e2e` golden RX/encode. Karlis’s files look like Karlis’s files.
4. **Radio boundary.** Core talks `radio_control` **ops + capabilities**. QMX CAT, meters, `MD8`, `PS0;`, and UAC profile live in the QMX backend. `main.cpp` does not `if (QMX)`. QMX and QMX+ are one backend until CAT differs.
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

## 6. Vendor boundary (`ft8_lib` first)

Third-party source we did not write:

- Pin a SHA (submodule or our GitHub fork of `kgoba/ft8_lib`).
- Mini-FT8 wrappers only: `fft_wrapper`, buffers, IDF CMake, ESP static-FFT.
- Protocol patches (FD / nonstd / Dxpedition) live as rebaseable commits on the fork, each small enough to open upstream or drop.
- Contribute back only Karlis-shaped fixes (correctness, protocol). Never “make it compile on ESP32-S3.”
- Do not clang-format or re-indent that tree.
- **Bump:** fetch, 3-way merge, `tx_e2e` golden RX/encode green, then move the pin. Goldens fail → no bump.

M5 stays “do not format, do not fork unless we must.” It is not in this campaign’s bump path.

## 7. Radio boundary

Keep and widen `radio_control_ops_t`. Do not replace it with a class hierarchy.

**Ops** (every radio, for a QSO): `ready`, `on_audio_start`, `sync_frequency_mode`, `begin_tx`, `set_tone_hz`, `end_tx`, `set_tune`, `set_time`. Null pointer = not supported, not a crash.

**Capabilities** (flags/struct the core and HUD ask): `has_wattmeter`, `has_swr`, `can_set_time`, `audio_is_uac`, `can_md8_tune`, `can_ps0`, and the audio-source binding. No `if (backend == QMX)` in `main.cpp` or in the dispatcher.

**One `.cpp` per radio.** New radio = fill ops + caps, register one table row. No edit to the slot loop.

**Meters** move from the QMX `if` in `radio_control.cpp` onto ops/caps.

**QMX and QMX+** are one backend.

**I8 / I10** stay Ideas until the QMX backend can expose `can_md8_tune` / `can_ps0` and the core only calls through that.

Contributor test: a second radio can be added without touching `main.cpp`. We do not have to ship that radio.

## 8. Execution order

Restyle and rename only the unit you are already changing.

| Slice | What | Status |
|---|---|---|
| A | This RFC + `STYLE.md` + agent workflow tenant | Done (D10) |
| B | Widen radio ops/caps; move meter and QMX `if`s out of dispatcher/`main` | Later; needs ADV. Pre-I5. |
| C | Pin `ft8_lib`, wrappers, documented bump path; goldens gate the pin | Next. Was B9. |
| D | Station parse/serialize + `sscanf` date/time overlap; host round-trip | Later. Was B3/B5. |
| E | ADIF 10-min *logger* dedupe into `components/adif` (merge already shipped) | Later. Was B3 remainder. |
| F | TA format (kill `tx_e2e` copy), decode sort, power hysteresis | Later. Was B3 remainder. |
| G | Dead-code pass on each extracted unit | Ongoing per extracted unit. |

One open slice at a time. **Next is C** (`ft8_lib`; no ADV required). Slice B waits on ADV. G is dead code on the unit you are already extracting, not a separate Now. B7 (non-blocking FATFS) is after Station/QSO browse have a module to move.

Campaign Done-when is §2.

## 9. What this is not

Not a second product plan. Ideas stay in `ROADMAP.md`. This RFC does not schedule I1–I10 except to say I5/I8/I10 depend on slice B.
