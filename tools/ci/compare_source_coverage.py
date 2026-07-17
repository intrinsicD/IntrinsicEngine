#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Mapping, Sequence

from source_coverage import (
    CoverageError,
    compare_test_cohort_transition,
    compare_test_only_refactor,
)
from test_cohort_manifest import (
    CohortManifestError,
    read_test_cohort_manifest,
)


def _parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Compare normalized IntrinsicEngine source-coverage evidence "
            "without relying on aggregate percentages."
        )
    )
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument(
        "--test-only-refactor",
        action="store_true",
        help=(
            "require identical production/build identities and reject every "
            "lost covered production line, region, or branch arm"
        ),
    )
    mode.add_argument(
        "--test-cohort-transition",
        type=Path,
        metavar="MANIFEST",
        help=(
            "validate an exact declared test-cohort transition using each "
            "report's bound test inventory"
        ),
    )
    parser.add_argument(
        "--require-exact",
        action="store_true",
        help="with --test-only-refactor, reject gained evidence too",
    )
    arguments = parser.parse_args(argv)
    if arguments.require_exact and not arguments.test_only_refactor:
        parser.error("--require-exact requires --test-only-refactor")
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


def _read_bound_inventory(
    report_path: Path,
    report: Mapping[str, object],
) -> Mapping[str, object]:
    artifacts = report.get("artifacts")
    if not isinstance(artifacts, dict):
        raise CoverageError(
            f"coverage report {report_path} has no artifacts object"
        )
    relative = artifacts.get("test_inventory")
    if not isinstance(relative, str) or not relative:
        raise CoverageError(
            f"coverage report {report_path} has no test_inventory artifact"
        )
    relative_path = Path(relative)
    if relative_path.is_absolute():
        raise CoverageError(
            f"coverage report {report_path} test_inventory must be relative"
        )
    artifact_root = report_path.resolve().parent
    inventory_path = (artifact_root / relative_path).resolve()
    try:
        inventory_path.relative_to(artifact_root)
    except ValueError as error:
        raise CoverageError(
            f"coverage report {report_path} test_inventory escapes its "
            "artifact directory"
        ) from error
    return _read_report(inventory_path)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = _parse_args(sys.argv[1:] if argv is None else argv)
    try:
        baseline = _read_report(arguments.baseline)
        candidate = _read_report(arguments.candidate)
        if arguments.test_only_refactor:
            result = compare_test_only_refactor(
                baseline,
                candidate,
                require_exact=arguments.require_exact,
            )
        else:
            baseline_inventory = _read_bound_inventory(
                arguments.baseline,
                baseline,
            )
            candidate_inventory = _read_bound_inventory(
                arguments.candidate,
                candidate,
            )
            try:
                transition = read_test_cohort_manifest(
                    arguments.test_cohort_transition
                )
            except CohortManifestError as error:
                raise CoverageError(str(error)) from error
            result = compare_test_cohort_transition(
                baseline,
                candidate,
                baseline_inventory,
                candidate_inventory,
                transition,
            )
    except CoverageError as error:
        print(f"CPU source coverage comparison: error: {error}", file=sys.stderr)
        return 1
    summary = f"CPU source coverage comparison: ok: mode={result['mode']} "
    if result["mode"] == "test-only-refactor":
        summary += f"gained_lines={len(result['gained_lines'])} "
    summary += (
        f"gained_regions={len(result['gained_regions'])} "
        f"gained_branch_arms={len(result['gained_branch_arms'])} "
    )
    if result["mode"] == "test-only-refactor":
        summary += "lost_lines=0 "
    summary += "lost_regions=0 lost_branch_arms=0"
    if result["mode"] == "test-cohort-transition":
        summary += (
            f" baseline_cases={result['baseline_case_count']}"
            f" candidate_cases={result['candidate_case_count']}"
            f" moved_cases={result['moved_case_count']}"
            f" added_fast_sentinels={result['added_fast_sentinel_count']}"
        )
    print(summary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
