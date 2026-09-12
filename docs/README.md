# docs/

Human- and tool-readable project memory. Prefer this tree over editor-specific rule files, vendor instruction stubs, or chat transcripts.

| File | Role |
|---|---|
| [ROADMAP.md](ROADMAP.md) | Plan: Now / Backlog / Ideas. Chat is intake; this file is truth. Shipped work is dropped, not archived — git history is the record. |
| [../README.md](../README.md) | Operator landing. Fork notes (why this tree) above the Wei delimiter; Wei’s original below. |
| [STYLE.md](STYLE.md) | Coding standard for our C/C++ (not vendored `M5*` / `ft8_lib`). |
| [TEST_PLAN.md](TEST_PLAN.md) | What is verified and by whom: agent-run automation vs operator field checks, plus field checks owed on the current branch. |
| [RELEASE_PROCESS.md](RELEASE_PROCESS.md) | How a build becomes a download: the rolling `continuous` tag, versioned `v*` releases, what a version tag does **not** set, and what a rename/de-fork would break. |
| [AUTOSEQ_ARCHITECTURE.md](AUTOSEQ_ARCHITECTURE.md) | Sequencer design (slot events, tick vs decode). |
| [AUTOSEQ_INACTIVE_QUEUE.md](AUTOSEQ_INACTIVE_QUEUE.md) | Retry exhaustion / reincarnation / inactive zone. |
| [FT8 Free-Text Reference Extension.md](FT8%20Free-Text%20Reference%20Extension.md) | Free-text / SOTA-style payload notes. |
| `rfcs/` | Long specs. Extract/style/boundaries: [0002](rfcs/0002-extract-and-boundaries.md). Companion: [0001](rfcs/0001-ble-companion.md) (on-chip NimBLE failed the DMA gate; sidekick over UART, retargeting NanoC6 → AtomS3 Lite; **headless-Atom-as-main closed, hardware-confirmed**, §4.6; **phone path decided**: WiFi + plain browser, no app, §5.0 — BLE/GATT parked). [0003](rfcs/0003-m32-pocket-port-and-ble-time.md) **rejected** (Pocket+CTS bundle). B15 CTS is ROADMAP; I17 is Ideas (own RFC if sequenced). Roadmap only links them. |

Git history is the changelog. Do not keep a narrative log in these files.

## Working agreement (any agent)

At the start of a session, read `docs/ROADMAP.md` (and the architecture or RFC for the area you touch). Follow [STYLE.md](STYLE.md) for our C/C++.

**Workflow matches documentation.** Committed docs are the workflow: this file, `ROADMAP.md`, `STYLE.md`, RFCs, and the architecture notes. Cite the relevant doc when proposing or making a change.

**A line earns its place here if it changes what an agent does.** If it only explains how a rule came about, it belongs in the commit message, not in this file.

**Push back before tools.** Classify the request against `ROADMAP.md` Now, the active RFC, and `STYLE.md` **before any edit of implementation, or before exploration spanning more than one file.** If it is not the current Now theme, not an explicit exception in those docs, or is still Ideas/Backlog (including “quick” UI/product tweaks), the first reply cites the doc and talks it through. No open-ended codebase exploration to be helpful — that is exactly the token spend this rule exists to stop. A single cheap lookup (one grep, one file read, "where is X defined") can just be answered without the gate. Tools only after the operator retracts, grants an exception, or updates the doc in the same turn — an exception is an explicit decision, not a shortcut, and never a silent divergence. Docs and roadmap questions are allowed without that gate.

Update `ROADMAP.md` in the **same turn** as the work:

- New idea → **Ideas** (do not implement until moved up)
- Agreed / sequenced → **Backlog**
- Next to build → **Now** (one theme)
- Shipped → drop the row from Now/Backlog. No separate Done archive — git history (and the commit that dropped the row) is the record.

Each row is an ID plus a name. Do not mix a feature change with an unrelated fix in the same commit.

