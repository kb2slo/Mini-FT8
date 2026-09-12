#!/usr/bin/env python3
"""Cut a versioned Mini-FT8 release.

Creates and pushes an annotated `v<version>` tag. CI does the rest: the
firmware job sees a tag build, builds with MINIFT8_BUILD_KIND=rel and the
version taken from the tag, and the release job publishes the binary.

This script edits no source file. The version the device displays is derived
from the tag by tools/gen_build_identity.cmake, so there is no number to bump
and no bump commit for the tag to land on the wrong side of.

Everything here is a precondition check. The guarantee that a `rel` build is
clean, versioned and from CI lives in the build, not in this script -- see
docs/RELEASE_PROCESS.md. Skipping this script cannot forge a release; it can
only produce a tag that CI then refuses to build.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys

SEMVER = re.compile(r"^\d+\.\d+\.\d+$")
REMOTE = "origin"
BRANCH = "main"


class Abort(Exception):
    pass


def run(*args: str, check: bool = True) -> str:
    proc = subprocess.run(args, capture_output=True, text=True)
    if check and proc.returncode != 0:
        raise Abort(f"{' '.join(args)}\n{proc.stderr.strip()}")
    return proc.stdout.strip()


def require_clean_tree() -> None:
    if run("git", "status", "--porcelain"):
        raise Abort(
            "working tree is dirty. A release must name a commit that contains "
            "exactly what was built; commit or stash first."
        )


def require_on_branch() -> str:
    branch = run("git", "rev-parse", "--abbrev-ref", "HEAD")
    if branch != BRANCH:
        raise Abort(f"on branch '{branch}', expected '{BRANCH}'.")
    return branch


def require_up_to_date() -> str:
    run("git", "fetch", REMOTE, BRANCH, "--quiet")
    local = run("git", "rev-parse", "HEAD")
    remote = run("git", "rev-parse", f"{REMOTE}/{BRANCH}")
    if local != remote:
        behind = run("git", "rev-list", "--count", f"HEAD..{REMOTE}/{BRANCH}")
        ahead = run("git", "rev-list", "--count", f"{REMOTE}/{BRANCH}..HEAD")
        raise Abort(
            f"HEAD does not match {REMOTE}/{BRANCH} ({ahead} ahead, {behind} "
            "behind). Tagging a commit that is not what main points at is how a "
            "release ends up containing something nobody reviewed."
        )
    return local


def require_tag_free(tag: str) -> None:
    if run("git", "tag", "--list", tag):
        raise Abort(f"tag '{tag}' already exists locally.")
    if run("git", "ls-remote", "--tags", REMOTE, f"refs/tags/{tag}"):
        raise Abort(
            f"tag '{tag}' already exists on {REMOTE}. Version tags are "
            "immovable by convention -- cut the next version instead."
        )


def require_ci_green(sha: str) -> None:
    """Refuse a release from a commit CI has not passed.

    Absence of a run is treated as failure, not as permission: a release built
    from an unverified commit is the exact thing this check exists to stop.
    """
    try:
        raw = run(
            "gh", "run", "list", "--commit", sha, "--workflow", "CI",
            "--json", "conclusion,status,url", "--limit", "20",
        )
    except Abort as exc:
        raise Abort(
            f"could not ask GitHub about CI for {sha[:7]} (is `gh` installed "
            f"and authenticated?).\n{exc}\nRe-run with --skip-ci-check if you "
            "have confirmed the commit another way."
        )

    runs = json.loads(raw or "[]")
    if not runs:
        raise Abort(
            f"no CI run found for {sha[:7]}. Push it and let CI finish before "
            "tagging, or re-run with --skip-ci-check."
        )
    unfinished = [r for r in runs if r.get("status") != "completed"]
    if unfinished:
        raise Abort(f"CI is still running for {sha[:7]}: {unfinished[0]['url']}")
    failed = [r for r in runs if r.get("conclusion") != "success"]
    if failed:
        raise Abort(
            f"CI is not green for {sha[:7]}: {failed[0]['conclusion']} "
            f"{failed[0]['url']}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", help="release version, e.g. 3.0.0")
    parser.add_argument("--yes", action="store_true", help="skip the confirmation")
    parser.add_argument(
        "--skip-ci-check", action="store_true",
        help="do not require a green CI run on the commit being tagged",
    )
    args = parser.parse_args()

    version = args.version.lstrip("v")
    if not SEMVER.match(version):
        print(f"error: '{args.version}' is not MAJOR.MINOR.PATCH", file=sys.stderr)
        return 2
    tag = f"v{version}"

    try:
        require_clean_tree()
        branch = require_on_branch()
        sha = require_up_to_date()
        require_tag_free(tag)
        if args.skip_ci_check:
            print("! skipping the CI check")
        else:
            require_ci_green(sha)
    except Abort as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    subject = run("git", "log", "-1", "--format=%s", sha)
    print(f"  tag     {tag}")
    print(f"  commit  {sha[:7]}  {subject}")
    print(f"  branch  {branch}")
    print(f"  builds  MINIFT8_BUILD_KIND=rel, version {version} from the tag")
    print(f"  device  will display 'Mini-FT8 {version}'")

    if not args.yes:
        if input("\npush this tag? [y/N] ").strip().lower() not in ("y", "yes"):
            print("aborted")
            return 1

    try:
        run("git", "tag", "-a", tag, "-m", f"Mini-FT8 {version}")
        run("git", "push", REMOTE, tag)
    except Abort as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    slug = run("git", "config", "--get", f"remote.{REMOTE}.url")
    slug = re.sub(r"^.*github\.com[:/]|\.git$", "", slug)
    print(f"\npushed {tag}. Release will appear at:")
    print(f"  https://github.com/{slug}/releases/tag/{tag}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
