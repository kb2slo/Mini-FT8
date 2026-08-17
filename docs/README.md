# docs/

Human- and tool-readable project memory. Prefer this tree over Cursor rules, `CLAUDE.md`, or chat transcripts.

| File | Role |
|---|---|
| [ROADMAP.md](ROADMAP.md) | Plan: Now / Backlog / Ideas / Done. Chat is intake; this file is truth. |
| [AUTOSEQ_ARCHITECTURE.md](AUTOSEQ_ARCHITECTURE.md) | Sequencer design (slot events, tick vs decode). |
| [AUTOSEQ_INACTIVE_QUEUE.md](AUTOSEQ_INACTIVE_QUEUE.md) | Retry exhaustion / reincarnation / inactive zone. |
| [RTC_COMPENSATION.md](RTC_COMPENSATION.md) | Clock compensation. |
| [FT8 Free-Text Reference Extension.md](FT8%20Free-Text%20Reference%20Extension.md) | Free-text / SOTA-style payload notes. |
| `rfcs/` | Long feature specs (e.g. BLE companion on branch `rfc/0001-ble-companion`). Roadmap only links them. |

Git history is the changelog. Do not keep a narrative log in these files.

## Working agreement (any agent)

At the start of a session, read `docs/ROADMAP.md` (and the architecture doc for the area you touch).

Update `ROADMAP.md` in the **same turn** as the work:

- New idea → **Ideas** (do not implement until moved up)
- Agreed / sequenced → **Backlog**
- Next to build → **Now** (one theme)
- Shipped → **Done**; drop the row from Now/Backlog

Each row has a type: `fix` | `extract` | `feature` | `ci` | `docs`.

Do not expand `main.cpp` without extracting a tested function. Do not mix a `feature` change with an unrelated `fix`.

This fork ships on **`origin/main`** (`kb2slo/Mini-FT8`). Iterate and push there. Do **not** push or open PRs to `upstream` (`wcheng95/Mini-FT8`) unless the operator explicitly asks. Staying mergeable with upstream is not a goal.

### Roadmap intake (ideas)

If the operator asks to add an idea or “add to the backlog”:
1. Draft the `docs/ROADMAP.md` row in chat. Do not commit yet.
2. New items go in **Ideas**. Use **Backlog** only when they give a Done-when or say it is sequenced.
3. After they approve, commit that row on `main` and push `origin`. Never `upstream` unless they explicitly ask.

### Backlog grooming reminder

The grooming *workflow* is still TBD. Until then:

- Once per session start, check `git log -1 --format=%ci -- docs/ROADMAP.md`.
- If that commit is older than about **1–2 days**, remind the operator once that it is time to groom Now / Backlog / Ideas / Done (promote, drop shipped Now rows, sequence or park ideas).
- Do not repeat the reminder in later turns of the same session, and do not interrupt an in-progress coding task.

### Local IDF build / flash

After `source` of the idf5.5 venv and `esp-idf/export.sh`. Radio unplugged from Cardputer USB-C. Leave Launcher **USB** (MSC) first so `/dev/cu.usbmodem*` exists.

**Keep Launcher** (daily iterate once Mini-FT8 has been installed from Launcher once):

```bash
python tools/flash_keep_launcher.py
```

Reads the *device* partition table and writes `build/mini_ft8.bin` into an OTA slot only. Never factory (Launcher), bootloader, or the table. `--dry-run` prints the layout. `--no-build` skips `idf.py build`.

This Cardputer’s Mini-FT8 slot is **`bt4000`** (Launcher PMan name). The helper prefers that partition when it exists; `--partition NAME` overrides.

Do **not** use `idf.py flash` or `idf.py app-flash` on a Launcher unit: both write `0x10000` / factory and replace Launcher.

**No Launcher** (factory-only Mini-FT8 layout):

```bash
idf.py -p /dev/cu.usbmodemXXXX build flash
```

Do not split into separate `build` then `flash` unless the user asks.

**`monitor`:** The Cardputer USB-C is either ESP USB Serial/JTAG (Mac ↔ firmware logs) **or** USB host for the radio (QMX / KH1-USBC) — not both. So `idf.py … flash monitor` works with the **radio unplugged** from USB-C. Live logs while the radio is connected need the console UART on **G4 (TX) / G5 (RX)** (USB–TTL adapter), not PORTA/Grove (`G1`/`G2`, GPS/CAT). That G4/G5 path is off when `GNSS_LoRa:ON`.

The repo-root `AGENTS.md` exists only so Claude Code, Codex, and Cursor load this agreement. Do not duplicate the plan there.
