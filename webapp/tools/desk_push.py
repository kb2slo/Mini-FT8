#!/usr/bin/env python3
"""Desk-push webapp/app onto a sidekick (RFC 0004 §4 bundle API).

Regenerates the manifest, resolves the host once (mDNS is slow per-request),
then pushes using a cached pairing token if one is on disk (see sidekick.py)
— the button is only needed the first time, or if the cached token gets
rejected (401), or with --forget-token.

Close any browser tab on minift8.local first if the button *is* needed —
pairing.js also claims the token, and the first successful GET wins.

Usage:
  python3 webapp/tools/desk_push.py
  python3 webapp/tools/desk_push.py --host http://192.168.50.128
  python3 webapp/tools/desk_push.py --token HEX…   # skip cache + button
  python3 webapp/tools/desk_push.py --forget-token # clear cache, re-pair
"""

from __future__ import annotations

import json
import sys
import urllib.parse
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sidekick import (  # noqa: E402
    ROOT,
    AuthError,
    SidekickError,
    clear_token,
    die,
    http,
    resolve_base,
    url,
    with_token_retry,
)

APP = ROOT / "webapp" / "app"
MANIFEST = APP / "manifest.json"
DEFAULT_HOST = "http://minift8.local"
WAIT_S = 120.0


def gen_manifest() -> dict:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import gen_manifest as gm

    doc = gm.build()
    text = json.dumps(doc, indent=2) + "\n"
    MANIFEST.write_text(text)
    print(f"wrote {MANIFEST.relative_to(ROOT)} ({len(doc['assets'])} assets)")
    return doc


def fnv1a(data: bytes) -> str:
    """Matches app.html's own fnv1a() exactly -- same algorithm over the same
    bytes, so its printed hash for app.html can be eyeballed directly against
    the "App: ..." line Settings shows after a push, without needing
    crypto.subtle (unavailable on the sidekick's plain-HTTP origin) or a
    server-side version endpoint."""
    h = 0x811C9DC5
    for b in data:
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return f"{h:08x}"


def _check(code: int, body: bytes, what: str) -> None:
    if code == 401:
        raise AuthError(f"{what} → 401")
    if code != 200:
        die(f"{what} → {code}: {body.decode(errors='replace')}")


def push(host: str, token: str, doc: dict) -> None:
    man_bytes = MANIFEST.read_bytes()
    code, body = http(
        "POST",
        url(host, "/api/bundle/begin"),
        token=token,
        body=man_bytes,
        content_type="application/json",
    )
    _check(code, body, "begin")
    print("begin ok")

    for asset in doc["assets"]:
        path = asset["path"]
        data = (APP / path).read_bytes()
        if len(data) != asset["size"]:
            die(f"{path}: size {len(data)} != manifest {asset['size']} — re-run without --no-regen")
        code, body = http(
            "PUT",
            url(host, f"/api/bundle/file?path={urllib.parse.quote(path)}"),
            token=token,
            body=data,
            timeout=60.0,
        )
        _check(code, body, f"put {path}")
        suffix = f", fnv1a {fnv1a(data)}" if path == "app.html" else ""
        print(f"put {path} ({len(data)} bytes{suffix})")

    code, body = http("POST", url(host, "/api/bundle/commit"), token=token)
    _check(code, body, "commit")
    print("commit ok")

    land = "installed.html"
    for a in doc["assets"]:
        if a["path"] == "app.html":
            land = "app.html"
            break
    code, _ = http("GET", url(host, f"/{land}"), timeout=10.0)
    print(f"GET /{land} → {code}")
    if code != 200:
        die(f"/{land} not serving after commit")
    print(f"done — open {url(host, '/' + land)}")


def main() -> int:
    import argparse

    ap = argparse.ArgumentParser(
        description="Desk-push webapp/app to a sidekick via a cached pairing token or the button."
    )
    ap.add_argument("--host", default=DEFAULT_HOST, help=f"sidekick base URL (default {DEFAULT_HOST})")
    ap.add_argument("--token", default="", help="pairing token hex; skips the cache and the button")
    ap.add_argument("--wait", type=float, default=WAIT_S, help=f"seconds to wait for the button (default {int(WAIT_S)})")
    ap.add_argument("--no-regen", action="store_true", help="use existing manifest.json without regenerating")
    ap.add_argument("--forget-token", action="store_true", help="clear the cached token before running (forces the button)")
    args = ap.parse_args()

    if args.forget_token:
        clear_token()
        print("cleared cached token")

    try:
        host = resolve_base(args.host)
        code, _ = http("GET", url(host, "/"), timeout=5.0)
    except SidekickError as e:
        die(str(e))
    if code != 200:
        die(f"sidekick not reachable at {host}/ (HTTP {code})")
    print(f"sidekick ok at {host}")

    if args.no_regen:
        if not MANIFEST.is_file():
            die(f"missing {MANIFEST}")
        doc = json.loads(MANIFEST.read_text())
        print(f"using existing manifest ({len(doc.get('assets', []))} assets)")
    else:
        doc = gen_manifest()

    token = args.token.strip()
    try:
        if token:
            print("using --token from CLI (not cached)")
            push(host, token, doc)
        else:
            with_token_retry(host, args.wait, lambda tok: push(host, tok, doc))
    except SidekickError as e:
        die(str(e))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
