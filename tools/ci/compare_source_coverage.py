#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Mapping, Sequence

from source_coverage import CoverageError, compare_test_only_refactor


def _parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Compare normalized IntrinsicEngine source-coverage evidence "
            "without relying on aggregate percentages."
        )
    )
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument(
        "--test-only-refactor",
        action="store_true",
        help=(
            "require identical production/build identities and reject every "
            "lost covered production region or branch arm"
        ),
    )
    arguments = parser.parse_args(argv)
    if not arguments.test_only_refactor:
        parser.error("--test-only-refactor is required; no weaker mode is defined")
    return arguments


def _read_report(path: Path) -> Mapping[str, object]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise CoverageError(f"cannot read coverage report {path}: {error}") from error
    except json.JSONDecodeError as error:
        raise CoverageError(
            f"coverage report {path} is invalid JSON: {error}"
        ) from error
    if not isinstance(document, dict):
        raise CoverageError(f"coverage report {path} must be a JSON object")
    return document


def main(argv: Sequence[str] | None = None) -> int:
    arguments = _parse_args(sys.argv[1:] if argv is None else argv)
    try:
        baseline = _read_report(arguments.baseline)
        candidate = _read_report(arguments.candidate)
        result = compare_test_only_refactor(baseline, candidate)
    except CoverageError as error:
        print(f"CPU source coverage comparison: error: {error}", file=sys.stderr)
        return 1
    print(
        "CPU source coverage comparison: ok: "
        f"gained_regions={len(result['gained_regions'])} "
        f"gained_branch_arms={len(result['gained_branch_arms'])} "
        "lost_regions=0 lost_branch_arms=0"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
