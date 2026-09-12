# Release process (kb2slo fork)

How a build becomes something an operator can download and flash. Companion to [README.md](README.md): that
file is the working agreement, this one is what CI publishes and what a human must still do by hand.

Two paths, both defined in [`.github/workflows/ci.yml`](../.github/workflows/ci.yml). Nothing else publishes.

| Path | Trigger | Job | Tag | Asset |
| --- | --- | --- | --- | --- |
| Rolling | push to `main` that can change the binary | `release-main` | `continuous`, force-moved every run | `YYYYMMDD-minift8-<commit>.bin` |
| Versioned | push of a tag matching `v*` | `release` | the tag you pushed, immovable | `MiniFT8-<tag>-Merged.bin` |

Both publish the same merged image — flash at `0x0`, 8 MB, custom FATFS partition table — under different
names. The firmware artifact is skipped entirely for docs-only commits (the `changes` path filter, D7), so a
roadmap-only merge neither rebuilds nor re-tags.

## The rolling `continuous` release

**The invariant: the `continuous` tag is the tip of `main`.** Not a description of it — the actual ref, so
`git checkout continuous` and the published binary are the same commit.

That invariant needs the explicit `git push --force origin <sha>:refs/tags/continuous` step in `release-main`.
Keep that step, and keep knowing why: GitHub's release API **ignores `target_commitish` once a tag of that
name exists** — it is honoured only when the tag is created. A workflow that merely passes `target_commitish`
therefore places the tag on its first run and never moves it again, while still replacing the release body
and assets correctly every time. The failure is silent and reads backwards — the download is current, the
release page quotes the right commit, and only the ref is stale — so nothing surfaces it. Delete the push
step as redundant and the tag quietly freezes again.

Corollaries an agent should not re-derive:

- The release is **updated in place**, not recreated, so GitHub keeps its original `publishedAt`. The date on
  the release page is when the release object was first made and will not advance. Judge freshness by the
  commit in the body or by the tag, never by that date.
- `prerelease: true` and `make_latest: false` are deliberate: they keep the rolling build from taking the
  "Latest release" badge away from the newest real version. This is also why the tag is not called `latest`.
- **The asset name changes every merge, on purpose.** `YYYYMMDD-minift8-<commit>.bin` still identifies itself
  after download, where several same-named files in one Downloads folder do not — the same reasoning as the
  hash-first build names (D12). The date is the *commit* date in UTC, so rebuilding a commit reproduces the
  name. The cost is that there is no stable download URL; a script should resolve the asset from the release
  rather than hardcode a filename.
- Because the name changes, `release-main` **deletes the release's existing assets** before uploading. Without
  that they accumulate on one release forever. The release object itself is never deleted.

## Cutting a versioned release

```bash
python3 tools/cut_release.py 3.0.0
```

That is the whole procedure. The script creates and pushes the annotated `v3.0.0` tag; CI does the rest.
It **edits no source file** — the version the device shows is derived from the tag, so there is no number to
bump and therefore no bump commit for the tag to land on the wrong side of, which is the classic way a
hand-run release ends up tagging the wrong thing.

It refuses, rather than warns, on: a dirty tree, not being on `main`, `HEAD` disagreeing with
`origin/main`, a tag that already exists locally or on the remote, and a commit CI has not run green.
**A missing CI run counts as a failure, not as permission** (`--skip-ci-check` overrides deliberately, and
says so on stdout). Then it prints the tag, commit, what the device will display, and asks before pushing;
`--yes` skips the prompt.

Cutting a release is also the event that ends pre-alpha — **Pre-alpha: do not carry the past** in
[README.md](README.md) — because a version aimed at anyone but the operator is the first time back-compat
becomes an obligation rather than a choice. None have been cut under this process yet.

Version tags are immovable by convention: re-pointing one breaks the single guarantee a version number
carries. To fix a bad release, cut the next one.

