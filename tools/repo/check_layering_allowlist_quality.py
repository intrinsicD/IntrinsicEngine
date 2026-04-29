#!/usr/bin/env python3
"""Validate layering allowlist quality requirements."""

from __future__ import annotations

import argparse
from collections import Counter
from pathlib import Path

import yaml

ALLOWLIST_REL = Path("tools/repo/layering_allowlist.yaml")
REQUIRED_FIELDS = ("from", "to", "file_glob", "task", "expires", "reason")
FORBIDDEN_GLOBS = {"src/legacy/**", "./src/legacy/**"}


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


def validate(entries: list[dict[str, object]]) -> list[str]:
    findings: list[str] = []

    for idx, entry in enumerate(entries, start=1):
        for field in REQUIRED_FIELDS:
            value = entry.get(field)
            if not isinstance(value, str) or not value.strip():
                findings.append(f"Entry #{idx}: missing or empty '{field}'.")

        file_glob = str(entry.get("file_glob", "")).strip()
        if file_glob in FORBIDDEN_GLOBS:
            findings.append(f"Entry #{idx}: forbidden broad glob '{file_glob}'.")

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

    findings = validate(entries)
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
