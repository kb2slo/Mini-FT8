# docs/

Human- and tool-readable project memory. Prefer this tree over editor-specific rule files, vendor instruction stubs, or chat transcripts.

| File | Role |
|---|---|
| [ROADMAP.md](ROADMAP.md) | Plan: Now / Backlog / Ideas / Done. Chat is intake; this file is truth. |
| [STYLE.md](STYLE.md) | Coding standard for our C/C++ (not vendored `M5*` / `ft8_lib`). |
| [AUTOSEQ_ARCHITECTURE.md](AUTOSEQ_ARCHITECTURE.md) | Sequencer design (slot events, tick vs decode). |
| [AUTOSEQ_INACTIVE_QUEUE.md](AUTOSEQ_INACTIVE_QUEUE.md) | Retry exhaustion / reincarnation / inactive zone. |
| [RTC_COMPENSATION.md](RTC_COMPENSATION.md) | Clock compensation. |
| [FT8 Free-Text Reference Extension.md](FT8%20Free-Text%20Reference%20Extension.md) | Free-text / SOTA-style payload notes. |
| `rfcs/` | Long specs. Extract/style/boundaries: [0002](rfcs/0002-extract-and-boundaries.md). BLE companion: [0001](rfcs/0001-ble-companion.md) (RAM measurement is a Phase 1 gate). Roadmap only links them. |

Git history is the changelog. Do not keep a narrative log in these files.

## Working agreement (any agent)

At the start of a session, read `docs/ROADMAP.md` (and the architecture or RFC for the area you touch). Follow [STYLE.md](STYLE.md) for our C/C++.

**Workflow matches documentation.** Committed docs are the workflow: this file, `ROADMAP.md`, `STYLE.md`, RFCs, and the architecture notes. Cite the relevant doc when proposing or making a change.

**Push back before tools.** Classify the request against `ROADMAP.md` Now, the active RFC, and `STYLE.md` **before any search, read, or edit of implementation.** If it is not the current Now theme, not an explicit exception in those docs, or is still Ideas/Backlog (including “quick” UI/product tweaks), the first reply cites the doc and talks it through. No codebase exploration to be helpful. Tools only after the operator retracts, grants an exception, or updates the doc in the same turn. Docs and roadmap questions are allowed without that gate.

If the operator asks for something that violates those docs, point it out, cite the rule, and talk it through **before** coding. Then the operator may retract, grant a one-off exception, or update the doc in the same turn and proceed. Do not silently diverge. An exception or doc change is an explicit decision, not a shortcut.

Update `ROADMAP.md` in the **same turn** as the work:

- New idea → **Ideas** (do not implement until moved up)
- Agreed / sequenced → **Backlog**
- Next to build → **Now** (one theme)
- Shipped → **Done**; drop the row from Now/Backlog

Each row is an ID plus a name. Do not mix a feature change with an unrelated fix in the same commit.

Do not expand `main.cpp` without extracting a tested function. Extract/style/radio/`ft8_lib` campaign [RFC 0002](rfcs/0002-extract-and-boundaries.md) is Done. STYLE still applies. Radio-profile remainder is Backlog B14.

This fork ships on **`origin/main`** (`kb2slo/Mini-FT8`). Do **not** push or open PRs to `upstream` (`wcheng95/Mini-FT8`) unless the operator explicitly asks. Staying mergeable with upstream is not a goal.

### Clone (submodules)

`ft8_lib` is the git submodule `components/ft8_lib/vendor`. Host `tx_e2e` and `idf.py build` need it. GitHub’s default clone does not fetch it.

```bash
git clone --recurse-submodules https://github.com/kb2slo/Mini-FT8.git
```

Existing trees: `git submodule update --init`. Pin and bump path: [RFC 0002](rfcs/0002-extract-and-boundaries.md) §6.

### Chat gates

The operator reviews in chat, then green-lights each step. A good previous turn is not permission to skip a gate.

1. **Design in chat.** Architecture and “should we” questions get an answer first. Do not implement until they say to (e.g. “go”, “do it”). Docs and roadmap questions do not need that wait.

2. **Test plan with the result.** When work is done, the reply includes a short test plan: host command(s) they can run, and a field check if the path is field-only. They often run tests and flash themselves and will say when it is solid. Do not flash the device unless they ask.

3. **Commit message in chat.** Propose the message here (match recent `git log`: one sentence, why). Wait for them to accept or edit. Do not commit in the same turn as the proposal unless they already asked to commit.

4. **Commit / push only when asked.** Local edits are fine when they asked for the work. `git commit` waits until they have seen the plan or diff and said to commit. `git push origin` waits until they said to push. “Commit and push” is both, using the agreed message. Do not treat “ship on main”, a green host test, or “local commits are fine” as standing permission.

### Roadmap intake (ideas)

If the operator asks to add an idea or “add to the backlog”:
1. Draft the `docs/ROADMAP.md` row in chat. Do not commit yet.
2. New items go in **Ideas**. Use **Backlog** only when they give a Done-when or say it is sequenced.
3. After they approve, commit that row on `main`. Push `origin` only when they ask to push. Never `upstream` unless they explicitly ask.

### Backlog grooming reminder

The grooming *workflow* is still TBD. Until then:

- Once per session start, check `git log -1 --format=%ci -- docs/ROADMAP.md`.
- If that commit is older than about **1–2 days**, remind the operator once that it is time to groom Now / Backlog / Ideas / Done (promote, drop shipped Now rows, sequence or park ideas).
- Do not repeat the reminder in later turns of the same session, and do not interrupt an in-progress coding task.

### Upstream pin watch (session start)

Once per session start, same rules as grooming (once, do not interrupt coding). Network fail → skip.

`ft8_lib` only ([RFC 0002](rfcs/0002-extract-and-boundaries.md) §6). Do not watch `wcheng95/ft8_lib` or Mini-FT8 `upstream`. GitHub **Sync fork** on `kb2slo/ft8_lib` is off (parent is Wei, not Karlis).

```bash
git ls-remote https://github.com/kgoba/ft8_lib.git refs/heads/master
git -C components/ft8_lib/vendor merge-base --is-ancestor <kgoba-sha> HEAD
```

If the submodule is missing, skip. If Karlis’s SHA **is** an ancestor of the pin, we already have him. If it is **not**, he moved.

Then draft a Backlog row in chat. Do not commit it yet. Do not merge, bump the submodule, or open a kgoba PR in that turn. Done-when: written take (sync now / wait / drop). Sync uses RFC 0002 §6 and is a separate Now. Goldens gate the pin. If a row for that SHA already exists, remind; do not duplicate.

### Local IDF build / flash

Clone with submodules first (see **Clone (submodules)** above).

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

Loader stubs (`AGENTS.md`, `.cursor/rules/`, and similar) exist only so a given tool loads this agreement. They point here. Do not duplicate the plan in those files.
