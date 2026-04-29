#!/usr/bin/env python3
"""Enforce taxonomy-owned test source layout."""

from __future__ import annotations

import argparse
from pathlib import Path

TEXT_TEST_EXTS = {".cpp", ".cc", ".cxx", ".hpp", ".hh", ".h", ".ixx", ".cppm"}
ALLOWED_SOURCE_ROOTS = {
    "tests/unit",
    "tests/contract",
    "tests/integration",
    "tests/regression",
    "tests/gpu",
    "tests/benchmark",
    "tests/support",
}
ALLOWED_EXACT_FILES = {"tests/CMakeLists.txt"}
LEGACY_WRAPPER_ROOTS = {
    "tests/Asset",
    "tests/Core",
    "tests/ECS",
    "tests/Graphics",
    "tests/Runtime",
}
SKIP_DIRS = {".git", "build", "out", ".cache", "third_party", "external", "vendor"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Validate taxonomy-owned test source layout")
    parser.add_argument("--root", default=".", help="Repository root")
    parser.add_argument("--strict", action="store_true", help="Fail on findings")
    return parser.parse_args()


def is_test_source(path: Path) -> bool:
    return path.suffix.lower() in TEXT_TEST_EXTS or path.name == "CMakeLists.txt"


def is_under(rel: str, root: str) -> bool:
    return rel == root or rel.startswith(root + "/")


def main() -> int:
    args = parse_args()
    repo_root = Path(args.root).resolve()

    findings: list[str] = []
    for path in (repo_root / "tests").rglob("*"):
        if not path.is_file():
            continue
        if any(part in SKIP_DIRS for part in path.parts):
            continue

        rel = path.relative_to(repo_root).as_posix()
        if any(is_under(rel, root) for root in LEGACY_WRAPPER_ROOTS):
            findings.append(f"{rel}: legacy wrapper path is forbidden")
            continue

        if rel in ALLOWED_EXACT_FILES:
            continue
        if not is_test_source(path):
            continue

        if not any(is_under(rel, root) for root in ALLOWED_SOURCE_ROOTS):
            findings.append(f"{rel}: test source is outside taxonomy-owned roots")

    if findings:
        mode = "ERROR" if args.strict else "WARNING"
        for finding in findings:
            print(f"{mode}: {finding}")
        print(f"test layout check complete. findings={len(findings)} mode={'strict' if args.strict else 'warning'}")
        return 1 if args.strict else 0

    print(f"test layout check complete. findings=0 mode={'strict' if args.strict else 'warning'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
