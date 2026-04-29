#!/usr/bin/env python3
"""Check repository root hygiene (markdown policy + top-level allowlist)."""

from __future__ import annotations

import argparse
from pathlib import Path

ALLOWED_ROOT_MARKDOWN = {"README.md", "AGENTS.md", "CLAUDE.md"}
DEFAULT_ALLOWLIST_FILE = Path("tools/repo/root_allowlist.yaml")


def _load_root_allowlist(path: Path) -> set[str]:
    if not path.exists():
        return set()

    entries: set[str] = set()
    in_entries = False
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue

        if line.startswith("allowed_root_entries:"):
            in_entries = True
            continue

        if in_entries and not line.startswith("-"):
            in_entries = False

        if in_entries and line.startswith("-"):
            value = line[1:].strip()
            if value.startswith((""", "'")) and value.endswith((""", "'")):
                value = value[1:-1]
            entries.add(value)

    return entries


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."), help="Repository root")
    parser.add_argument("--strict", action="store_true", help="Fail on disallowed markdown files or unexpected root entries")
    parser.add_argument(
        "--allowlist",
        type=Path,
        default=DEFAULT_ALLOWLIST_FILE,
        help="Path to root allowlist YAML-like file",
    )
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

    allowlist_path = (root / args.allowlist).resolve() if not args.allowlist.is_absolute() else args.allowlist
    expected_entries = _load_root_allowlist(allowlist_path)

    current_entries = {
        f"{p.name}/" if p.is_dir() else p.name
        for p in root.iterdir()
        if p.name != ".git"
    }

    unexpected_entries = sorted(current_entries - expected_entries) if expected_entries else []

    if disallowed:
        print("[check_root_hygiene] Action: move/archive/delete disallowed root markdown files.")
        if args.strict:
            print("[check_root_hygiene] STRICT MODE: failing due to disallowed files.")
            return 1
        print("[check_root_hygiene] WARNING MODE: non-fatal.")

    if expected_entries:
        missing_entries = sorted(expected_entries - current_entries)
        print(f"[check_root_hygiene] Root allowlist: {allowlist_path}")
        if unexpected_entries:
            print("[check_root_hygiene] Unexpected root entries:")
            for entry in unexpected_entries:
                print(f"  - {entry}")
        if missing_entries:
            print("[check_root_hygiene] Missing expected root entries:")
            for entry in missing_entries:
                print(f"  - {entry}")

        if args.strict and (unexpected_entries or missing_entries):
            print("[check_root_hygiene] STRICT MODE: failing due to root allowlist mismatch.")
            return 1

        if unexpected_entries or missing_entries:
            print("[check_root_hygiene] WARNING MODE: allowlist mismatch is non-fatal.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
