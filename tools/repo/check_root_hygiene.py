#!/usr/bin/env python3
"""Report root-level markdown files and whether they are allowed."""

from __future__ import annotations

import argparse
from pathlib import Path

ALLOWED_ROOT_MARKDOWN = {
    "README.md",
    "AGENTS.md",
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."), help="Repository root")
    parser.add_argument("--strict", action="store_true", help="Fail on disallowed markdown files")
    args = parser.parse_args()

    root = args.root.resolve()
    root_md = sorted(
        p.name for p in root.iterdir() if p.is_file() and p.suffix.lower() == ".md"
    )

    disallowed = [name for name in root_md if name not in ALLOWED_ROOT_MARKDOWN]

    print(f"[check_root_hygiene] Root: {root}")
    if root_md:
        print("[check_root_hygiene] Root markdown files:")
        for name in root_md:
            status = "allowed" if name in ALLOWED_ROOT_MARKDOWN else "disallowed"
            print(f"  - {name}: {status}")
    else:
        print("[check_root_hygiene] No root-level markdown files found.")

    if disallowed:
        print("[check_root_hygiene] Action: move/archive/delete disallowed root markdown files.")
        if args.strict:
            print("[check_root_hygiene] STRICT MODE: failing due to disallowed files.")
            return 1
        print("[check_root_hygiene] WARNING MODE: non-fatal.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