**Fix defects in the slice.** A defect found while working in an area gets fixed in that slice, not filed for later. It still gets a `ROADMAP.md` row, but the row records what was wrong and why — it is not a promise to act later. Single-operator workflow: the reasons to defer (protecting someone else's sprint, batching context switches across a team) do not apply, and deferred fixes here have a poor track record of ever landing.

Judge by **verifiability**, not size, and say which of the three applied:

1. **You can prove it** — host test, dead-code proof (linker map plus `nm`, byte-identical binary), or the compiler. Fix it and prove it in the same commit.
2. **Only the operator can prove it** — anything needing the radio. Fix it and add the bench check to [TEST_PLAN.md](TEST_PLAN.md) **Pending** with the exact keys and expected result.
3. **Neither can, yet** — an unreproducible hardware or timing bug. Prefer a change whose **worst case is no worse than current behaviour**, and ship the logging that makes the next occurrence diagnosable. Never let an unverified fix blend in with verified work: say plainly, in the commit and in chat, that it is unproven.

An unproven fix is still worth making. Pretending it is proven is not.

**Tier the model to the task, the same way work is tiered by verifiability above.** An agent can prove some things and not others; a model can carry some kinds of work well and not others, and a wrong choice here is not caught by the compiler the way a bad fix is. State which tier a task is before starting non-trivial work:

1. **Mechanical, low blast radius** — `git mv`-shaped extracts, renames, doc formatting, running `host_mock`/`tx_e2e`/`idf.py build` and reporting the result, applying a change the operator already fully specified. Verified by the compiler or a host test regardless of which model did it — use whatever is fast and cheap.
2. **Judgment under ambiguity** — RFC drafting, sizing a memory/timing budget, choosing an extract boundary, roadmap grooming, anything **Push back before tools** above would stop for. Use the strongest available model. Nothing mechanical catches a wrong call here, which is exactly what makes it expensive.
3. **Field-only, no model tier substitutes** — USB/CDC/CAT, UAC timing, TX behaviour, anything [TEST_PLAN.md](TEST_PLAN.md) marks Pending. This is category 2/3 of the verifiability rubric above wearing a model-choice hat: a bigger model does not turn an operator-only or hardware-only check into something an agent can close. Say so plainly rather than picking a bigger model and reporting more confidence than the check supports.

**Test plan matches the work too.** [TEST_PLAN.md](TEST_PLAN.md) is the same kind of living file as the roadmap, and gets updated in the **same turn** as the work:

- Added or changed a host-testable unit (parse / format / policy) → add or update its row under `host_mock` coverage, in the same commit as the code.
- Touched a field-only path (USB/UAC, CAT, display, GPS, RTC, SD, flash, TX timing) → add a row to **Pending** naming the commit and the exact check. "Test the radio" is not a check; "MENU P3 `5` reports `Copied OK`" is.
- Removed code that was provably never in the image — linker map plus `nm` on the ELF, byte-identical `mini_ft8.bin` — say so and add no row. That evidence *is* the test.
- Removed a harness or a check → drop its row and say why in the commit message.

State which of these you did in the turn you report the work. **"No test-plan change needed" is a valid answer and must be said out loud**, exactly like the README audit — an audit you skipped and an audit you did are otherwise indistinguishable.

**Never report a field check as passed.** An agent runs `host_mock`, `tx_e2e`, and `idf.py build` and reports those honestly. Anything needing a Cardputer, a radio, or an antenna is the operator's to run: propose it, put it in **Pending**, and wait. A green build is evidence about linkage, not about whether the radio transmitted.

**Fork README stays current.** The top of [`README.md`](../README.md) (above the Wei delimiter) is the public “why this fork” copy. When work is operator-visible (TX UX, meters, logging, flash/Launcher, Charge Mode, CQ/beacon, radios, …), audit that section in the **same turn**: propose the add, change, or drop in chat with the test plan and commit message. Include the accepted edit in the same commit and push as the feature. Do not rewrite Wei’s original below the delimiter. Skip agent, RFC, extract-only, or other work operators cannot feel on the radio.

**Triggers, stated so they cannot be missed.** Audit that section when a change adds, alters, or **removes** any of: a key binding or screen; an on-screen string the README quotes; a documented workflow (log offload, flash/Launcher, charge, time sync); or a supported radio/board. **Removal counts, and is the easy one to miss** — a README documenting a key that now does nothing is worse than one that never mentioned it. If Wei’s original below the delimiter documents the same thing, do **not** edit it: put the correction in the fork note and let the delimiter rule (“use the fork notes above where they disagree”) carry it. When it genuinely does not apply, say so explicitly in that turn, as above. CI enforces the mechanical half of this (`readme-audit` in `.github/workflows/ci.yml`): a PR that removes a `UIMode` enumerator or changes a key binding under `main/` without touching `README.md` fails, unless the PR body says `README-audit: none - <why>`. The check catches signals, not judgement — it will not notice a behaviour change that alters no enum and no key.

Do not expand `main.cpp` without extracting a tested function. Extract/style/radio/`ft8_lib` campaign [RFC 0002](rfcs/0002-extract-and-boundaries.md) is Done. STYLE still applies. Radio-profile remainder is Backlog B14. Read the current Now from `ROADMAP.md` rather than trusting a name written here — this line has gone stale before.

This fork ships on **`origin/main`** (`kb2slo/Mini-FT8`). Do **not** push or open PRs to `upstream` (`wcheng95/Mini-FT8`) unless the operator explicitly asks. Staying mergeable with upstream is not a goal.

### Clone (submodules)

`ft8_lib` is the git submodule `components/ft8_lib/vendor`. Host `tx_e2e` and `idf.py build` need it. GitHub’s default clone does not fetch it.

```bash
git clone --recurse-submodules https://github.com/kb2slo/Mini-FT8.git
```

Existing trees: `git submodule update --init`. Pin and bump path: [RFC 0002](rfcs/0002-extract-and-boundaries.md) §6.

### Reproducibility

The full dev environment — build, test, and the sidekick field-flash workflow — must be reproducible from a fresh `git clone` (submodules included) plus a capable agent, with no dependency on state that exists only on one operator's machine. When a step genuinely needs a one-time physical-hardware action (e.g. capturing a Nano's factory-state backup before it is ever field-flashed), document the exact command in the relevant README/script rather than committing a derived artifact whose redistribution terms aren't ours to decide. `sidekick/README.md`'s stock-backup section is the current example of this pattern.

