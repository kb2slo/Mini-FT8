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
`git checkout continuous` and the published `minift8-continuous.bin` are the same commit.

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

None have been cut under this process yet. Doing so is also the event that ends pre-alpha — see
**Pre-alpha: do not carry the past** in [README.md](README.md) — because a version aimed at anyone but the
operator is the first time back-compat becomes an obligation rather than a choice.

There is no automation for the decision, only for the build. The whole procedure:

```bash
git tag v3.0.0
git push origin v3.0.0
```

`release` then builds the tag, names the binary `MiniFT8-v3.0.0-Merged.bin`, and publishes with
`generate_release_notes: true`. Version tags are immovable by convention — re-pointing one would break the
one guarantee a version number carries. To fix a bad release, cut the next one.

Use lowercase `v`. This fork's tags are `v0.1` … `v1.1.2`; Wei's upstream tags are uppercase (`V1.1` …
`V2.1b`) and are in this repo's history too. Git refs are case-sensitive and the trigger glob is `v*`, so
upstream's tags do not fire CI and ours do — a `V3.0.0` would silently build nothing.

## What a version tag does **not** set

This is the part that surprises people, and the part to read before cutting a "3.0". The tag, the asset name,
the version on the operator's screen, and the build kind are four independent things, and CI derives only the
second from the first.

| Thing | Where it comes from | Who updates it |
| --- | --- | --- |
| Git tag | you, by hand | you |
| Release asset name | the tag, in `ci.yml` | CI |
| `MINIFT8_PRODUCT_VER` — **what the device displays** | hardcoded `"2.1d"`, [`tools/gen_build_identity.cmake:13`](../tools/gen_build_identity.cmake) | nobody |
| `MINIFT8_BUILD_KIND` — `dev` or `rel` | defaults `dev`, [`CMakeLists.txt:8`](../CMakeLists.txt) | nobody |

Both bottom rows are inert today. Nothing in `ci.yml` passes `-DMINIFT8_BUILD_KIND=rel`, so the `rel` branch
in `gen_build_identity.cmake` has never been taken by a published build, and `MINIFT8_PRODUCT_VER` has never
been rewritten by anything but a hand edit.

The consequence, stated plainly because it is easy to ship without noticing: **tagging `v3.0.0` today produces
a device whose splash screen reads `Mini-FT8 V2.1d` and whose status title reads `Mini-FT8 V2.1d. dev <sha>`**
(`main/main.cpp:479` and `main/main.cpp:1832`). Note that `-dev` in a build filename is this *build kind*, not
a release tag — two unrelated meanings of the same word, and the reason the rolling asset carries neither.

So the manual half of a versioned release, until the open design below is settled, is: bump
`MINIFT8_PRODUCT_VER`, decide whether the build should claim `rel`, commit that, *then* tag.

### Open: the runtime must prove which kind of build it is

**Undecided; an agent must not settle it in passing.** The requirement is that looking at a running device
tells you without ambiguity whether it is a versioned release or a rolling build — today it always claims
`dev` and always claims `2.1d`, so it proves nothing either way.

The pieces already exist and are simply unwired: `MINIFT8_BUILD_KIND` has a `rel` branch nothing selects, and
`gen_build_identity.cmake` already computes a dirty flag. A scheme worth considering, recorded so the
discussion starts somewhere rather than from scratch:

- CI's `release` job passes `-DMINIFT8_BUILD_KIND=rel`; every other path stays `dev`.
- A `rel` build **fails to configure** unless HEAD is exactly a `v*` tag and the tree is clean. That is what
  makes `rel` on screen a claim rather than a label — it becomes impossible to produce by accident.
- `MINIFT8_PRODUCT_VER` derives from that tag for `rel`, so a release cannot display a number that disagrees
  with the tag it was cut from. The alternative is to keep it hand-edited and have CI fail the tag build when
  the two disagree — less automatic, but it keeps the version a deliberate human act.

Watch the display width either way: the status title is a `char[40]` holding both the product version and the
build line (`main/main.cpp:1832`), so a `3.0.0-14-gabc1234`-style string has to fit or be truncated
deliberately.

Whether versioned releases should be cut from `main` or from their own `release/*` branch is open too, and
deliberately **not** blocking: the `release` job triggers on `refs/tags/v*` regardless of which branch the tag
sits on, so adopting release branches later changes nothing in CI. Branches earn their place only once a fix
has to ship against an old version while `main` has moved on, which pre-alpha does not have.

## If the project is renamed or de-forked

Recorded because it is a stated direction, not a hypothetical. Renaming touches more than strings, and one of
these has a field consequence rather than a cosmetic one.

**The one that is not cosmetic:** the ADV decides whether an attached sidekick is *ours* by `strcmp` of the
device's `esp_app_desc_t` project name against `kExpectedProjectName[] = "sidekick"`
([`components/sidekick_flasher/sidekick_flasher.cpp:96`](../components/sidekick_flasher/sidekick_flasher.cpp)),
set by `project(sidekick)` in [`sidekick/CMakeLists.txt:34`](../sidekick/CMakeLists.txt). Change that project
name and **every sidekick already in the field reads as a foreign device** — `H` offers `2: OVERWRITE it`
instead of an update. That is [TEST_PLAN.md](TEST_PLAN.md) S3.5 working as designed, aimed at the wrong
target. A rename must either leave `project(sidekick)` alone or ship knowing every companion needs a
deliberate overwrite.

The rest is naming, but all of it is operator-visible:

- Asset and tag names in `ci.yml` (`MiniFT8-<tag>-Merged.bin`, `minift8-continuous.bin`).
- `MERGED_BIN_NAME` in `gen_build_identity.cmake` — the hash-first name M5Launcher truncates (D12).
- The `MINIFT8_*` macro names, and `project(mini_ft8)` in `CMakeLists.txt:6`.
- The mDNS hostname `minift8.local` (RFC 0001 §5.0), which is in operators' browser history.
- Clone URLs and release links in `README.md` and `docs/`. GitHub redirects a renamed repo, so these keep
  working — but redirects are not a plan.

**The version number is the de-fork.** `2.1d` is a continuation of Wei's `V2.1b`, so the number on the screen
still sits in upstream's numbering. Whatever this fork calls itself next, choosing its own version line is the
moment it stops being a fork by version as well as by name — which is why "3.0" is a product decision and not
a `MINIFT8_PRODUCT_VER` edit.
