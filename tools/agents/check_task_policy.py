#!/usr/bin/env python3
"""Validate repository task-policy conventions.

This checker replaces legacy TODO-policy checks that only validated a single
backlog markdown file.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

REQUIRED_TASK_DIRS = (
    "tasks/active",
    "tasks/backlog",
    "tasks/done",
    "tasks/templates",
)

LEGACY_ROOT_TODO_FILES = (
    "TODO.md",
    "ACTIVE.md",
    "PLAN.md",
    "BUGS.md",
    "ROADMAP.md",
    "PATTERNS.md",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate task-policy structure and task metadata conventions."
    )
    parser.add_argument("--root", default=".", help="Repository root path.")
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Fail on any policy finding; warning mode otherwise.",
    )
    return parser.parse_args()


def collect_task_files(tasks_root: Path) -> list[Path]:
    task_files: list[Path] = []
    for status_dir in ("active", "backlog", "done"):
        root = tasks_root / status_dir
        if not root.is_dir():
            continue
        for path in root.rglob("*.md"):
            if path.name == "README.md":
                continue
            if path.name == "legacy-todo.md":
                continue
            task_files.append(path)
    return task_files


def main() -> int:
    args = parse_args()
    repo_root = Path(args.root).resolve()

    findings: list[str] = []

    for rel_dir in REQUIRED_TASK_DIRS:
        if not (repo_root / rel_dir).is_dir():
            findings.append(f"missing required directory: {rel_dir}")

    for file_name in LEGACY_ROOT_TODO_FILES:
        if (repo_root / file_name).exists():
            findings.append(
                f"legacy root planning file must be migrated into tasks/: {file_name}"
            )

    tasks_root = repo_root / "tasks"
    task_files = collect_task_files(tasks_root)
    if not task_files:
        findings.append("no structured task markdown files found under tasks/active|backlog|done")

    validator = repo_root / "tools" / "agents" / "validate_tasks.py"
    if validator.is_file() and tasks_root.is_dir():
        cmd = [sys.executable, str(validator), "--root", str(tasks_root), "--strict"]
        result = subprocess.run(cmd, cwd=repo_root)
        if result.returncode != 0:
            findings.append("validate_tasks.py strict mode reported task-format violations")
    else:
        findings.append("tools/agents/validate_tasks.py not found; cannot verify task format")

    if findings:
        mode = "ERROR" if args.strict else "WARNING"
        for finding in findings:
            print(f"{mode}: {finding}")
        if args.strict:
            return 1

    print(
        f"Task policy check complete. findings={len(findings)} mode={'strict' if args.strict else 'warning'}."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