### Chat gates

The operator reviews in chat, then green-lights each step. A good previous turn is not permission to skip a gate.

1. **Design in chat.** Architecture and “should we” questions get an answer first. Do not implement until they say to (e.g. “go”, “do it”). Docs and roadmap questions do not need that wait.

2. **Test plan with the result.** When work is done, the reply includes a short test plan: host command(s) they can run, and a field check if the path is field-only. They often run tests and flash themselves and will say when it is solid. Do not flash the device unless they ask.

3. **Commit message in chat.** Propose the message here (match recent `git log`: one sentence, why). For operator-visible work, also propose the fork-README delta (or say why that section needs no change). Wait for them to accept or edit. Do not commit in the same turn as the proposal unless they already asked to commit.

4. **Commit / push only when asked.** Local edits are fine when they asked for the work. `git commit` waits until they have seen the plan or diff and said to commit. `git push origin` waits until they said to push. “Commit and push” is both, using the agreed message. Do not treat “ship on main”, a green host test, or “local commits are fine” as standing permission.

5. **Undo with git, not a second edit.** Do not invert your last turn by re-editing files. That is slow and easy to get wrong when other uncommitted work is in the same tree.
   - Already a commit: `git revert` (or drop a *local, unpushed* commit only if they asked).
   - Uncommitted, and those paths contain *only* the work to undo: `git restore -- <paths>`.
   - Uncommitted mixed with other dirty files: do **not** `git restore` (that drops the other work). One sentence in chat: cheap undo needs a commit. Ask them to checkpoint, then revert.
   Before an experiment they may want to unwind, ask for a checkpoint commit first.

### Review before commit — nominate what is worth the operator's eyes

The operator trades review time for speed and does not read every line. So do the triage: **with every commit
proposal, give a "worth your eyes" list of one to three items**, each a `file.cpp:line` and one sentence on
what could be wrong, then say plainly that the rest is mechanical. An unranked diff handed over is the same as
handing over nothing.

Prefer, in this order:

1. **What needs the operator's domain knowledge and not the agent's** — what a real ADIF consumer accepts,
   what an operator expects a key to do, whether a report format is right. The agent cannot grade these at all.
2. **What compiles, passes, and is still wrong** — encoding, units, a flash offset, a label paired with the
   wrong action.
3. **Choices made unilaterally**, where another call was reasonable.
4. **What is hard to undo** — pushed history, partition tables, key handling.

