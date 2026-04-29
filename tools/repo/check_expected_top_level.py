#!/usr/bin/env python3
"""Compare current top-level entries against an expected allowlist."""

from __future__ import annotations

import argparse
from pathlib import Path

EXPECTED_TOP_LEVEL = {
    ".claude",
    ".codex",
    ".github",
    ".gitignore",
    "AGENTS.md",
    "CLAUDE.md",
    "README.md",
    "CMakeLists.txt",
    "CMakePresets.json",
    "assets",
    "benchmarks",
    "cmake",
    "docs",
    "lsan.supp",
    "methods",
    "src",
    "tasks",
    "tests",
    "tools",
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."), help="Repository root")
    parser.add_argument("--strict", action="store_true", help="Fail on unexpected/missing entries")
    args = parser.parse_args()

    root = args.root.resolve()
    actual = {p.name for p in root.iterdir() if p.name != ".git"}

    unexpected = sorted(actual - EXPECTED_TOP_LEVEL)
    missing = sorted(EXPECTED_TOP_LEVEL - actual)

    print(f"[check_expected_top_level] Root: {root}")
    if unexpected:
        print("[check_expected_top_level] Unexpected top-level entries:")
        for name in unexpected:
            print(f"  - {name}")
    if missing:
        print("[check_expected_top_level] Missing expected top-level entries:")
        for name in missing:
            print(f"  - {name}")

    if not unexpected and not missing:
        print("[check_expected_top_level] Top-level entries match expected allowlist.")
        return 0

    print("[check_expected_top_level] Action: reconcile migration phase or update allowlist intentionally.")
    if args.strict:
        print("[check_expected_top_level] STRICT MODE: failing.")
        return 1

    print("[check_expected_top_level] WARNING MODE: non-fatal.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
