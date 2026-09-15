#!/usr/bin/env python3
"""Shared "talk to a real sidekick" plumbing for webapp/tools/*.py scripts
(RFC 0004 §7's pairing token, §4's bundle API, and whatever hardware-test
scripts follow). Not a CLI on its own.

Token caching: the pairing token is scoped to one physical sidekick and does
not rotate on retrieval (RFC 0004 §7), so once a script has claimed it via
the button, every later script run can reuse it from disk instead of making
the operator walk over and press the button again. Cached at TOKEN_CACHE,
outside version control (.gitignore) since it is a live credential that
guards transmit control on a licensed radio -- treat it like one, not like
build output.
"""

from __future__ import annotations

import json
import socket
import stat
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Callable, Optional, TypeVar

ROOT = Path(__file__).resolve().parents[2]
TOKEN_CACHE = Path(__file__).resolve().parent / ".sidekick_token"
HEADER = "X-MiniFT8-Token"
POLL_S = 1.0

T = TypeVar("T")


class SidekickError(Exception):
    """Base for errors talking to a real sidekick -- network failure, a
    non-200/401 the caller didn't specifically handle, etc. Deliberately not
    SystemExit: a one-shot CLI script can catch this and die() with its own
    message, but a future test runner wants to catch it per-test and keep
    going rather than have one helper call kill the whole run."""


class AuthError(SidekickError):
    """The device returned 401 -- the token in hand is wrong, expired, or was
    never claimed. Distinct from SidekickError so with_token_retry() can
    catch exactly this and only this before falling back to the button."""


def resolve_base(host: str) -> str:
    """Resolve hostname once; return base URL using the IP (avoids slow mDNS
    per request). Raises SidekickError on failure rather than exiting, so a
    caller (or a future test harness) can decide how to report it."""
    raw = host.rstrip("/")
    if "://" not in raw:
        raw = "http://" + raw
    parts = urllib.parse.urlsplit(raw)
    scheme = parts.scheme or "http"
    hostname = parts.hostname
    if not hostname:
        raise SidekickError(f"bad host: {host}")
    port = parts.port
    if port is None:
        port = 443 if scheme == "https" else 80

    # Already an IP — nothing to do.
    for family in (socket.AF_INET, socket.AF_INET6):
        try:
            socket.inet_pton(family, hostname)
            return urllib.parse.urlunsplit((scheme, parts.netloc, "", "", "")).rstrip("/")
        except OSError:
            pass

    print(f"resolving {hostname}…")
    t0 = time.monotonic()
    try:
        infos = socket.getaddrinfo(hostname, port, type=socket.SOCK_STREAM)
    except socket.gaierror as e:
        raise SidekickError(f"DNS/mDNS failed for {hostname}: {e}") from e
    if not infos:
        raise SidekickError(f"no addresses for {hostname}")

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


def url(host: str, path: str) -> str:
    return host.rstrip("/") + path


def http(
    method: str,
    full: str,
    *,
    token: Optional[str] = None,
    body: Optional[bytes] = None,
    content_type: Optional[str] = None,
    timeout: float = 30.0,
) -> tuple[int, bytes]:
    """Returns (status, body) for any HTTP response, including 4xx/5xx --
    callers decide which codes are errors for their own request. Only a
    network-level failure (can't reach the host at all) raises."""
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
        raise SidekickError(f"{method} {full}: {e.reason}") from e


def load_token() -> Optional[str]:
    try:
        tok = TOKEN_CACHE.read_text().strip()
    except FileNotFoundError:
        return None
    return tok or None


def save_token(token: str) -> None:
    TOKEN_CACHE.write_text(token.strip() + "\n")
    try:
        TOKEN_CACHE.chmod(stat.S_IRUSR | stat.S_IWUSR)  # 0600 -- it's a live credential
    except OSError:
        pass  # best-effort; not fatal on platforms/filesystems that reject it


def clear_token() -> None:
    TOKEN_CACHE.unlink(missing_ok=True)


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
            raise SidekickError("200 from /api/pairing-token but no token field")
        if code not in (404,):
            print(f"  GET /api/pairing-token → {code} (retrying)")
        remaining = int(deadline - time.monotonic())
        if remaining != last_tick and (remaining % 10 == 0 or remaining < 5):
            print(f"  still waiting… {remaining}s left — press the button")
            last_tick = remaining
        time.sleep(POLL_S)
    raise SidekickError("timed out waiting for pairing button / token")


def with_token_retry(host: str, wait_s: float, fn: Callable[[str], T]) -> T:
    """Runs fn(token): cached token first if one is on disk, else the button
    flow (and the result is cached for next time either way). If fn raises
    AuthError -- meaning the device said 401 -- the cache is cleared and this
    falls back to the button once, then retries fn with the fresh token.
    fn is responsible for raising AuthError itself on a 401 response; this
    function has no way to see inside fn's own HTTP calls otherwise.
    """
    token = load_token()
    if token:
        print(f"using cached token ({len(token)} hex chars)")
    else:
        token = wait_for_token(host, wait_s)
        save_token(token)

    try:
        return fn(token)
    except AuthError:
        print("cached token rejected (401) — press the button for a fresh one")
        clear_token()
        token = wait_for_token(host, wait_s)
        save_token(token)
        return fn(token)


def die(msg: str, code: int = 1) -> None:
    print(f"error: {msg}", file=sys.stderr)
    raise SystemExit(code)
