#!/usr/bin/env python3
"""Detect stale src_new/src-new/src new references outside allowlisted files."""

from __future__ import annotations

import argparse
import re
import fnmatch
from pathlib import Path

PATTERN = re.compile(r"src_new|src-new|src new", re.IGNORECASE)
DEFAULT_ALLOWLIST = "tools/repo/src_new_reference_allowlist.txt"
SKIP_DIRS = {".git", "build", "out", ".cache", "third_party", "external", "vendor"}
TEXT_EXTS = {".md", ".txt", ".py", ".cmake", ".yaml", ".yml", ".json", ".cpp", ".hpp", ".h", ".cppm", ".ixx", ".sh"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Check for stale src_new references")
    parser.add_argument("--root", default=".", help="Repository root")
    parser.add_argument("--allowlist", default=DEFAULT_ALLOWLIST, help="Allowlist file path (repo-relative)")
    parser.add_argument("--strict", action="store_true", help="Fail on findings")
    return parser.parse_args()


def load_allowlist(repo_root: Path, allowlist_rel: str) -> list[str]:
    allowlist_path = repo_root / allowlist_rel
    patterns: list[str] = []
    if not allowlist_path.is_file():
        return patterns
    for raw in allowlist_path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        patterns.append(line)
    return patterns


def is_candidate(path: Path) -> bool:
    if path.suffix.lower() in TEXT_EXTS:
        return True
    return path.name in {"CMakeLists.txt", "AGENTS.md"}


def main() -> int:
    args = parse_args()
    repo_root = Path(args.root).resolve()
    allow_patterns = load_allowlist(repo_root, args.allowlist)

    findings: list[str] = []
    for path in repo_root.rglob("*"):
        if not path.is_file():
            continue
        if any(part in SKIP_DIRS for part in path.parts):
            continue
        rel = path.relative_to(repo_root).as_posix()
        if any(fnmatch.fnmatch(rel, pattern) for pattern in allow_patterns):
            continue
        if not is_candidate(path):
            continue
        try:
            lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
        except OSError:
            continue
        for i, line in enumerate(lines, start=1):
            if PATTERN.search(line):
                findings.append(f"{rel}:{i}: contains stale src_new reference")

    if findings:
        mode = "ERROR" if args.strict else "WARNING"
        for finding in findings:
            print(f"{mode}: {finding}")
        print(f"src_new stale-reference check complete. findings={len(findings)} mode={'strict' if args.strict else 'warning'}")
        return 1 if args.strict else 0

    print(f"src_new stale-reference check complete. findings=0 mode={'strict' if args.strict else 'warning'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
