#!/usr/bin/env python3
"""Fail-fast prerequisite guard for CI steps (3-state: pass / fail / BLOCKED).

Verify that producer artifacts exist before a dependent step runs. Exits 3
(BLOCKED) — distinct from a normal failure — when a required path is absent, so
a pipeline can distinguish "the thing we depend on was never produced" from
"the thing we tested is broken".

    python3 tools/agent/checks/check_prereqs.py --path build/app --kind file
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import _common as c

TOOL = "check_prereqs"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__ or TOOL)
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--path", action="append", required=True, help="required path (repeatable)")
    parser.add_argument("--kind", choices=["file", "dir", "any"], default="any")
    parser.add_argument("--message", default="", help="hint printed when blocked")
    args = parser.parse_args()

    blocked = []
    for rel in args.path:
        target = args.root / rel
        ok = (
            target.is_file() if args.kind == "file"
            else target.is_dir() if args.kind == "dir"
            else target.exists()
        )
        if not ok:
            blocked.append(rel)

    if blocked:
        for rel in blocked:
            print(f"[{TOOL}] BLOCKED: missing {args.kind}: {rel}")
        if args.message:
            print(f"[{TOOL}] hint: {args.message}")
        return c.EXIT_BLOCKED
    print(f"[{TOOL}] prerequisites satisfied ({len(args.path)} path(s)).")
    return c.EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
