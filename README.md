# Mini-FT8 (kb2slo fork)

This is Jeff KB2SLO’s continuation of Wei AG6AQ’s Mini-FT8 for the Cardputer ADV. The goal is the same QRP FT8/FT4 box, with less of the original “don’t tap that during TX / don’t copy logs / don’t flash or you lose the launcher” friction, and with a path to more radios later.

- **Improved UX while transmitting.** The decode list stays tappable so you can queue a QSO; a one-row banner shows the TX message plus QMX power and SWR (abort and high SWR stay pinned). Leftover rows dim after the slot so they don’t look live.
- **One tap, one callsign.** R-tap will not stack the same station twice.
- **Flash without losing M5Launcher.** `tools/flash_keep_launcher.py` writes the app slot only. Merges that change firmware also publish `minift8-dev.bin`.
- **Logs survive the workflow.** Copy-to-SD merges `.adi` instead of overwriting; internal FAT append no longer leaves empty QSO files. Critically low battery stops TX and flash writes so a dying pack does not corrupt the filesystem.
- **Charge Mode, not a dead Sleep.** MENU charge matches Launcher: percent stripe (or `SW ON to charge`), then dim and screen off; first key wakes.
- **Sync time from an iPhone.** Unplug the radio from USB-C first. `H` then `1`: Cardputer advertises `Mini-FT8-<call>`. Connect with nRF Connect or LightBlue every time (Settings → My Devices will connect and drop without setting time). After **Time OK**, plug QMX/KH1 back in and **S → 2**. STATUS `P` is UTC (phone local converted with timezone/DST — check time.is, not the lock screen). Time only, no grid. NimBLE is torn down after the read. Leave the radio unplugged for the whole sync — NimBLE and QMX USB host cannot share the port’s DMA.
- **Busy band, busy UI.** Opening QSO browse or copying files does not stall the slot loop. Beacon CQ re-arms after a QSO or an empty slot (replies still win); turning beacon OFF drops the queued CQ. Low battery abort is visible on R.

## Clone and CI firmware

To build from source, clone with the `ft8_lib` submodule. GitHub’s default clone leaves `components/ft8_lib/vendor` empty, and host `tx_e2e` / `idf.py build` will fail.

```
git clone --recurse-submodules https://github.com/kb2slo/Mini-FT8.git
```

Existing trees: `git submodule update --init`.

Pushes and pull requests run GitHub Actions (ESP-IDF **v5.5.1**, target **esp32s3**). When firmware sources change, the job uploads a flashable merged image as artifact **MiniFT8-Cardputer-ADV** (hash-first `.bin` inside the zip). Docs-only commits (roadmap, README, and similar) skip the IDF build and do not refresh the rolling image.