Do not nominate what the compiler, a host test or CI already proves, or mechanical refactors. Saying "the rest
is mechanical" is part of the job — it is what makes the nomination worth trusting.

Nominate the places of least confidence, not the ones that look best: a highlight reel steers attention away
from the risk. Never let "I verified it" stand in for evidence — say what was checked and give the command
that re-checks it. Name what only hardware can catch, separately from what review can.

If more than three items feel essential, the change is too big to review. Say so and propose splitting it.

### Operating notes

Working preferences the operator has stated. They live here, in the repo, because that is the only place
every agent and every operator can see them — see "Memory lives in this repo" below.

- **Build locally; do not round-trip CI for a test binary.** `idf.py build` is ~40 s against ~5 min for the
  firmware CI job, and during bench debugging that latency dominates. The merged image lands in `build/`
  (`POST_BUILD` writes `<sha>-minift8-dev.bin` alongside `MiniFT8_Merged_Auto.bin`). CI is for pre-merge
  verification, where its value is real: it builds the PR *merge commit*, which is what actually lands, and
  it runs the sidekick and host-test jobs too. Stated 2026-09-04.

- **Assume another agent may be in this worktree.** The operator sometimes runs a second Claude Code session
  on the same checkout. One worktree means one index, so a concurrent `git add -A` can sweep up files another
  session wrote seconds earlier and commit them under an unrelated message — this happened on 2026-09-08
  (`75e3ed0`). Re-check `git log -1` and `git status` immediately before committing rather than trusting
  state read earlier in the turn, and stage explicit paths rather than `-A` unless staging everything is
  genuinely the intent. If files have already been swept into someone else's pushed commit, say so and
  cross-reference that SHA; do not rewrite pushed history to reclaim them.

- **Gloss roadmap IDs in chat.** Write "B44 (the sidekick pulls its own firmware over HTTPS)", not "B44".
  The operator does not hold the ID-to-topic mapping in their head, and a bare ID makes them go look it up
  mid-conversation. Applies to backlog (`B*`) and initiative (`I*`) IDs and RFC section numbers. Test-plan
  row IDs are fine bare when the surrounding text already says what the check does. Inside `ROADMAP.md` and
  commit messages the bare ID is correct — this is about chat. Stated 2026-09-10.

- **Prose is authoritative; a diagram is derived.** Never let a fact live only in a diagram. Agents read
  the prose and may not reconstruct a constraint that exists only as an arrow, and a diagram that is the sole
  source of something goes stale without anyone noticing. Where a diagram earns its place, write it as
  **mermaid in the markdown**: GitHub renders it for humans, its source *is* the graph semantics rather than
  coordinates an agent has to re-derive, and a changed edge is a one-line diff instead of two pictures to
  compare. Do not commit SVG or raster diagrams to `docs/` — SVG costs an agent far more to read than the
  content is worth, and images diff and grep not at all. Stated 2026-09-10.

### Pre-alpha: do not carry the past

This project is **pre-alpha**. No external users, no fleet anyone else owns, no promise that anything keeps
working across a commit. Act accordingly: when something changes, change it and delete what it replaced. Do
not write migration notes, deprecation notes, or "was X, now Y" bookkeeping, and do not keep code whose only
job is accepting an older state.

The boundary matters, because over-applying this would delete the reasoning the rest of these docs are built
on:

- **Keep: why the code is the way it is now.** A constraint, an approach that failed, an API that behaves
  surprisingly. A future agent needs it to avoid re-breaking the thing. That is not legacy documentation.
- **Drop: how to get from an older state to this one.** Upgrade steps, one-time reflash rows, "an old link
  will 404", enum values retired but kept for an old file format.

The test: **would a fresh clone and a fresh flash need this line?** If only an existing install needs it, it
goes.

**The one exception is upstream.** Deltas from Wei's Mini-FT8 stay documented — the fork section of
[`../README.md`](../README.md) above the delimiter, and divergence notes in RFCs and roadmap rows. Someone
arriving from upstream needs to know what is different here, and an agent needs it to avoid "fixing" a
deliberate divergence back to upstream behaviour.

**This ends when we cut a versioned release meant for someone other than the operator**
([RELEASE_PROCESS.md](RELEASE_PROCESS.md)). From then on back-compat is a real obligation and this section
comes out.

