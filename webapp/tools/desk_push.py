#!/usr/bin/env python3
"""Desk-push webapp/app onto a sidekick (RFC 0004 §4 bundle API).

Regenerates the manifest, resolves the host once (mDNS is slow per-request),
waits for you to press the AtomS3 Lite button, claims GET /api/pairing-token,
then begin / put / commit.

Close any browser tab on minift8.local first — pairing.js also claims the
token, and the first successful GET wins.

Usage:
  python3 webapp/tools/desk_push.py
  python3 webapp/tools/desk_push.py --host http://192.168.50.128
  python3 webapp/tools/desk_push.py --token HEX…   # skip the button
"""

from __future__ import annotations

import argparse
import json
import socket
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Optional

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "webapp" / "app"
MANIFEST = APP / "manifest.json"
HEADER = "X-MiniFT8-Token"
DEFAULT_HOST = "http://minift8.local"
POLL_S = 1.0
WAIT_S = 120.0


def die(msg: str, code: int = 1) -> None:
    print(f"error: {msg}", file=sys.stderr)
    raise SystemExit(code)


def gen_manifest() -> dict:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import gen_manifest as gm

    doc = gm.build()
    text = json.dumps(doc, indent=2) + "\n"
    MANIFEST.write_text(text)
    print(f"wrote {MANIFEST.relative_to(ROOT)} ({len(doc['assets'])} assets)")
    return doc


def url(host: str, path: str) -> str:
    return host.rstrip("/") + path


def resolve_base(host: str) -> str:
    """Resolve hostname once; return base URL using the IP (avoids slow mDNS per request)."""
    raw = host.rstrip("/")
    if "://" not in raw:
        raw = "http://" + raw
    parts = urllib.parse.urlsplit(raw)
    scheme = parts.scheme or "http"
    hostname = parts.hostname
    if not hostname:
        die(f"bad --host: {host}")
    port = parts.port
    if port is None:
        port = 443 if scheme == "https" else 80

    # Already an IP — nothing to do.
    try:
        socket.inet_pton(socket.AF_INET, hostname)
        return urllib.parse.urlunsplit((scheme, parts.netloc, "", "", "")).rstrip("/")
    except OSError:
        pass
    try:
        socket.inet_pton(socket.AF_INET6, hostname)
        return urllib.parse.urlunsplit((scheme, parts.netloc, "", "", "")).rstrip("/")
    except OSError:
        pass

    print(f"resolving {hostname}…")
    t0 = time.monotonic()
    try:
        infos = socket.getaddrinfo(hostname, port, type=socket.SOCK_STREAM)
    except socket.gaierror as e:
        die(f"DNS/mDNS failed for {hostname}: {e}")
    if not infos:
        die(f"no addresses for {hostname}")

    # Prefer IPv4 on the LAN (sidekick station mode).
    ip = None
    for fam, _, _, _, sockaddr in infos:
        if fam == socket.AF_INET:
            ip = sockaddr[0]
            break
    if ip is None:
        ip = infos[0][4][0]
        if ":" in ip:
            ip = f"[{ip}]"

    elapsed = time.monotonic() - t0
    netloc = ip if (scheme == "http" and port == 80) or (scheme == "https" and port == 443) else f"{ip}:{port}"
    base = f"{scheme}://{netloc}"
    print(f"resolved {hostname} → {base} ({elapsed:.2f}s)")
    return base


def http(
    method: str,
    full: str,
    *,
    token: Optional[str] = None,
    body: Optional[bytes] = None,
    content_type: Optional[str] = None,
    timeout: float = 30.0,
) -> tuple[int, bytes]:
    headers: dict[str, str] = {}
    if token:
        headers[HEADER] = token
    if content_type:
        headers["Content-Type"] = content_type
    req = urllib.request.Request(full, data=body, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, resp.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()
    except urllib.error.URLError as e:
        die(f"{method} {full}: {e.reason}")
    return 0, b""  # unreachable; keeps type checkers happy


def wait_for_token(host: str, wait_s: float) -> str:
    print()
    print("Close browser tabs on the sidekick (they race for the token).")
    print(f"Press the sidekick button now — polling for up to {int(wait_s)} s…")
    print()
    deadline = time.monotonic() + wait_s
    last_tick = -1
    while time.monotonic() < deadline:
        code, body = http("GET", url(host, "/api/pairing-token"), timeout=5.0)
        if code == 200:
            try:
                tok = json.loads(body.decode()).get("token", "").strip()
            except json.JSONDecodeError:
                tok = ""
            if tok:
                print(f"claimed token ({len(tok)} hex chars)")
                return tok
            die("200 from /api/pairing-token but no token field")
        if code not in (404,):
            print(f"  GET /api/pairing-token → {code} (retrying)")
        remaining = int(deadline - time.monotonic())
        if remaining != last_tick and (remaining % 10 == 0 or remaining < 5):
            print(f"  still waiting… {remaining}s left — press the button")
            last_tick = remaining
        time.sleep(POLL_S)
    die("timed out waiting for pairing button / token")


def push(host: str, token: str, doc: dict) -> None:
    man_bytes = MANIFEST.read_bytes()
    code, body = http(
        "POST",
        url(host, "/api/bundle/begin"),
        token=token,
        body=man_bytes,
        content_type="application/json",
    )
    if code != 200:
        die(f"begin → {code}: {body.decode(errors='replace')}")
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
        if code != 200:
            die(f"put {path} → {code}: {body.decode(errors='replace')}")
        print(f"put {path} ({len(data)} bytes)")

    code, body = http("POST", url(host, "/api/bundle/commit"), token=token)
    if code != 200:
        die(f"commit → {code}: {body.decode(errors='replace')}")
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
    ap = argparse.ArgumentParser(
        description="Desk-push webapp/app to a sidekick via the pairing button."
    )
    ap.add_argument(
        "--host",
        default=DEFAULT_HOST,
        help=f"sidekick base URL (default {DEFAULT_HOST})",
    )
    ap.add_argument(
        "--token",
        default="",
        help="pairing token hex; omit to claim via button",
    )
    ap.add_argument(
        "--wait",
        type=float,
        default=WAIT_S,
        help=f"seconds to wait for the button (default {int(WAIT_S)})",
    )
    ap.add_argument(
        "--no-regen",
        action="store_true",
        help="use existing manifest.json without regenerating",
    )
    args = ap.parse_args()

    host = resolve_base(args.host)
    code, _ = http("GET", url(host, "/"), timeout=5.0)
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
    if not token:
        token = wait_for_token(host, args.wait)
    else:
        print("using --token from CLI")

    push(host, token, doc)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
