#!/usr/bin/env python3
"""Validate task lifecycle links and nearby status claims."""

from __future__ import annotations

import argparse
import fnmatch
import re
from dataclasses import dataclass
from pathlib import Path

TASK_ID_RE = re.compile(r"\b[A-Z]+-\d+[A-Z0-9]*\b")
TASK_ID_FROM_FILENAME_RE = re.compile(r"^([A-Z]+-\d+[A-Z0-9]*)(?:-|\.md$)")
LINK_PATTERN = re.compile(r"\[[^\]]+\]\(([^)]+)\)")
FENCED_CODE_PATTERN = re.compile(r"```.*?```", re.DOTALL)
INLINE_CODE_PATTERN = re.compile(r"`[^`]*`")

IGNORED_TOP_LEVEL_PATTERNS = {
    ".git",
    ".idea",
    "Testing",
    "build",
    "build-*",
    "cmake-build-*",
    "external",
    "experimental",
    "third_party",
}

LIFECYCLE_DIRS = ("active", "backlog", "done")

STATUS_PATTERNS: tuple[tuple[re.Pattern[str], str], ...] = (
    (re.compile(r"^\s*\((?:active|in[- ]progress)\)", re.IGNORECASE), "active"),
    (re.compile(r"^\s*\((?:backlog|open)\)", re.IGNORECASE), "backlog"),
    (re.compile(r"^\s*\((?:done|completed|retired)\)", re.IGNORECASE), "done"),
)


@dataclass(frozen=True)
class TaskRecord:
    task_id: str
    state: str
    path: Path


@dataclass(frozen=True)
class Finding:
    path: Path
    line: int
    message: str


def is_ignored_path(path: Path, root: Path) -> bool:
    try:
        relative = path.relative_to(root)
    except ValueError:
        return False
    if not relative.parts:
        return False
    return any(fnmatch.fnmatchcase(relative.parts[0], pattern) for pattern in IGNORED_TOP_LEVEL_PATTERNS)


def task_id_from_path(path: Path) -> str | None:
    match = TASK_ID_FROM_FILENAME_RE.match(path.name)
    return match.group(1) if match else None


def lifecycle_state(path: Path, tasks_root: Path) -> str | None:
    try:
        relative = path.relative_to(tasks_root)
    except ValueError:
        return None
    if not relative.parts:
        return None
    return relative.parts[0] if relative.parts[0] in LIFECYCLE_DIRS else None


def collect_task_index(tasks_root: Path) -> dict[str, list[TaskRecord]]:
    index: dict[str, list[TaskRecord]] = {}
    for state in LIFECYCLE_DIRS:
        root = tasks_root / state
        if not root.is_dir():
            continue
        for path in sorted(root.rglob("*.md")):
            if path.name == "README.md":
                continue
            task_id = task_id_from_path(path)
            if task_id is None:
                continue
            index.setdefault(task_id, []).append(TaskRecord(task_id=task_id, state=state, path=path))
    return index


def actual_states(task_id: str, index: dict[str, list[TaskRecord]]) -> set[str]:
    return {record.state for record in index.get(task_id, [])}


def describe_actual(task_id: str, index: dict[str, list[TaskRecord]], root: Path) -> str:
    records = index.get(task_id, [])
    if not records:
        return "missing"
    return ", ".join(f"{record.state}:{record.path.relative_to(root)}" for record in records)


def strip_fenced_code(content: str) -> str:
    return FENCED_CODE_PATTERN.sub("", content)


def is_ignored_link(raw_link: str) -> bool:
    link = raw_link.strip()
    return (
        not link
        or link.startswith("http://")
        or link.startswith("https://")
        or link.startswith("mailto:")
        or link.startswith("#")
    )


def normalize_target(source_file: Path, raw_link: str) -> Path:
    target_text = raw_link.split("#", 1)[0].strip()
    return (source_file.parent / target_text).resolve()


def line_number_for_offset(content: str, offset: int) -> int:
    return content.count("\n", 0, offset) + 1


def validate_link_states(
    md_file: Path,
    content: str,
    root: Path,
    tasks_root: Path,
    index: dict[str, list[TaskRecord]],
) -> list[Finding]:
    findings: list[Finding] = []
    content_wo_code = strip_fenced_code(content)

    for match in LINK_PATTERN.finditer(content_wo_code):
        raw_link = match.group(1).strip()
        if is_ignored_link(raw_link):
            continue

        target = normalize_target(md_file, raw_link)
        target_state = lifecycle_state(target, tasks_root)
        if target_state is None:
            continue

        task_id = task_id_from_path(target)
        if task_id is None:
            continue

        states = actual_states(task_id, index)
        line = line_number_for_offset(content_wo_code, match.start())
        if not states:
            findings.append(
                Finding(
                    md_file,
                    line,
                    f"{task_id}: link targets tasks/{target_state}, but no task file with this ID exists",
                )
            )
            continue

        if target_state not in states:
            findings.append(
                Finding(
                    md_file,
                    line,
                    f"{task_id}: link targets tasks/{target_state}, but actual location is {describe_actual(task_id, index, root)}",
                )
            )

        if len(states) > 1:
            findings.append(
                Finding(
                    md_file,
                    line,
                    f"{task_id}: task ID exists in multiple lifecycle directories: {describe_actual(task_id, index, root)}",
                )
            )

    return findings


