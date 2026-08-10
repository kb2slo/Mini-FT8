# AGENTS.md

## Cursor Cloud specific instructions

Mini-FT8 is **ESP-IDF firmware** for the M5Stack Cardputer ADV (ESP32-S3). The
cloud VM has **no physical device**, so firmware can be *built* but not flashed
or run on hardware. The runnable/testable pieces in the cloud are the host-side
test suites (which exercise the core FT8 encode/decode + autoseq QSO logic) and
the firmware build itself.

### Activating ESP-IDF (required before any `idf.py` command)

ESP-IDF v5.5.1 is installed at `~/esp/esp-idf` (matches `dependencies.lock`).
Every new shell must activate it first:

```bash
. "$HOME/esp/esp-idf/export.sh"   # interactive shells can use the alias: get_idf
```

`idf.py` is not on `PATH` until this is sourced. The activation is not inherited
between separate non-interactive command invocations, so source it in the same
command you run `idf.py` in.

### Building the firmware — DO NOT run `idf.py set-target`

The target (`esp32s3`) is already pinned in the committed `sdkconfig`, so just:

```bash
. "$HOME/esp/esp-idf/export.sh"
idf.py build
```

**Gotcha:** running `idf.py set-target esp32s3` **overwrites the committed
`sdkconfig`** with defaults and drops the project's custom console-UART settings
(`CONFIG_ESP_CONSOLE_UART_CUSTOM=y`, TX/RX GPIO 4/5). The build then fails with
`'CONFIG_ESP_CONSOLE_UART_TX_GPIO' was not declared`. If this happens, restore
the file: `git checkout -- sdkconfig` (and delete the generated `sdkconfig.old`),
then `idf.py build`. Build outputs (`build/`, `MiniFT8_Merged_Auto.bin`) are
gitignored; a post-build step auto-generates the merged flash image.

### Host test suites (no hardware or ESP-IDF needed)

These build with the system GCC/Clang toolchain and are the best way to verify
core logic in the cloud:

- `tests/tx_e2e` — CMake + ctest (FT8 L1 encoder, TX state machine, poll/timer
  timing, golden-WAV RX decode, KH1 tone map):
  `cmake -S tests/tx_e2e -B tests/tx_e2e/build && cmake --build tests/tx_e2e/build && ctest --test-dir tests/tx_e2e/build`
- `host_mock` — runs the real `main/autoseq.cpp` against JSON QSO scenarios:
  `cd host_mock && make && ./host_test test_qso.json` (other `test_*.json` too).

Toolchain note: the default `c++` alternative is Clang, which needs
`libstdc++-14-dev` (installed during setup) to link, otherwise you get
`cannot find -lstdc++`.

### Flashing / running on device

Not possible in the cloud (no `/dev/ttyACM*`). Flashing helpers live in
`release/` (`flash.sh`) and host tools in `tools/` (`pc_terminal.py`, etc.,
need `pyserial`); these require real hardware.