Use lowercase `v`. This fork's tags are `v0.1` … `v1.1.2`; Wei's upstream tags are uppercase (`V1.1` …
`V2.1b`) and are in this repo's history too. Git refs are case-sensitive and the trigger glob is `v*`, so
upstream's tags do not fire CI and ours do — a `V3.0.0` would silently build nothing.

## The runtime proves which kind of build it is

**A release states a version and nothing else; a dev build states its kind and commit and claims no version.**
The presence or absence of a version number is the signal:

| Build | Device displays | Merged image |
| --- | --- | --- |
| `v3.0.0` tag, via CI | `Mini-FT8 3.0.0` | `<sha>-minift8-3.0.0.bin` |
| anything else | `Mini-FT8 dev <sha>` (`*` if dirty) | `<sha>-minift8-dev.bin` |

This is a guarantee rather than a label, because `rel` is **gated in the build, not in the release script**.
[`tools/gen_build_identity.cmake`](../tools/gen_build_identity.cmake) hard-fails a `rel` build unless all
three hold:

1. **It is running in CI** (`CI` set in the environment). The workflow passes `-e CI=true` explicitly, since
   the ESP-IDF build container does not inherit the runner's environment.
2. **A version was handed to it** (`MINIFT8_RELEASE_VERSION`), which CI takes from `github.ref_name` on a tag
   push — so the number on screen cannot disagree with the tag it was cut from.
3. **The tree is clean**, so the version names a commit that actually contains what was built.

It fails loudly on each rather than quietly falling back to `dev`: a release that silently built as a dev
image is the exact outcome the gate exists to prevent. Bypassing the script cannot forge a release — it can
only produce a tag that CI then refuses to build.

**This stops an accident, not an adversary.** Anyone can set `CI=1` on their own machine. The point is that
no ordinary desk build, and no agent running `idf.py build`, can produce something claiming to be 3.0.0.

Note `-dev` in a build filename is the *build kind*, not a release tag — two unrelated meanings of the same
word, which is why the rolling release asset carries neither.

One deliberate consequence of deriving the version from the tag: **a dev build claims no product version at
all.** A version that is identical on every build, released or not, tells you nothing about what you are
looking at — which is the job this line has to do.

## If the project is renamed or de-forked

Recorded because it is a stated direction, not a hypothetical. Renaming touches more than strings, and one of
these has a field consequence rather than a cosmetic one.

**The one that is not cosmetic:** the ADV decides whether an attached sidekick is *ours* by `strcmp` of the
device's `esp_app_desc_t` project name against `kExpectedProjectName[] = "sidekick"`
([`components/sidekick_flasher/sidekick_flasher.cpp`](../components/sidekick_flasher/sidekick_flasher.cpp)),
set by `project(sidekick)` in [`sidekick/CMakeLists.txt`](../sidekick/CMakeLists.txt). Change that project
name and **every sidekick already in the field reads as a foreign device** — `H` offers `2: OVERWRITE it`
instead of an update. That is [TEST_PLAN.md](TEST_PLAN.md) S3.5 working as designed, aimed at the wrong
target. A rename must either leave `project(sidekick)` alone or ship knowing every companion needs a
deliberate overwrite.

The rest is naming, but all of it is operator-visible:

- Asset and tag names in `ci.yml` (`MiniFT8-<tag>-Merged.bin`, `YYYYMMDD-minift8-<commit>.bin`).
- `MERGED_BIN_NAME` in `gen_build_identity.cmake` — the hash-first name M5Launcher truncates (D12).
- The `MINIFT8_*` macro names, and `project(mini_ft8)` in `CMakeLists.txt`.
- The mDNS hostname `minift8.local` (RFC 0001 §5.0), which is in operators' browser history.
- Clone URLs and release links in `README.md` and `docs/`. GitHub redirects a renamed repo, so these keep
  working — but redirects are not a plan.

**The version number is the de-fork.** The `2.1d` this fork used to display was a continuation of Wei's
`V2.1b` — upstream's numbering, on our screen. Nothing displays a product version now until a release is cut,
so the first `v*` tag chooses this project's own version line from scratch. That makes the first number a
product decision about where this tree starts counting, not an edit to a constant.
