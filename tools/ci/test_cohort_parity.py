#!/usr/bin/env python3
"""Verify a declared fast-to-slow CTest cohort transition."""

from __future__ import annotations

import argparse
import json
import re
import sys
import xml.etree.ElementTree as ElementTree
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping, Sequence

from test_cohort_manifest import (
    CohortManifestError,
    read_test_cohort_manifest,
)

REPORT_SCHEMA = "intrinsic.test-timing/v1"
VALID_STATUSES = {"disabled", "error", "failed", "passed", "skipped"}
MIN_SAMPLE_COUNT = 5
EXPECTED_REPORT_IDENTITIES = {
    "pr-fast": {
        "aggregate": "IntrinsicPrFastTests",
        "selector": {
            "exclude_any": ["flaky-quarantine", "gpu", "slow", "vulkan"],
            "include_any": ["contract", "unit"],
        },
        "timeout_seconds": 60,
    },
    "cpu": {
        "aggregate": "IntrinsicCpuTests",
        "selector": {
            "exclude_any": ["flaky-quarantine", "gpu", "slow", "vulkan"],
            "include_any": [],
        },
        "timeout_seconds": 60,
    },
    "cpu-slow": {
        "aggregate": "IntrinsicCpuSlowTests",
        "selector": {
            "exclude_any": [
                "benchmark",
                "flaky-quarantine",
                "gpu",
                "slo",
                "vulkan",
            ],
            "include_any": ["slow"],
        },
        "timeout_seconds": 120,
    },
}


class ParityError(RuntimeError):
    pass


@dataclass(frozen=True)
class TimingReport:
    cases: Mapping[str, Mapping[str, object]]
    sample_count: int
    sha: str
    runner_signature: tuple[object, ...]


def _read_json_object(path: Path, context: str) -> Mapping[str, object]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise ParityError(f"cannot read {context} {path}: {error}") from error
    except json.JSONDecodeError as error:
        raise ParityError(f"{context} {path} is invalid JSON: {error}") from error
    if not isinstance(document, dict):
        raise ParityError(f"{context} {path} must be a JSON object")
    return document


def _string_list(
    value: object,
    *,
    context: str,
    require_nonempty: bool,
) -> tuple[str, ...]:
    if not isinstance(value, list) or any(
        not isinstance(item, str) or not item for item in value
    ):
        raise ParityError(f"{context} must be a list of nonempty strings")
    result = tuple(value)
    if require_nonempty and not result:
        raise ParityError(f"{context} must not be empty")
    if len(result) != len(set(result)):
        raise ParityError(f"{context} contains duplicate values")
    if result != tuple(sorted(result)):
        raise ParityError(f"{context} must be sorted")
    return result