- Each merge to `main` that changes the firmware updates a prerelease at tag [`dev`](https://github.com/kb2slo/Mini-FT8/releases/tag/dev) with `minift8-dev.bin`. Flash at `0x0`.
- Tags matching `v*` also create a versioned GitHub Release (`MiniFT8-<tag>-Merged.bin`).

```
esptool.py --chip esp32s3 write_flash 0x0 minift8-dev.bin
```

Host autoseq (`host_mock`) and `tests/tx_e2e` CTest run in a separate job. Hardware CAT/flash is still local.

A `0x0` flash of the merged image replaces Launcher. Daily iterate with `python tools/flash_keep_launcher.py` once Mini-FT8 has been installed from Launcher once.

## Notes that differ from the manual below

**Charge Mode (MENU `M` → `6`):** this fork replaced Sleep. Launcher stripe + Mini-FT8 version; `SW ON to charge` under the bar. Screen off after ~25 s idle. First key wakes; any key after that exits.

**Sync iPhone (`H` then `1`):** unplug QMX/KH1 from Cardputer USB-C first. Then start one-shot BLE time sync. On the iPhone: nRF Connect or LightBlue → connect to `Mini-FT8-<call>` and Pair if asked. Use nRF Connect again after a reboot; the Settings Bluetooth list is not this path. Wait for **Time OK**, then plug the radio in and **S → 2**. STATUS time shows `P` after a successful read and is UTC, not the iPhone lock-screen clock. This does not set grid. If the screen says **No 2A0F**, the phone did not expose timezone and time is not applied. If the radio stays on USB-C during sync, CAT/`TA` often dies afterward.

**USB Drive (`C`):** expose internal FATFS as a USB disk. Best on Mac or PC; iPhone Files is unreliable. Unplug the radio from USB-C first if you were using QMX/KH1-USBC; the same port cannot be USB host and a disk at once. Press `C`. The Cardputer enumerates as `USB DISK`. The screen says `Waiting for computer...` until the computer actually attaches. On Mac or Windows, copy logs off the disk, eject it in Finder/Explorer, then press `C` again to return to RX. If the screen says `USB busy, unplug radio`, disconnect the QMX/KH1 USB cable first. iPhone/iPad Files is best-effort only.

**Band config (`O` then `3`):** replaces the old ActiveBand long-edit. `*` is in the STATUS band rotation, `.` is off. `1`–`6` toggle the row; `;` / `.` page; Enter edits kHz. The last enabled band cannot be turned off. `` ` `` returns to MENU P3. `S` then `3` still steps the enabled set. `B` is still the frequency-only list.

**Clock is not stored in Station.txt.** After reboot, time comes from a DS3231 if fitted, else the ESP RTC if it still has a valid year, else you sync (iPhone CTS, GPS, or `S` date/time). Last session’s clock is not reloaded from the file.

---

# Original README (Wei AG6AQ)

The hardware list, thanks, and operation manual below are Wei’s text as of the split from [wcheng95/Mini-FT8](https://github.com/wcheng95/Mini-FT8). Use the fork notes above where they disagree (Charge Mode, USB Drive, Band config, Station.txt clock, clone/CI).

Subscribe to [https://freelists.org/list/qrp-portable](https://freelists.org/list/qrp-portable) for announcements, discussions, and updates about my Mini-series apps for the Cardputer ADV.

### Dean, KD3AN had added support for IC705: https://github.com/hamrec/cp705
### Michael, 2E0MIK had added support for FTX-1: https://github.com/mapoby/Mini-FT8

# First POTA activation (v1.0 2025-12-31)
![First POTA Activation](IMG_6087.jpeg)

## Mini-FT8 Release Notice
Mini-FT8 is built on Karlis Goba’s ft8_lib. It’s also a joint adventure between Zhenxing (N6HAN) and Wei (AG6AQ), with inspiration from DXFT8 by Barb (WB2CBA) and Charley (W5BAA). It has been a great learning platform for me, and I hope you find it just as fun to use. It supports FT8 and FT4 with QMX, QDX, and KH1.

### Thanks

- The [DX FT8](https://github.com/WB2CBA/DX-FT8-FT8-MULTIBAND-TABLET-TRANSCEIVER) team — Barb (WB2CBA), Charley (W5BAA), and Paul (G8KIG) — for the inspiration.
- Zhenxing (N6HAN) — for helping build the audio/DSP path (UAC) and autoseq. This project would not have been possible without his help.
- Karlis Goba — for [ft8_lib](https://github.com/kgoba/ft8_lib). Thanks also to Shawn Rutledge for non-standard callsign support.
- OpenAI and Anthropic — for their incredible coding assistance.
  
### Hardware
(I have no affiliation with the vendors.)
  - Must order: https://shop.m5stack.com/products/m5stack-cardputer-adv-version-esp32-s3 or from digikey: https://www.digikey.com/en/products/detail/m5stack-technology-co-ltd/K132-ADV/27685158
  - Optional: [https://shop.m5stack.com/products/gps-bds-unit-v1-1-at6668](https://shop.m5stack.com/products/gps-bds-unit-v1-1-at6668) (PORTA GPS for Date/Time/Grid; other UART NMEA GPS modules work too)
  - Optional: M5Stack LoRa-1262 cap GNSS (set `GNSS_LoRa:ON`; only the GNSS is used)
  - Optional: DS3231 RTC module on I2C `G8/G9` (for retained UTC date/time without GPS)
  - For KH1 TX: https://shop.m5stack.com/products/4pin-buckled-grove-cable, for a custom serial cable
  - For KH1-USBC RX: [USB-C microphone adapter](https://www.amazon.com/dp/B0FWC9ZFC4?ref=ppx_yo2ov_dt_b_fed_asin_title&th=1). Other adapters may also work, but this one is confirmed. KH1-MIC uses the Cardputer built-in microphone, so the USB-C adapter is optional.

73, Wei AG6AQ

# Mini-FT8 Operation Manual

## Quick Mode Map

| Key | Mode | Purpose |
|---|---|---|
| `R` | RX | View decoded messages and tap one to start a QSO. |
| `T` | TX Queue | View and manage the transmit queue. |
| `S` | STATUS | Access beacon, connect/sync, band step, tune, and date/time functions. |
| `G` | GPS | View GPS telemetry and synchronization status. |
| `M` | MENU P1 | Configure core station and operator settings. |
| `N` | MENU P2 | Configure radio, input, and comment settings. |
| `O` | MENU P3 | Configure logging, active bands, GNSS LoRa GPS, copy-to-SD, and retry settings. |
| `Q` | QSO | Browse QSO and log files, and view entries. |
| `D` | Delete Files | Browse and delete files stored in internal FATFS. |
| `B` | BAND | Edit per-band frequencies. |
| `C` | USB Drive | Toggle internal FATFS ownership between Mini-FT8 and the PC. |
| `P` | Performance | View A Simple Performance Monitor. (added in V2.0.4)|

## Global Keys and Navigation

- `R` / `T` / `B` / `S` / `G` / `Q` / `D` / `C`: switch to the selected mode. Press the same mode key again to return to `RX`.
- `M` / `N` / `O`: jump to MENU page 1 / 2 / 3. Press the current page key again to return to `RX`.
- `` ` ``: cancel TX globally in `RX`, `TX`, and `STATUS` when not editing.
- `▲` / `▼`: page up / page down in `RX`, `TX`, `BAND`, `MENU`, `QSO`, and `Delete`.
- `◀` / `▶`: move left / right in `QSO-SNR`, `STATUS` date/time, `MENU P2` (N->2).
- `1`..`6`: always select the currently visible row in the active mode.

## Per-Mode Controls

- ` acts as ESC where applicable.
- Text Edit: Backspace deletes, ` cancels, Enter saves.
  
| Mode | Item | Notes |
|---|---|---|
| `R` (RX) | `1..6` | Select a decoded line to reply to. CQ messages are sorted from strongest to weakest. If selected within 4 s, TX starts immediately. |
|  | `▲` `▼` | Page up/down is available when line 1 or line 6 is cyan. |
| `T` (TX Queue) | `1` | Rotate the queue to the next same-parity entry. |
|  | `2..6` | Drop the queue item on the current page. |
|  | `` ` `` | Cancel TX immediately. |
| `G` (GPS) |  | View live GPS telemetry including active source, 3D fix, satellites, UTC time, grid square, and last synchronization age. |
| `S` (STATUS) | `1` | Cycle Beacon mode. Applies when leaving STATUS mode. |
|  | `2` | Run connect/sync now; starts audio and follows the CAT sync path. |
|  | `3` | Step to the next active band. Applies after key 2 is pressed or when leaving STATUS. |
|  | `4` | Toggle Tune. |
|  | `5` | Edit Date (in place). On the Time line, `G` means GPS time and `R` means DS3231 RTC time. |
|  | `6` | Edit Time (in place). |
| `M` (MENU P1) | `1` | Cycle CQ Type. For CQ FD, enter operating class and ARRL/RAC section in FreeText, for example `1B SCV`. |
|  | `2` | Send FreeText once. |
|  | `3` | Edit FreeText (Long Edit). Used for SOTAMAT, park/summit reference, ARRL Field Day exchange, CQ modifiers (`CQ EU`, `CQ ASIA`), and similar text. |
|  | `4` | Edit Call (in place). |
|  | `5` | Edit Grid (in place). Supports 4/6/8-character grid. If GPS is available, the GPS grid is shown and used, but not saved. |
|  | `6` | Enter Sleep. Shows battery info. |
| `N` (MENU P2) | `1` | Select offset source: Random / RX / Fixed. Random values are within 500-2500 Hz. |
|  | `2` | Edit fixed cursor offset (in place). Enter directly or use `▲` `▼` `◀` `▶`. |
|  | `3` | Select radio (`QMX` / `QDX` / `KH1-USBC` / `KH1-MIC`). |
|  | `4` | Edit ignore list (Long Edit). Prefixes are separated by spaces; maximum 64 characters. |
|  | `5` | Edit comment (Long Edit). Used for ADIF logging. Supports `/Radio` and `/Grid` macro expansion. |
|  | `6` | Select FT8 / FT4 protocol. Reboot to apply the change. |
| `O` (MENU P3) | `1` | Turn RxTx log on/off. Note: RxTxLog has been renamed to `RT[YYMMDD].txt`. |
|  | `2` | Turn SkipTX1 on/off. Skips `dxcall mycall mygrid` and replies with the SNR report. |
|  | `3` | Edit active bands (Long Edit). Used by STATUS -> Band. |
|  | `4` | Toggle `GNSS_LoRa`. `OFF` uses PORTA GPS; `ON` uses the LoRa-1262 cap GNSS. |
|  | `5` | Copy files to SD. Feedback is `Copied OK` or `Missed [n]`. |
|  | `6` | Edit max retry (in place). Accepts any natural number or `0`. |
| `Q` (QSO) | `1..6` | Open the selected ADIF file. |
|  | `◀` `▶` | Switch columns (Default view or SNR view). |
| `D` (Delete Files) | `1..6` | Delete the selected file immediately, without confirmation. |
| `B` (BAND) | `1..6` | Choose a band slot to edit. |
| `C` (USB Drive) |  | Stop radio audio and expose FATFS to the PC. Safely eject it on the PC, then press `C` again to remount storage and return to RX. |
| `P` (PERFORMANCE) | | A Simple Performance Monitor. (added in V2.0.4) |

## Download Logs

- Mini-FT8 and Mini-CW share the `fatfs` partition. Their files can coexist,
  and current M5Launcher installs/reinstalls can switch between the applications
  while preserving an existing compatible FATFS partition. Both applications
  use 512-byte FATFS and wear-levelling sectors.

- Use SD
  - Insert a FAT/FAT32-formatted SD card.
  - In MENU P3 (`O`), press `5` (Copy files to SD). All files will be copied to the SD card.
  - If the result shows `Missed`, a reboot will usually fix it.

## GPS Connections

Mini-FT8 supports two GPS sources selected from MENU P3 (`O -> 4`):

- `GNSS_LoRa:OFF` uses the PORTA GPS wiring below. Both 9600 and 115200 baud GPS modules are supported and auto-detected. **Make sure the micro switch is on the left.** Once Mini-FT8 gets its time/grid, the GPS can be removed, this is important for KH1.
- `GNSS_LoRa:ON` uses the M5Stack LoRa-1262 cap GNSS on UART2 (`RX=G15`, `TX=G13`) at 115200 baud. The LoRa/SX1262 radio side is not used. This source can keep running while KH1 CAT uses PORTA/UART1.

When `GNSS_LoRa` is `ON`, the physical G4/G5 debug UART path is disabled and the pins are left as floating inputs to avoid conflicts. USB Serial/JTAG host commands still work.

The GPS view shows the active source on its first line.
```text
┌──────────────────┐                 ┌─────────────────────────────┐
│ GPS              │                 │ Cardputer ADV               │
│                  │                 │ PORTA                       │
│ GND ─────────────┼─────────────────┤ GND                         │
│ VDD ─────────────┼─────────────────┤ 5V                          │
│ RX  ─────────────┼<──(Not Used)────┤ TX (G2)                     │
│ TX  ─────────────┼────────────────>┤ RX (G1)                     │
└──────────────────┘                 │                             │
                                     │ SW: 5VOUT (Left)            │
                                     └─────────────────────────────┘
```

## DS3231 RTC Connections

Mini-FT8 can use an optional DS3231 module as an external UTC clock. Connect it
to the Cardputer Adv shared I2C bus: `SDA=G8`, `SCL=G9`, plus module power and
ground. On boot, a valid DS3231 time is used before the ESP RTC or saved
`Station.txt` time. Status `S -> 6` appends `R` when the active time came from
the DS3231, and appends `G` after a full GPS time sync. GPS and manual time
updates write the DS3231 when it is present; FT8 decode fine corrections do not.

## KH1 Connections
![KH1 Cables](kh1_cables.jpeg)

 - TX Only ([sotamat](https://sotamat.com/))
```text
┌──────────────────┐                 ┌────────────────────────────┐
│ KH1 RS232        │                 │ Cardputer ADV              │
│                  │                 │ PORTA                      │
│ GND ─────────────┼─────────────────┤ GND                        │
│                  │                 │ 5V (NC)                    │
│ Tip(Rx) ─────────┼<────────────────┤ TX (G2)                    │
│ Ring(TX) ────────┼───(Not Used)───>┤ RX (G1)                    │
└──────────────────┘                 │                            │
                                     │ SW: NA                     │
                                     └────────────────────────────┘
```
- TX + RX (FT8/FT4 QSO)
  - Choose `KH1-USBC` for USB-C audio adapter RX. Tested adapter: Amazon `B0FWC9ZFC4`. Other adapters may also work, but this one is confirmed.
  - Choose `KH1-MIC` for Cardputer microphone RX. No USB-C audio adapter is needed.
  - For `KH1-USBC`, supply 5 V to PORTA; otherwise, the USB-C OTG port will not be powered. **Make sure the micro switch is on the right**
```text
┌──────────────────┐
│ Power Cable      │
│ GND ─────────────┼─────────┐
│ 5V  ─────────────┼─────┐   │
└──────────────────┘     |   |
┌──────────────────┐     |   |       ┌────────────────────────────┐
│ KH1 RS232        │     |   |       │ Cardputer ADV              │
│                  │     |   |       │ PORTA                      │
│ GND ─────────────┼─────)───┴───────┤ GND                        │
│                  │     └───────────┤ 5V                         │
│ Tip(Rx) ─────────┼<────────────────┤ TX (G2)                    │
│ Ring(TX) ────────┼── (Not Used)───>┤ RX (G1)                    │
└──────────────────┘                 │                            │
                                     │ SW: 5VIN (Right)           │
                                     └────────────────────────────┘
```

- Mini-FT8 automatically sets KH1 TX power to 2 W.
- For best RX performance, reduce AF volume to `05` or `06`.
