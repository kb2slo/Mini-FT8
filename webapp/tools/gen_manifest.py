#!/usr/bin/env python3
"""Generate webapp/app/manifest.json from sibling files (RFC 0004 §4).

Usage:
  python3 webapp/tools/gen_manifest.py          # write manifest.json
  python3 webapp/tools/gen_manifest.py --check  # exit 1 if committed file drifts
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

API_MIN = 1
ROOT = Path(__file__).resolve().parents[1] / "app"
MANIFEST = ROOT / "manifest.json"
SKIP = {"manifest.json"}


def assets() -> list[dict]:
    out = []
    for path in sorted(ROOT.iterdir()):
        if not path.is_file() or path.name in SKIP or path.name.startswith("."):
            continue
        data = path.read_bytes()
        out.append(
            {
                "path": path.name,
                "sha256": hashlib.sha256(data).hexdigest(),
                "size": len(data),
            }
        )
    if not out:
        raise SystemExit(f"no assets under {ROOT}")
    return out


def build() -> dict:
    return {"api_min": API_MIN, "assets": assets()}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    doc = build()
    text = json.dumps(doc, indent=2) + "\n"
    if args.check:
        if not MANIFEST.is_file():
            print(f"missing {MANIFEST}", file=sys.stderr)
            return 1
        if MANIFEST.read_text() != text:
            print(f"{MANIFEST} is stale; run: python3 webapp/tools/gen_manifest.py", file=sys.stderr)
            return 1
        print("manifest ok")
        return 0
    MANIFEST.write_text(text)
    print(f"wrote {MANIFEST} ({len(doc['assets'])} assets)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