### Memory lives in this repo

Agents with a private per-machine memory store must not use it for anything about this project. These docs
are the memory: `README.md` for working agreement and operating notes, `ROADMAP.md` for plan and history,
`TEST_PLAN.md` for what has and has not been verified on hardware, `STYLE.md` for code conventions.

**Why:** a private store is scoped to one machine and one account. It is invisible to the operator, to a
second agent on another machine, to a future operator, and to code review — so it silently diverges from the
repo and cannot be corrected by anyone who has not got it. Four such notes existed on 2026-09-10 and were
migrated here; one of them duplicated a `ROADMAP.md` row and said so in its own text, and another stated a
commit-hygiene rule that the agent holding it broke twice the same day. A rule only one agent can read is
not a rule.

**How to apply:** when the operator states a preference worth keeping, put it in the right doc in the same
turn and propose the commit. If something genuinely should not be published, say so rather than filing it
somewhere the operator cannot see — this repo is public.

### Roadmap intake (ideas)

If the operator asks to add an idea or “add to the backlog”:
1. Draft the `docs/ROADMAP.md` row in chat. Do not commit yet.
2. New items go in **Ideas**. Use **Backlog** only when they give a Done-when or say it is sequenced.
3. After they approve, commit that row on `main`. Push `origin` only when they ask to push. Never `upstream` unless they explicitly ask.

### Backlog grooming reminder

The grooming *workflow* is still TBD. Until then:

- Once per session start, check `git log -1 --format=%ci -- docs/ROADMAP.md`.
- If that commit is older than about **1–2 days**, remind the operator once that it is time to groom Now / Backlog / Ideas (promote, drop shipped Now rows, sequence or park ideas).
- While grooming, run `awk '{ print length, NR }' docs/ROADMAP.md | sort -rn | head -5` and name any outlier row. Say what's in it and let the operator decide: trim in place, extract to an RFC (the "long features get an RFC" rule), or leave it — do not refine or extract unprompted.
- Do not repeat the reminder in later turns of the same session, and do not interrupt an in-progress coding task.

### Upstream watches (session start)

Three watches, different cadences. Network fail → skip (any of them); do not interrupt an in-progress coding task.

#### `ft8_lib` pin — every session start

Scoped to the submodule pin ([RFC 0002](rfcs/0002-extract-and-boundaries.md) §6) only. General Mini-FT8 firmware drift is the separate watch below, not this one. The pin has **two** parents and both need watching. GitHub **Sync fork** on `kb2slo/ft8_lib` is off (parent is Wei, not Karlis).

**Karlis** — protocol upstream:

```bash
git ls-remote https://github.com/kgoba/ft8_lib.git refs/heads/master
git -C components/ft8_lib/vendor merge-base --is-ancestor <kgoba-sha> HEAD
```

If the submodule is missing, skip. If Karlis’s SHA **is** an ancestor of the pin, we already have him. If it is **not**, he moved.

**Wei** — the other parent. Do **not** watch `wcheng95/ft8_lib`: that repo is stale at `bb3d94d` (2026-03) and already fully absorbed into the pin. His protocol fixes land in Mini-FT8’s *in-tree* `components/ft8_lib/ft8/` instead, and reach us only by hand — his tree has no submodule, so merging `upstream/main` cannot move our pin:

```bash
git fetch upstream
for f in $(git ls-tree -r --name-only upstream/main components/ft8_lib/ft8/ | sed 's|.*/ft8/||'); do
  diff -q <(git show upstream/main:components/ft8_lib/ft8/$f) \
          <(git -C components/ft8_lib/vendor show HEAD:ft8/$f) >/dev/null 2>&1 || echo "DIFFERS $f"
done
```

Expect `message.c` to differ by our own `stpcpy_compat` removal (`f211146`); anything else is his.

**Why both.** Written 2026-09-08, after this watch missed a live stack overflow (B41): Karlis’s master has not moved since **2025-08-23**, while the one protocol fix worth having in that window came from Wei. Watching only Karlis watches the quiet parent.

Then draft a Backlog row in chat. Do not commit it yet. Do not merge, bump the submodule, or open a kgoba PR in that turn. Done-when: written take (sync now / wait / drop). Sync uses RFC 0002 §6 and is a separate Now. Goldens gate the pin. If a row for that SHA already exists, remind; do not duplicate.

