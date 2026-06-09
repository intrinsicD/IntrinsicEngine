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
    "Context",
    "Required changes",
    "Tests",
    "Docs",
    "Acceptance criteria",
    "Verification",
    "Forbidden changes",
]

ACTIONABLE_TODO_SECTIONS = [
    "Required changes",
    "Tests",
    "Docs",
    "Acceptance criteria",
]

TODO_RE = re.compile(r"^\s*- \[[ xX]\]\s+.+", re.MULTILINE)
OPEN_TODO_RE = re.compile(r"^\s*- \[ \]\s+.+", re.MULTILINE)

SKIP_FILENAMES = {
    "README.md",
    "index.md",
    "legacy-todo.md",
    "0000-repo-reorganization-tracker.md",
    "RENDERING-CLEANUP-TASK-PACK.md",
    "RETIREMENT-LOG.md",
}

# Queue/index meta-files (e.g. `tasks/active/task-NN-<slug>.md`) are
# ordering trackers for a series of structured tasks rather than canonical
# tasks themselves; the structured tasks they describe live elsewhere
# under `tasks/backlog|done` with proper task IDs.
SKIP_PATTERNS = [
    re.compile(r"^task-\d+-.*\.md$"),
]

# Historical task-ID collisions frozen as-is: the files are link targets across
# docs, reports, and the retirement record, so renaming them would damage the
# audit trail for no behavioral gain. Each ID is allowed exactly the listed
# filenames; any additional file claiming one of these IDs is a violation, as
# is any new collision on an ID not listed here. Do not extend this allowlist
# for collisions created after 2026-06-09 (PROC-002).
GRANDFATHERED_DUPLICATE_IDS: dict[str, frozenset[str]] = {
    # Two sandbox bug records opened concurrently under the same number.
    "BUG-021": frozenset(
        {
            "BUG-021-sandbox-camera-scene-table-shader-wiring.md",
            "BUG-021-sandbox-drop-import-blocks-platform-poll.md",
        }
    ),
    "BUG-022": frozenset(
        {
            "BUG-022-sandbox-nonmanifold-obj-import.md",
            "BUG-022-sandbox-reference-triangle-camera-frustum-visibility.md",
        }
    ),
    # Three HARDEN streams (ECS parity, sandbox boundary, task policy) each
    # took the next free number without cross-checking the other directories.
    "HARDEN-065": frozenset(
        {
            "HARDEN-065-ecs-geometry-source-population-and-dirty-domains.md",
            "HARDEN-065-sandbox-runtime-boundary.md",
            "HARDEN-065-task-checkbox-todo-policy.md",
        }
    ),
    "HARDEN-066": frozenset(
        {
            "HARDEN-066-ecs-render-sync-export-policy.md",
            "HARDEN-066-fix-halfedge-property-test-source-name.md",
        }
    ),
    "HARDEN-067": frozenset(
        {
            "HARDEN-067-ecs-bounds-propagation-system.md",
            "HARDEN-067-remove-stale-platform-linuxglfwvulkan.md",
        }
    ),
}


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
    section_bodies: dict[str, str]
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
    section_bodies: dict[str, str] = {}
    current_section: str | None = None
    current_body: list[str] = []

    for line in lines:
        m = SECTION_RE.match(line.strip())
        if m:
            if current_section is not None:
                section_bodies[current_section] = "\n".join(current_body)
            current_section = m.group(1)
            current_body = []
            sections.add(current_section)
            continue
        if current_section is not None:
            current_body.append(line)

    if current_section is not None:
        section_bodies[current_section] = "\n".join(current_body)

    return ParsedTask(path=path, task_id=task_id, sections=sections, section_bodies=section_bodies, content=content)


def find_markdown_files(root: Path) -> list[Path]:
    task_roots = [root / "active", root / "backlog", root / "done"]
    files: list[Path] = []
    for task_root in task_roots:
        if not task_root.exists():
            continue
        for path in sorted(task_root.rglob("*.md")):
            if path.name in SKIP_FILENAMES:
                continue
            if any(pattern.match(path.name) for pattern in SKIP_PATTERNS):
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

    for section in ACTIONABLE_TODO_SECTIONS:
        body = parsed.section_bodies.get(section, "")
        if section in parsed.sections and not TODO_RE.search(body):
            findings.append(
                Finding(
                    "error",
                    rel_path,
                    f"`## {section}` must contain markable checkbox todo(s) (`- [ ]` or `- [x]`).",
                )
            )

    if is_done:
        for section in ACTIONABLE_TODO_SECTIONS:
            body = parsed.section_bodies.get(section, "")
            if OPEN_TODO_RE.search(body):
                findings.append(
                    Finding(
                        "error",
                        rel_path,
                        f"done task has unchecked todo(s) in `## {section}`; move open work to a follow-up task.",
                    )
                )

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


def validate_id_uniqueness(parsed_tasks: list[ParsedTask]) -> list[Finding]:
    findings: list[Finding] = []
    by_id: dict[str, list[ParsedTask]] = {}
    for parsed in parsed_tasks:
        if parsed.task_id:
            by_id.setdefault(parsed.task_id, []).append(parsed)

    for task_id, owners in sorted(by_id.items()):
        if len(owners) <= 1:
            continue
        names = {owner.path.name for owner in owners}
        if names <= GRANDFATHERED_DUPLICATE_IDS.get(task_id, frozenset()):
            continue
        listing = ", ".join(sorted(str(owner.path) for owner in owners))
        findings.append(
            Finding(
                "error",
                owners[0].path,
                f"duplicate task ID `{task_id}` claimed by multiple files: {listing}. "
                "Allocate the next free number (see docs/agent/task-format.md, ID allocation).",
            )
        )
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

    parsed_tasks = [parse_task(file) for file in files]

    for parsed in parsed_tasks:
        findings.extend(validate_task(parsed, mode=mode))

    findings.extend(validate_id_uniqueness(parsed_tasks))

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