def _positive_int(value: object, context: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value < 1:
        raise ParityError(f"{context} must be a positive integer")
    return value


def _nonempty_string(value: object, context: str) -> str:
    if not isinstance(value, str) or not value:
        raise ParityError(f"{context} must be a nonempty string")
    return value


def _read_runner_identity(
    document: Mapping[str, object],
    path: Path,
    parallel_jobs: int,
) -> tuple[str, tuple[object, ...]]:
    host = document.get("host")
    if not isinstance(host, dict):
        raise ParityError(f"timing report {path} omits its host object")
    host_parallel = _positive_int(
        host.get("parallel_jobs"), f"timing report {path} host parallel_jobs"
    )
    if host_parallel != parallel_jobs:
        raise ParityError(
            f"timing report {path} host parallel_jobs {host_parallel} "
            f"does not match identity {parallel_jobs}"
        )
    github = host.get("github")
    if not isinstance(github, dict):
        raise ParityError(f"timing report {path} omits hosted GitHub identity")
    sha = _nonempty_string(
        github.get("github_sha"), f"timing report {path} github_sha"
    )
    if re.fullmatch(r"[0-9a-f]{40}", sha) is None:
        raise ParityError(f"timing report {path} github_sha is not a full SHA")
    runner_environment = _nonempty_string(
        github.get("runner_environment"),
        f"timing report {path} runner_environment",
    )
    if runner_environment != "github-hosted":
        raise ParityError(
            f"timing report {path} must come from a github-hosted runner"
        )
    signature = (
        _nonempty_string(host.get("system"), f"timing report {path} host system"),
        _nonempty_string(host.get("machine"), f"timing report {path} host machine"),
        _positive_int(
            host.get("logical_cpu_count"),
            f"timing report {path} logical_cpu_count",
        ),
        host_parallel,
        _nonempty_string(
            github.get("github_repository"),
            f"timing report {path} github_repository",
        ),
        _nonempty_string(
            github.get("imageos"), f"timing report {path} imageos"
        ),
        _nonempty_string(
            github.get("imageversion"), f"timing report {path} imageversion"
        ),
        _nonempty_string(
            github.get("runner_arch"), f"timing report {path} runner_arch"
        ),
        runner_environment,
        _nonempty_string(
            github.get("runner_os"), f"timing report {path} runner_os"
        ),
    )
    return sha, signature


def _verify_sample_protocol(
    document: Mapping[str, object],
    path: Path,
    sample_count: int,
) -> None:
    samples = document.get("samples")
    if not isinstance(samples, list) or len(samples) != sample_count:
        raise ParityError(
            f"timing report {path} must contain {sample_count} sample records"
        )
    for index, sample in enumerate(samples, start=1):
        context = f"timing report {path} sample #{index}"
        if not isinstance(sample, dict) or sample.get("index") != index:
            raise ParityError(f"{context} has invalid ordering or shape")
        if sample.get("ctest_returncode") != 0:
            raise ParityError(f"{context} did not complete with CTest exit zero")
        for load_key in ("load_before", "load_after"):
            load = sample.get(load_key)
            if not isinstance(load, dict) or load.get("available") is not True:
                raise ParityError(f"{context} omits {load_key} diagnostics")
            for field in ("load_1m", "load_5m", "load_15m"):
                value = load.get(field)
                if (
                    not isinstance(value, (int, float))
                    or isinstance(value, bool)
                    or value < 0
                ):
                    raise ParityError(
                        f"{context} has invalid {load_key}.{field}"
                    )

    cost_data = document.get("test_cost_data")
    if (
        not isinstance(cost_data, dict)
        or cost_data.get("reset_before_each_sample") is not True
        or cost_data.get("restored_after_collection") is not True
    ):
        raise ParityError(
            f"timing report {path} did not reset and restore CTest cost data"
        )
    digest = document.get("selection_digest")
    if not isinstance(digest, str) or re.fullmatch(r"[0-9a-f]{64}", digest) is None:
        raise ParityError(f"timing report {path} has invalid selection_digest")


def _read_report(path: Path, expected_cohort: str) -> TimingReport:
    document = _read_json_object(path, "timing report")
    if document.get("schema") != REPORT_SCHEMA:
        raise ParityError(
            f"timing report {path} schema must be {REPORT_SCHEMA!r}, "
            f"got {document.get('schema')!r}"
        )

    identity = document.get("identity")
    if not isinstance(identity, dict):
        raise ParityError(f"timing report {path} omits its identity object")
    parallel_jobs = _positive_int(
        identity.get("parallel_jobs"),
        f"timing report {path} identity parallel_jobs",
    )
    expected_identity = {
        **EXPECTED_REPORT_IDENTITIES[expected_cohort],
        "cohort": expected_cohort,
        "parallel_jobs": parallel_jobs,
    }
    if identity != expected_identity:
        raise ParityError(
            f"timing report {path} identity drifted: "
            f"expected={expected_identity!r}, actual={identity!r}"
        )

    summary = document.get("summary")
    if not isinstance(summary, dict):
        raise ParityError(f"timing report {path} omits its summary object")
    if summary.get("valid") is not True:
        raise ParityError(f"timing report {path} is not marked valid")
    sample_count = _positive_int(
        summary.get("sample_count"), f"timing report {path} sample_count"
    )
    if sample_count < MIN_SAMPLE_COUNT:
        raise ParityError(
            f"timing report {path} requires at least {MIN_SAMPLE_COUNT} samples; "
            f"got {sample_count}"
        )
    sha, runner_signature = _read_runner_identity(
        document, path, parallel_jobs
    )
    _verify_sample_protocol(document, path, sample_count)

    raw_cases = document.get("cases")
    if not isinstance(raw_cases, list) or not raw_cases:
        raise ParityError(f"timing report {path} must contain a nonempty cases list")
    cases: dict[str, Mapping[str, object]] = {}
    for index, record in enumerate(raw_cases):
        context = f"timing report {path} case #{index + 1}"
        if not isinstance(record, dict):
            raise ParityError(f"{context} must be an object")
        name = record.get("name")
        if not isinstance(name, str) or not name:
            raise ParityError(f"{context} must have a nonempty name")
        if name in cases:
            raise ParityError(f"timing report {path} repeats case {name!r}")
        labels = _string_list(
            record.get("labels"),
            context=f"{context} labels",
            require_nonempty=True,
        )
        if not isinstance(record.get("disabled"), bool):
            raise ParityError(f"{context} disabled must be boolean")
        _nonempty_string(record.get("executable"), f"{context} executable")
        raw_statuses = record.get("statuses")
        if not isinstance(raw_statuses, list) or not raw_statuses or any(
            not isinstance(status, str) or not status for status in raw_statuses
        ):
            raise ParityError(
                f"{context} statuses must be a nonempty list of strings"
            )
        statuses = tuple(raw_statuses)
        if len(statuses) != sample_count:
            raise ParityError(
                f"{context} has {len(statuses)} statuses for "
                f"{sample_count} report samples"
            )
        unsupported = sorted(set(statuses) - VALID_STATUSES)
        if unsupported:
            raise ParityError(f"{context} has unsupported statuses {unsupported!r}")
        duration = record.get("duration_us")
        if not isinstance(duration, dict):
            raise ParityError(f"{context} omits duration_us")
        duration_samples = duration.get("samples")
        if (
            not isinstance(duration_samples, list)
            or len(duration_samples) != sample_count
            or any(
                not isinstance(value, int)
                or isinstance(value, bool)
                or value < 0
                for value in duration_samples
            )
        ):
            raise ParityError(
                f"{context} duration samples must match report sample_count"
            )
        for statistic in ("median", "p95"):
            value = duration.get(statistic)
            if not isinstance(value, int) or isinstance(value, bool) or value < 0:
                raise ParityError(f"{context} has invalid duration {statistic}")
        cases[name] = {**record, "labels": labels, "statuses": statuses}

    selected_count = _positive_int(
        summary.get("selected_test_count"),
        f"timing report {path} selected_test_count",
    )
    if selected_count != len(cases):
        raise ParityError(
            f"timing report {path} selected_test_count is {selected_count}, "
            f"but cases contains {len(cases)} records"
        )
    return TimingReport(
        cases=cases,
        sample_count=sample_count,
        sha=sha,
        runner_signature=runner_signature,
    )


def _require_all_passed(
    report: TimingReport, names: Sequence[str], context: str
) -> None:
    for name in names:
        statuses = report.cases[name]["statuses"]
        if any(status != "passed" for status in statuses):
            raise ParityError(
                f"{context} case {name!r} must pass every sample without skips; "
                f"got {list(statuses)!r}"
            )


def _verify_transition(
    *,
    baseline: TimingReport,
    candidate: TimingReport,
    moved: Sequence[str],
    sentinels: Sequence[str],
    context: str,
) -> None:
    baseline_names = set(baseline.cases)
    candidate_names = set(candidate.cases)
    declared_moved = set(moved)
    actual_moved = baseline_names - candidate_names
    if actual_moved != declared_moved:
        raise ParityError(
            f"{context} removals differ from moved_to_slow: "
            f"undeclared={sorted(actual_moved - declared_moved)!r}, "
            f"not_removed={sorted(declared_moved - actual_moved)!r}"
        )

    declared_sentinels = set(sentinels)
    actual_added = candidate_names - baseline_names
    if actual_added != declared_sentinels:
        raise ParityError(
            f"{context} additions differ from added_fast_sentinels: "
            f"undeclared={sorted(actual_added - declared_sentinels)!r}, "
            f"missing={sorted(declared_sentinels - actual_added)!r}"
        )


def _verify_comparable_reports(
    baseline_cpu: TimingReport,
    baseline_pr_fast: TimingReport,
    candidate_cpu: TimingReport,
    candidate_pr_fast: TimingReport,
    candidate_slow: TimingReport,
) -> None:
    if baseline_cpu.sha != baseline_pr_fast.sha:
        raise ParityError("baseline CPU and PR-fast reports use different SHAs")
    candidate_reports = (candidate_cpu, candidate_pr_fast, candidate_slow)
    if len({report.sha for report in candidate_reports}) != 1:
        raise ParityError("candidate CPU, PR-fast, and slow reports use different SHAs")
    reports = (
        baseline_pr_fast,
        candidate_cpu,
        candidate_pr_fast,
        candidate_slow,
    )
    if any(
        report.runner_signature != baseline_cpu.runner_signature
        for report in reports
    ):
        raise ParityError(
            "timing reports do not share the same hosted runner protocol"
        )


def _local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def _junit_status(testcase: ElementTree.Element, context: str) -> str:
    outcomes = [
        _local_name(child.tag)
        for child in testcase
        if _local_name(child.tag) in {"error", "failure", "skipped"}
    ]
    if len(outcomes) > 1:
        raise ParityError(f"{context} has multiple JUnit outcomes")
    if outcomes:
        return {
            "error": "error",
            "failure": "failed",
            "skipped": "skipped",
        }[outcomes[0]]
    status = (testcase.get("status") or "run").lower()
    if status == "run":
        return "passed"
    if status == "disabled":
        return "disabled"
    raise ParityError(f"{context} has unsupported JUnit status {status!r}")


def _verify_scheduled_junit(path: Path, moved: Sequence[str]) -> None:
    if not path.is_file() or path.stat().st_size == 0:
        raise ParityError(f"scheduled slow JUnit is missing or empty: {path}")
    try:
        root = ElementTree.parse(path).getroot()
    except (OSError, ElementTree.ParseError) as error:
        raise ParityError(f"cannot parse scheduled slow JUnit {path}: {error}") from error
    if _local_name(root.tag) not in {"testsuite", "testsuites"}:
        raise ParityError(f"{path}: unsupported JUnit root {root.tag!r}")

    statuses: dict[str, str] = {}
    for testcase in root.iter():
        if _local_name(testcase.tag) != "testcase":
            continue
        name = testcase.get("name")
        if not name:
            raise ParityError(f"{path}: JUnit testcase omits its name")
        if name in statuses:
            raise ParityError(f"{path}: JUnit repeats testcase {name!r}")
        statuses[name] = _junit_status(testcase, f"JUnit testcase {name!r}")

    moved_set = set(moved)
    scheduled_set = set(statuses)
    missing = sorted(moved_set - scheduled_set)
    extra = sorted(scheduled_set - moved_set)
    if missing or extra:
        raise ParityError(
            "scheduled slow JUnit must contain exactly declared moved cases; "
            f"missing={missing!r}, extra={extra!r}"
        )
    not_passed = {
        name: statuses[name] for name in moved if statuses[name] != "passed"
    }
    if not_passed:
        raise ParityError(
            "scheduled slow JUnit moved cases must pass without skips: "
            f"{not_passed!r}"
        )


def _compare(
    *,
    manifest: Path,
    baseline_cpu_path: Path,
    baseline_pr_fast_path: Path,
    candidate_cpu_path: Path,
    candidate_pr_fast_path: Path,
    candidate_slow_path: Path,
    scheduled_slow_junit: Path,
) -> dict[str, int]:
    try:
        transition = read_test_cohort_manifest(manifest)
    except CohortManifestError as error:
        raise ParityError(str(error)) from error
    moved = transition.moved_to_slow
    sentinels = transition.added_fast_sentinels
    baseline_cpu = _read_report(baseline_cpu_path, "cpu")
    baseline_pr_fast = _read_report(baseline_pr_fast_path, "pr-fast")
    candidate_cpu = _read_report(candidate_cpu_path, "cpu")
    candidate_pr_fast = _read_report(candidate_pr_fast_path, "pr-fast")
    candidate_slow = _read_report(candidate_slow_path, "cpu-slow")
    _verify_comparable_reports(
        baseline_cpu,
        baseline_pr_fast,
        candidate_cpu,
        candidate_pr_fast,
        candidate_slow,
    )

    _verify_transition(
        baseline=baseline_cpu,
        candidate=candidate_cpu,
        moved=moved,
        sentinels=sentinels,
        context="baseline-to-candidate CPU",
    )
    _verify_transition(
        baseline=baseline_pr_fast,
        candidate=candidate_pr_fast,
        moved=moved,
        sentinels=sentinels,
        context="baseline-to-candidate PR-fast",
    )

    cpu_names = set(candidate_cpu.cases)
    pr_fast_names = set(candidate_pr_fast.cases)
    slow_names = set(candidate_slow.cases)
    unexpected_pr_fast = sorted(pr_fast_names - cpu_names)
    if unexpected_pr_fast:
        raise ParityError(
            f"candidate PR-fast contains cases outside CPU: {unexpected_pr_fast!r}"
        )
    overlap = sorted(cpu_names.intersection(slow_names))
    if overlap:
        raise ParityError(
            f"candidate CPU and slow reports overlap on cases {overlap!r}"
        )

    declared_moved = set(moved)
    unexpected_slow = sorted(slow_names - declared_moved)
    missing_slow = sorted(declared_moved - slow_names)
    if missing_slow or unexpected_slow:
        raise ParityError(
            "candidate slow report must contain exactly moved_to_slow: "
            f"missing={missing_slow!r}, unexpected={unexpected_slow!r}"
        )

    for name in moved:
        baseline_labels = set(baseline_cpu.cases[name]["labels"])
        slow_labels = set(candidate_slow.cases[name]["labels"])
        expected_labels = baseline_labels | {"slow"}
        if "slow" in baseline_labels or slow_labels != expected_labels:
            raise ParityError(
                f"moved case {name!r} labels must transition only by adding "
                f"'slow': baseline={sorted(baseline_labels)!r}, "
                f"candidate={sorted(slow_labels)!r}"
            )

    _require_all_passed(baseline_cpu, moved, "baseline CPU")
    _require_all_passed(baseline_pr_fast, moved, "baseline PR-fast")
    _require_all_passed(candidate_cpu, sentinels, "candidate CPU sentinel")
    _require_all_passed(
        candidate_pr_fast, sentinels, "candidate PR-fast sentinel"
    )
    _require_all_passed(candidate_slow, moved, "candidate slow")
    _verify_scheduled_junit(scheduled_slow_junit, moved)
    return {
        "added_fast_sentinel_count": len(sentinels),
        "baseline_cpu_case_count": len(baseline_cpu.cases),
        "baseline_pr_fast_case_count": len(baseline_pr_fast.cases),
        "candidate_cpu_case_count": len(candidate_cpu.cases),
        "candidate_pr_fast_case_count": len(candidate_pr_fast.cases),
        "candidate_slow_case_count": len(slow_names),
        "moved_case_count": len(moved),
    }


def _parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--baseline-cpu", required=True, type=Path)
    parser.add_argument("--baseline-pr-fast", required=True, type=Path)
    parser.add_argument("--candidate-cpu", required=True, type=Path)
    parser.add_argument("--candidate-pr-fast", required=True, type=Path)
    parser.add_argument("--candidate-slow", required=True, type=Path)
    parser.add_argument("--scheduled-slow-junit", required=True, type=Path)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = _parse_args(sys.argv[1:] if argv is None else argv)
    try:
        summary = _compare(
            manifest=arguments.manifest,
            baseline_cpu_path=arguments.baseline_cpu,
            baseline_pr_fast_path=arguments.baseline_pr_fast,
            candidate_cpu_path=arguments.candidate_cpu,
            candidate_pr_fast_path=arguments.candidate_pr_fast,
            candidate_slow_path=arguments.candidate_slow,
            scheduled_slow_junit=arguments.scheduled_slow_junit,
        )
    except ParityError as error:
        print(f"BLOCKED: {error}", file=sys.stderr)
        return 3
    print(json.dumps(summary, ensure_ascii=True, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