#### Mini-FT8 upstream (`wcheng95/Mini-FT8`) — at least weekly

**Last checked:** 2026-09-12 — 11 commits on `upstream/main` not in our `main`. `491e757` ("Fix telemetry decode buffer overflow") is already ported by hand as B41; the other 10 are upstream's own CI/test-tooling commits for an "RX-1A" reference-dump/regression harness, nothing that looks portable here. No row drafted.

General drift, not just the `ft8_lib` files this repo shares with Wei's tree (that's the watch above). Feeds [I12](ROADMAP.md) ("Mine public Mini-FT8 forks"), which is Ideas, not a standing workflow — this session-start check is what keeps it from going stale between the occasional full sweep (the kind B39 did once by hand).

```bash
git fetch upstream
git log --oneline main..upstream/main
```

If the date above is more than **7 days** old, run it even though it repeats what might be a no-op week to week, and update the date (and the one-line finding) regardless of outcome — that line is the only record of when this last actually ran. Something worth taking → draft an Ideas/Backlog row in chat citing the commit, same discipline as the pin watch: do not merge, do not commit the date bump silently — say what you found first.

#### QMX panadapter project (`SteffenLav/qmx-panadapter`) — at least weekly

**Last checked:** 2026-09-12 — large overlap with several open roadmap items, worth the operator's attention rather than an agent's judgment call. This is a shipping FT8/FT4 station on the **M5Stack Tab5 (ESP32-P4)**: QMX as a USB host (UAC audio + CAT), on-device FT8/FT4 decode and TX, a browser-streamed panadapter UI, ADIF logging with upload to QRZ/eQSL/LoTW/Cloudlog/Wavelog, POTA/SOTA activation logging, and live PSK Reporter/DX-cluster/POTA spotting. That bears directly on [B26](ROADMAP.md) (does a P4 host a QMX at all — this project says yes, in production), [I27](ROADMAP.md) (P4 headless design), [I26](ROADMAP.md) (decode headroom on P4), and [I28](ROADMAP.md)/[B48](ROADMAP.md) (headless web app, QRZ/PSKReporter from the browser). Not yet turned into a roadmap row — flagged for the operator to decide what, if anything, to look at more closely.

Check for anything new via the repo's own version history (`docs/version-history.md` in that repo) rather than a git remote — it is not a fork parent, just a project worth watching for ideas:

```bash
gh repo view SteffenLav/qmx-panadapter --json pushedAt,description
```

If the date above is more than **7 days** old, look at what changed since the last check (the linked version history is newest-last) and update the date and one-line finding regardless of outcome. Insight, not code — do not port or copy from it without the operator's own read of the license and the code.

### Local IDF build / flash

**Local build first. CI is not an iteration loop.** Iterate with local `idf.py build` — roughly 15–40 s incremental — and hand the operator the merged image straight from `build/` (`POST_BUILD` writes `<sha>-minift8-dev.bin` beside `MiniFT8_Merged_Auto.bin`). Do **not** push a branch and wait on a CI artifact merely to produce a test binary: the firmware job alone is ~4–5 minutes, and during bench debugging that latency dominates the session. CI is for **pre-merge verification** — it builds the PR *merge commit* (what actually lands, not the branch tip) and runs the sidekick and host-test jobs that a local ADV build does not. Pushing is also gated by **Chat gates** below: it waits until the operator asks. Do not push in order to get a build.

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
idf.py build flash
```

Do not split into separate `build` then `flash` unless the user asks.

**`monitor`:** The Cardputer USB-C is either ESP USB Serial/JTAG (Mac ↔ firmware logs) **or** USB host for the radio (QMX / QDX) — not both. So `idf.py … flash monitor` works with the **radio unplugged** from USB-C. Live logs while the radio is connected need the console UART on **G4 (TX) / G5 (RX)** (USB–TTL adapter), not PORTA/Grove (`G1`/`G2`, GPS/CAT). That G4/G5 path is off when `GNSS_LoRa:ON`.

Loader stubs (`AGENTS.md`, `CLAUDE.md`, `.cursor/rules/`, and similar) exist only so a given tool loads this agreement. They point here. Do not duplicate the plan in those files. `AGENTS.md` states the rule; adding a tool means adding a stub, never moving rules into it.