def claimed_states(line: str) -> set[str]:
    claims: set[str] = set()
    visible = INLINE_CODE_PATTERN.sub("", line)
    for pattern, state in STATUS_PATTERNS:
        if pattern.search(visible):
            claims.add(state)
    return claims


def validate_status_claims(
    md_file: Path,
    content: str,
    root: Path,
    index: dict[str, list[TaskRecord]],
) -> list[Finding]:
    findings: list[Finding] = []

    def record_mismatches(task_id: str, claims: set[str], line_number: int) -> None:
        states = actual_states(task_id, index)
        if not states:
            return
        for claim in sorted(claims):
            if claim not in states:
                findings.append(
                    Finding(
                        md_file,
                        line_number,
                        f"{task_id}: line claims {claim}, but actual location is {describe_actual(task_id, index, root)}",
                    )
                )
        if len(states) > 1:
            findings.append(
                Finding(
                    md_file,
                    line_number,
                    f"{task_id}: task ID exists in multiple lifecycle directories: {describe_actual(task_id, index, root)}",
                )
            )

    for line_number, line in enumerate(content.splitlines(), start=1):
        for link_match in LINK_PATTERN.finditer(line):
            status_window = line[link_match.end() : link_match.end() + 80]
            claims = claimed_states(status_window)
            if not claims:
                continue
            for task_id in sorted(set(TASK_ID_RE.findall(link_match.group(0)))):
                record_mismatches(task_id, claims, line_number)

        for match in TASK_ID_RE.finditer(line):
            task_id = match.group(0)
            status_window = line[match.end() : match.end() + 80]
            claims = claimed_states(status_window)
            if not claims:
                continue
            record_mismatches(task_id, claims, line_number)
    return findings


# Session-start index files must describe current state only: retired-task
# history belongs in tasks/done/RETIREMENT-LOG.md, not in member lists. Any
# link into tasks/done/ from these files (other than to the retirement log
# itself) is regrowth of the pre-PROC-003 history clutter.
STATE_ONLY_INDEX_FILES = (
    Path("tasks/active/README.md"),
    Path("tasks/backlog/README.md"),
)

RETIREMENT_LOG_NAME = "RETIREMENT-LOG.md"


def validate_state_only_indexes(
    md_file: Path,
    content: str,
    root: Path,
    tasks_root: Path,
) -> list[Finding]:
    try:
        rel = md_file.relative_to(root)
    except ValueError:
        return []
    if rel not in STATE_ONLY_INDEX_FILES:
        return []

    findings: list[Finding] = []
    content_wo_code = strip_fenced_code(content)
    done_root = tasks_root / "done"
    for match in LINK_PATTERN.finditer(content_wo_code):
        raw_link = match.group(1).strip()
        if is_ignored_link(raw_link):
            continue
        target = normalize_target(md_file, raw_link)
        if target.name == RETIREMENT_LOG_NAME:
            continue
        if target.is_relative_to(done_root):
            line = line_number_for_offset(content_wo_code, match.start())
            findings.append(
                Finding(
                    md_file,
                    line,
                    f"state-only index links retired task {target.name}; move the entry "
                    f"to tasks/done/{RETIREMENT_LOG_NAME} or the category README",
                )
            )
    return findings


def markdown_files_to_scan(root: Path, tasks_root: Path) -> list[Path]:
    files: list[Path] = []
    for base in (tasks_root, root / "docs" / "agent"):
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*.md")):
            if is_ignored_path(path, root):
                continue
            files.append(path)
    return files


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=".", help="Repository root path.")
    parser.add_argument("--strict", action="store_true", help="Fail when findings are found.")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    tasks_root = root / "tasks"
    index = collect_task_index(tasks_root)

    findings: list[Finding] = []
    for md_file in markdown_files_to_scan(root, tasks_root):
        content = md_file.read_text(encoding="utf-8")
        findings.extend(validate_link_states(md_file, content, root, tasks_root, index))
        findings.extend(validate_status_claims(md_file, content, root, index))
        findings.extend(validate_state_only_indexes(md_file, content, root, tasks_root))

    print(f"[check_task_state_links] Root: {root}")
    print(f"[check_task_state_links] Indexed task IDs: {len(index)}")
    print(f"[check_task_state_links] Mode: {'strict' if args.strict else 'warning'}")

    if findings:
        print("[check_task_state_links] Findings:")
        for finding in findings:
            print(f"  - {finding.path.relative_to(root)}:{finding.line}: {finding.message}")
        if args.strict:
            print("[check_task_state_links] STRICT MODE: failing.")
            return 1
        print("[check_task_state_links] WARNING MODE: non-fatal.")
    else:
        print("[check_task_state_links] No task-state link findings.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
