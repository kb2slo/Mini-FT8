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

Do not expand `main.cpp` without extracting a tested function. Do not mix a `feature` change with an unrelated `fix`. Work on the fork first; upstream what is clearly useful.

### Local IDF build / flash

After `source` of the idf5.5 venv and `esp-idf/export.sh`, prefer one command:

```bash
idf.py -p /dev/cu.usbmodemXXXX build flash
```

Do not split into separate `build` then `flash` unless the user asks.

**`monitor`:** The Cardputer USB-C is either ESP USB Serial/JTAG (Mac ↔ firmware logs) **or** USB host for the radio (QMX / KH1-USBC) — not both. So `idf.py … flash monitor` works with the **radio unplugged** from USB-C. Live logs while the radio is connected need the console UART on **G4 (TX) / G5 (RX)** (USB–TTL adapter), not PORTA/Grove (`G1`/`G2`, GPS/CAT). That G4/G5 path is off when `GNSS_LoRa:ON`.

The repo-root `AGENTS.md` exists only so Claude Code, Codex, and Cursor load this agreement. Do not duplicate the plan there.
