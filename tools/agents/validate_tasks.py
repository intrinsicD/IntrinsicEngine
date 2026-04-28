#!/usr/bin/env python3
"""Validate structured task files under tasks/.

Default mode emits warnings and exits 0 for validation findings.
Use --strict to fail with exit code 1 when findings are present.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

TASK_ID_RE = re.compile(r"^#\s+([A-Z]+-\d+[A-Z0-9-]*)\s+—\s+.+$")
SECTION_RE = re.compile(r"^##\s+(.+?)\s*$")
DATE_RE = re.compile(r"\b\d{4}-\d{2}-\d{2}\b")

REQUIRED_SECTIONS = [
    "Goal",
    "Non-goals",
    "Required changes",
    "Tests",
    "Docs",
    "Acceptance criteria",
    "Verification",
]

SKIP_FILENAMES = {"README.md", "index.md", "legacy-todo.md", "0000-repo-reorganization-tracker.md"}


@dataclass
class Finding:
    level: str
    path: Path
    message: str


@dataclass
class ParsedTask:
    path: Path
    task_id: str | None
    sections: set[str]
    content: str


def parse_task(path: Path) -> ParsedTask:
    content = path.read_text(encoding="utf-8")
    lines = content.splitlines()

    task_id = None
    for line in lines:
        stripped = line.strip()
        if not stripped:
            continue
        m = TASK_ID_RE.match(stripped)
        if m:
            task_id = m.group(1)
        break

    sections: set[str] = set()
    for line in lines:
        m = SECTION_RE.match(line.strip())
        if m:
            sections.add(m.group(1))

    return ParsedTask(path=path, task_id=task_id, sections=sections, content=content)


def find_markdown_files(root: Path) -> list[Path]:
    task_roots = [root / "active", root / "backlog", root / "done"]
    files: list[Path] = []
    for task_root in task_roots:
        if not task_root.exists():
            continue
        for path in sorted(task_root.rglob("*.md")):
            if path.name in SKIP_FILENAMES:
                continue
            files.append(path)
    return files


def validate_task(parsed: ParsedTask, mode: str) -> list[Finding]:
    findings: list[Finding] = []
    rel_path = parsed.path

    if not parsed.task_id:
        findings.append(Finding("error", rel_path, "missing task header with ID (`# <ID> — <title>`)."))

    missing_sections = [name for name in REQUIRED_SECTIONS if name not in parsed.sections]
    if missing_sections:
        findings.append(
            Finding(
                "error",
                rel_path,
                "missing required section(s): " + ", ".join(missing_sections),
            )
        )

    path_parts = {p.lower() for p in rel_path.parts}
    is_active = "active" in path_parts
    is_done = "done" in path_parts

    if is_active and "Acceptance criteria" not in parsed.sections:
        findings.append(Finding("error", rel_path, "active task must include `## Acceptance criteria`."))

    if is_done:
        has_completion_date = bool(DATE_RE.search(parsed.content))
        has_pr_or_commit_ref = any(
            needle in parsed.content.lower()
            for needle in ("pr:", "pull request", "commit:", "sha:", "commit reference")
        )

        if not has_completion_date:
            findings.append(
                Finding(
                    "error",
                    rel_path,
                    "done task must include completion date (YYYY-MM-DD).",
                )
            )

        if not has_pr_or_commit_ref:
            findings.append(
                Finding(
                    "error",
                    rel_path,
                    "done task must include PR/commit reference placeholder or value.",
                )
            )

    if mode == "warning":
        for finding in findings:
            finding.level = "warning"

    return findings


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate structured task markdown files.")
    parser.add_argument("--root", default="tasks", help="Path to the tasks root directory (default: tasks).")
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Exit non-zero on findings. Default mode reports warnings and exits 0.",
    )
    args = parser.parse_args()

    root = Path(args.root).resolve()
    if not root.exists():
        print(f"ERROR: tasks root does not exist: {root}", file=sys.stderr)
        return 2

    files = find_markdown_files(root)
    if not files:
        print(f"No task markdown files found under {root}.")
        return 0

    mode = "strict" if args.strict else "warning"
    findings: list[Finding] = []

    for file in files:
        parsed = parse_task(file)
        findings.extend(validate_task(parsed, mode=mode))

    prefix = root.parent
    for finding in findings:
        try:
            display = finding.path.relative_to(prefix)
        except ValueError:
            display = finding.path
        print(f"[{finding.level.upper()}] {display}: {finding.message}")

    print(f"Validated {len(files)} task file(s); findings: {len(findings)}; mode: {mode}.")

    if args.strict and findings:
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
