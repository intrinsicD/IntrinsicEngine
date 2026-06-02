#!/usr/bin/env python3
"""Validate operational follow-up wording for ambiguous open task maturities."""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path

TASK_ID_RE = re.compile(r"\b[A-Z]+-\d+[A-Z0-9]*\b")
SECTION_RE = re.compile(r"^##\s+(.+?)\s*$")
CPU_CONTRACTED_RE = re.compile(r"\bCPUContracted\b")
NO_OPERATIONAL_FOLLOWUP_RE = re.compile(
    r"no\s+`?Operational`?\s+follow-up\s+is\s+owed",
    re.IGNORECASE,
)
OPERATIONAL_OWNER_RE = re.compile(
    r"`?Operational`?\s+(?:is\s+)?owned by\b[^\n]*\b[A-Z]+-\d+[A-Z0-9]*\b",
    re.IGNORECASE,
)
BACKEND_FACING_RE = re.compile(
    r"\b("
    r"graphics|render(?:er|ing)?|vulkan|gpu|runtime composition|"
    r"pass command|asset ingest|hot reload"
    r")\b",
    re.IGNORECASE,
)
CPU_CLOSURE_RE = re.compile(
    r"("
    r"target\s*:\s*(?![^\n]*`?Operational`?)[^\n]*\bCPUContracted\b|"
    r"\bclos(?:es|ed|ing)\b[^\n]*\bCPUContracted\b|"
    r"\bcloses the task at\b[^\n]*\bCPUContracted\b"
    r")",
    re.IGNORECASE,
)

OPEN_TASK_DIRS = ("active", "backlog")


@dataclass(frozen=True)
class Section:
    name: str
    start_line: int
    lines: list[str]


@dataclass(frozen=True)
class Finding:
    path: Path
    line: int
    phrase: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."), help="Repository root")
    parser.add_argument("--strict", action="store_true", help="Fail on findings")
    return parser.parse_args()


def collect_open_task_files(root: Path) -> list[Path]:
    task_files: list[Path] = []
    tasks_root = root / "tasks"
    for state in OPEN_TASK_DIRS:
        state_root = tasks_root / state
        if not state_root.is_dir():
            continue
        for path in sorted(state_root.rglob("*.md")):
            if path.name == "README.md":
                continue
            task_files.append(path)
    return task_files


def split_sections(lines: list[str]) -> dict[str, Section]:
    sections: dict[str, Section] = {}
    current_name: str | None = None
    current_start = 0
    current_lines: list[str] = []

    for idx, line in enumerate(lines, start=1):
        match = SECTION_RE.match(line)
        if match:
            if current_name is not None:
                sections[current_name.lower()] = Section(current_name, current_start, current_lines)
            current_name = match.group(1).strip()
            current_start = idx
            current_lines = []
            continue
        if current_name is not None:
            current_lines.append(line)

    if current_name is not None:
        sections[current_name.lower()] = Section(current_name, current_start, current_lines)

    return sections


def section_text(section: Section | None) -> str:
    if section is None:
        return ""
    return "\n".join(section.lines)


def is_backend_facing(path: Path, root: Path, title: str, sections: dict[str, Section]) -> bool:
    try:
        rel_path = path.relative_to(root).as_posix()
    except ValueError:
        rel_path = path.as_posix()
    searchable = "\n".join(
        [
            rel_path,
            title,
            section_text(sections.get("context")),
            section_text(sections.get("maturity")),
        ]
    )
    return bool(BACKEND_FACING_RE.search(searchable))


def has_accepted_operational_statement(text: str) -> bool:
    return bool(OPERATIONAL_OWNER_RE.search(text) or NO_OPERATIONAL_FOLLOWUP_RE.search(text))


def find_cpu_closure_line(maturity: Section) -> tuple[int, str] | None:
    for offset, line in enumerate(maturity.lines, start=1):
        if not CPU_CONTRACTED_RE.search(line):
            continue
        if CPU_CLOSURE_RE.search(line):
            return maturity.start_line + offset, line.strip()
    return None


def validate_task(path: Path, root: Path) -> list[Finding]:
    lines = path.read_text(encoding="utf-8").splitlines()
    sections = split_sections(lines)
    maturity = sections.get("maturity")
    if maturity is None:
        return []

    maturity_text = section_text(maturity)
    if not CPU_CONTRACTED_RE.search(maturity_text):
        return []

    title = lines[0] if lines else ""
    if not is_backend_facing(path, root, title, sections):
        return []

    closure = find_cpu_closure_line(maturity)
    if closure is None:
        return []

    if has_accepted_operational_statement(maturity_text):
        return []

    line, phrase = closure
    return [Finding(path=path, line=line, phrase=phrase)]


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    print(f"[check_task_maturity_followups] Root: {root}")

    findings: list[Finding] = []
    task_files = collect_open_task_files(root)
    for path in task_files:
        findings.extend(validate_task(path, root))

    print(f"[check_task_maturity_followups] Open task files scanned: {len(task_files)}")

    if findings:
        print(f"[check_task_maturity_followups] Findings: {len(findings)}")
        for finding in findings:
            try:
                rel_path = finding.path.relative_to(root)
            except ValueError:
                rel_path = finding.path
            print(
                f"  - {rel_path}:{finding.line}: backend-facing CPUContracted closure "
                f"'{finding.phrase}' must state `Operational owned by <TASK-ID>` "
                "or `no Operational follow-up is owed`."
            )
        if args.strict:
            print("[check_task_maturity_followups] STRICT MODE: failing due to findings.")
            return 1
        print("[check_task_maturity_followups] WARNING MODE: findings are non-fatal.")
        return 0

    print("[check_task_maturity_followups] No maturity follow-up findings.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
