#!/usr/bin/env python3
"""Validate layering allowlist quality requirements."""

from __future__ import annotations

import argparse
from collections import Counter
import re
from pathlib import Path

import yaml

ALLOWLIST_REL = Path("tools/repo/layering_allowlist.yaml")
REQUIRED_FIELDS = ("from", "to", "file_glob", "task", "expires", "reason")
FORBIDDEN_GLOBS = {"src/legacy/**", "./src/legacy/**"}
LIFECYCLE_DIRS = ("active", "backlog", "done")
OPEN_TASK_STATES = {"active", "backlog"}
TASK_ID_FROM_FILENAME_RE = re.compile(r"^([A-Z]+-\d+[A-Z0-9]*)(?:-|\.md$)")


def _load_entries(path: Path) -> list[dict[str, object]]:
    try:
        payload = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    except yaml.YAMLError as exc:
        raise ValueError(f"YAML parse error: {exc}") from exc

    entries = payload.get("exceptions", [])
    if not isinstance(entries, list):
        raise ValueError("Top-level 'exceptions' must be a list.")

    normalized: list[dict[str, object]] = []
    for idx, entry in enumerate(entries, start=1):
        if not isinstance(entry, dict):
            raise ValueError(f"Entry #{idx} is not a mapping.")
        normalized.append(entry)
    return normalized


def task_id_from_path(path: Path) -> str | None:
    match = TASK_ID_FROM_FILENAME_RE.match(path.name)
    return match.group(1) if match else None


def collect_task_states(root: Path) -> dict[str, set[str]]:
    states_by_task: dict[str, set[str]] = {}
    tasks_root = root / "tasks"
    for state in LIFECYCLE_DIRS:
        state_root = tasks_root / state
        if not state_root.is_dir():
            continue
        for path in sorted(state_root.rglob("*.md")):
            if path.name == "README.md":
                continue
            task_id = task_id_from_path(path)
            if task_id is None:
                continue
            states_by_task.setdefault(task_id, set()).add(state)
    return states_by_task


def describe_entry(idx: int, entry: dict[str, object]) -> str:
    return (
        f"Entry #{idx} "
        f"(from='{str(entry.get('from', '')).strip()}', "
        f"to='{str(entry.get('to', '')).strip()}', "
        f"file_glob='{str(entry.get('file_glob', '')).strip()}')"
    )


def validate(entries: list[dict[str, object]], task_states: dict[str, set[str]] | None = None) -> list[str]:
    findings: list[str] = []
    task_states = task_states or {}

    for idx, entry in enumerate(entries, start=1):
        for field in REQUIRED_FIELDS:
            value = entry.get(field)
            if not isinstance(value, str) or not value.strip():
                findings.append(f"Entry #{idx}: missing or empty '{field}'.")

        file_glob = str(entry.get("file_glob", "")).strip()
        if file_glob in FORBIDDEN_GLOBS:
            findings.append(f"Entry #{idx}: forbidden broad glob '{file_glob}'.")

        task = entry.get("task")
        if not isinstance(task, str) or not task.strip():
            continue

        task_id = task.strip()
        states = task_states.get(task_id, set())
        context = describe_entry(idx, entry)
        if not states:
            findings.append(f"{context}: unknown task owner '{task_id}'.")
            continue
        if len(states) > 1:
            findings.append(
                f"{context}: task owner '{task_id}' appears in multiple lifecycle directories "
                f"({', '.join(sorted(states))})."
            )
            continue
        if states.isdisjoint(OPEN_TASK_STATES):
            findings.append(
                f"{context}: task owner '{task_id}' is retired/not open "
                f"({', '.join(sorted(states))})."
            )

    key_counter: Counter[tuple[str, str, str]] = Counter()
    for entry in entries:
        key = (
            str(entry.get("from", "")).strip(),
            str(entry.get("to", "")).strip(),
            str(entry.get("file_glob", "")).strip(),
        )
        key_counter[key] += 1

    for (from_layer, to_layer, file_glob), count in sorted(key_counter.items()):
        if count > 1:
            findings.append(
                "Duplicate exception key "
                f"(from='{from_layer}', to='{to_layer}', file_glob='{file_glob}') occurs {count} times."
            )

    return findings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."), help="Repository root")
    parser.add_argument("--strict", action="store_true", help="Fail on findings")
    args = parser.parse_args()

    allowlist_path = (args.root / ALLOWLIST_REL).resolve()
    print(f"[check_layering_allowlist_quality] Allowlist: {allowlist_path}")

    if not allowlist_path.exists():
        print("[check_layering_allowlist_quality] ERROR: allowlist file not found.")
        return 2

    try:
        entries = _load_entries(allowlist_path)
    except ValueError as exc:
        print(f"[check_layering_allowlist_quality] ERROR: {exc}")
        return 2

    task_states = collect_task_states(args.root.resolve())
    findings = validate(entries, task_states)
    print(f"[check_layering_allowlist_quality] Entries: {len(entries)}")

    if findings:
        print(f"[check_layering_allowlist_quality] Findings: {len(findings)}")
        for item in findings:
            print(f"  - {item}")
        if args.strict:
            print("[check_layering_allowlist_quality] STRICT MODE: failing due to findings.")
            return 1
        print("[check_layering_allowlist_quality] WARNING MODE: findings are non-fatal.")
        return 0

    print("[check_layering_allowlist_quality] No findings.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
