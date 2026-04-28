#!/usr/bin/env python3
"""Validate GitHub workflow file naming and structure policy."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ALLOWED_WORKFLOW_FILES = {
    "pr-fast.yml",
    "ci-linux-clang.yml",
    "ci-sanitizers.yml",
    "ci-docs.yml",
    "ci-bench-smoke.yml",
    "nightly-deep.yml",
}

REQUIRED_WORKFLOW_FILES = {
    "pr-fast.yml",
    "ci-linux-clang.yml",
    "ci-sanitizers.yml",
    "ci-docs.yml",
    "ci-bench-smoke.yml",
}

NAME_PATTERN = re.compile(r"^name:\s*(.+?)\s*$")
ON_PATTERN = re.compile(r"^on:\s*(.*)$")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate workflow naming, trigger presence, and readability checks."
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(".github/workflows"),
        help="Path to workflow directory (default: .github/workflows)",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Fail if optional-but-allowed files from final set are still missing.",
    )
    return parser.parse_args()


def validate_file(workflow_path: Path, errors: list[str], warnings: list[str]) -> None:
    expected_name = workflow_path.stem
    raw_lines = workflow_path.read_text(encoding="utf-8").splitlines()

    if len(raw_lines) <= 1:
        errors.append(
            f"{workflow_path.name}: workflow appears compressed to one line; keep YAML readable."
        )

    name_value: str | None = None
    on_seen = False

    for line in raw_lines:
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue

        name_match = NAME_PATTERN.match(line)
        if name_match and name_value is None:
            candidate = name_match.group(1).strip()
            if (candidate.startswith('"') and candidate.endswith('"')) or (
                candidate.startswith("'") and candidate.endswith("'")
            ):
                candidate = candidate[1:-1]
            name_value = candidate
            continue

        on_match = ON_PATTERN.match(line)
        if on_match:
            on_seen = True
            inline = on_match.group(1).strip()
            if inline:
                warnings.append(
                    f"{workflow_path.name}: inline 'on' trigger is allowed but mapping form is preferred."
                )
            continue

    if name_value != expected_name:
        errors.append(
            f"{workflow_path.name}: top-level name must match filename stem '{expected_name}' (found '{name_value}')."
        )

    if not on_seen:
        errors.append(f"{workflow_path.name}: missing explicit 'on' trigger section.")


def main() -> int:
    args = parse_args()
    workflow_dir = args.root
    if not workflow_dir.exists():
        print(f"ERROR: workflow directory not found: {workflow_dir}")
        return 1

    workflow_files = sorted(workflow_dir.glob("*.yml"))
    found_names = {path.name for path in workflow_files}

    errors: list[str] = []
    warnings: list[str] = []

    unexpected = sorted(found_names - ALLOWED_WORKFLOW_FILES)
    if unexpected:
        errors.append(f"Unexpected workflow files: {', '.join(unexpected)}")

    missing_required = sorted(REQUIRED_WORKFLOW_FILES - found_names)
    if missing_required:
        errors.append(f"Missing required workflow files: {', '.join(missing_required)}")

    if args.strict:
        missing_allowed = sorted(ALLOWED_WORKFLOW_FILES - found_names)
        if missing_allowed:
            errors.append(
                "Strict mode requires full canonical set; missing: "
                + ", ".join(missing_allowed)
            )

    for workflow_file in workflow_files:
        validate_file(workflow_file, errors, warnings)

    for warning in warnings:
        print(f"WARNING: {warning}")

    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1

    print(
        "Workflow naming check passed: file names, top-level names, trigger sections, and readability gates are valid."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
