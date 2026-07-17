#!/usr/bin/env python3
"""Collect repeatable per-case CTest timing evidence for named CPU cohorts."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import platform
import shlex
import subprocess
import sys
import time
import xml.etree.ElementTree as ElementTree
from collections import Counter
from dataclasses import dataclass
from datetime import datetime, timezone
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
from pathlib import Path
from typing import Mapping, Sequence

import cpu_test_selection


REPORT_SCHEMA = "intrinsic.test-timing/v1"
MICROSECONDS_PER_SECOND = Decimal(1_000_000)


class TimingError(RuntimeError):
    pass


@dataclass(frozen=True)
class Cohort:
    aggregate: str
    include_any: tuple[str, ...]
    exclude_any: tuple[str, ...]
    timeout_seconds: int


COHORTS = {
    "pr-fast": Cohort(
        aggregate="IntrinsicPrFastTests",
        include_any=("contract", "unit"),
        exclude_any=("flaky-quarantine", "gpu", "slow", "vulkan"),
        timeout_seconds=60,
    ),
    "cpu": Cohort(
        aggregate="IntrinsicCpuTests",
        include_any=(),
        exclude_any=("flaky-quarantine", "gpu", "slow", "vulkan"),
        timeout_seconds=60,
    ),
    "cpu-slow": Cohort(
        aggregate="IntrinsicCpuSlowTests",
        include_any=("slow",),
        exclude_any=("benchmark", "flaky-quarantine", "gpu", "slo", "vulkan"),
        timeout_seconds=120,
    ),
}


def _timestamp_utc() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(value, ensure_ascii=True, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def _digest(value: object) -> str:
    encoded = json.dumps(
        value, ensure_ascii=True, separators=(",", ":"), sort_keys=True
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _prepare_output(path: Path) -> Path:
    path = path.resolve()
    if path.exists():
        if not path.is_dir():
            raise TimingError(f"output path exists and is not a directory: {path}")
        try:
            entries = list(path.iterdir())
        except OSError as error:
            raise TimingError(f"cannot inspect output directory {path}: {error}") from error
        if entries:
            raise TimingError(f"output directory must be absent or empty: {path}")
    else:
        try:
            path.mkdir(parents=True)
        except OSError as error:
            raise TimingError(f"cannot create output directory {path}: {error}") from error
    (path / "samples").mkdir()
    return path


def _label_matches(labels: Sequence[str], cohort: Cohort) -> bool:
    values = set(labels)
    if cohort.include_any and not values.intersection(cohort.include_any):
        return False
    return not values.intersection(cohort.exclude_any)


def _command_for_test(test: Mapping[str, object]) -> list[str]:
    raw_command = test.get("command")
    if not isinstance(raw_command, list) or not raw_command or any(
        not isinstance(argument, str) for argument in raw_command
    ):
        raise TimingError(f"CTest test {test.get('name')!r} has no valid command")
    return raw_command


def _capture_inventory(
    build_dir: Path, cohort: Cohort
) -> tuple[list[dict[str, object]], dict[str, tuple[str, ...]]]:
    inventory_dir = build_dir / "test-inventories"
    try:
        registered = cpu_test_selection._read_tsv(  # noqa: SLF001
            inventory_dir / "RegisteredTestTargets.tsv"
        )
        members = cpu_test_selection._read_aggregate(  # noqa: SLF001
            inventory_dir / f"{cohort.aggregate}.txt"
        )
        document = cpu_test_selection._ctest_document(build_dir)  # noqa: SLF001
    except cpu_test_selection.SelectionError as error:
        raise TimingError(str(error)) from error

    expected_members = {
        target
        for target, labels in registered.items()
        if _label_matches(labels, cohort)
    }
    missing_members = sorted(expected_members - set(members))
    extra_members = sorted(set(members) - expected_members)
    unknown_members = sorted(set(members) - registered.keys())
    if missing_members or extra_members or unknown_members:
        raise TimingError(
            f"{cohort.aggregate} disagrees with its canonical label predicate: "
            f"missing={missing_members!r}, extra={extra_members!r}, "
            f"unknown={unknown_members!r}"
        )

    binaries: dict[Path, str] = {}
    for target in registered:
        for suffix in ("", ".exe"):
            binary = Path(
                os.path.realpath(
                    os.path.abspath(build_dir / "bin" / f"{target}{suffix}")
                )
            )
            previous = binaries.get(binary)
            if previous is not None and previous != target:
                raise TimingError(
                    f"registered producer paths collide: {previous!r} and {target!r}"
                )
            binaries[binary] = target

    selected: list[dict[str, object]] = []
    seen_names: set[str] = set()
    producer_counts = Counter[str]()
    for raw_test in document["tests"]:
        if not isinstance(raw_test, dict):
            raise TimingError("CTest JSON contains a malformed test record")
        name = raw_test.get("name")
        if not isinstance(name, str) or not name:
            raise TimingError("CTest JSON contains a test without a name")
        if name in seen_names:
            raise TimingError(f"CTest inventory repeats test name {name!r}")
        seen_names.add(name)

        try:
            labels = cpu_test_selection._labels(raw_test)  # noqa: SLF001
            disabled = cpu_test_selection._disabled(raw_test)  # noqa: SLF001
        except cpu_test_selection.SelectionError as error:
            raise TimingError(str(error)) from error

        matches = _label_matches(labels, cohort)
        if not matches:
            continue
        try:
            command = _command_for_test(raw_test)
            producer = cpu_test_selection._producer_for_command(  # noqa: SLF001
                command, binaries, name
            )
        except cpu_test_selection.SelectionError as error:
            raise TimingError(str(error)) from error

        if producer is not None and labels != registered[producer]:
            raise TimingError(
                f"CTest test {name!r} labels differ from producer {producer!r}: "
                f"expected={registered[producer]!r}, actual={labels!r}"
            )
        if producer is None:
            raise TimingError(
                f"selected CTest test {name!r} does not map to a registered producer"
            )
        if producer not in members:
            raise TimingError(
                f"selected CTest test {name!r} maps outside "
                f"{cohort.aggregate}: {producer!r}"
            )
        selected.append(
            {
                "disabled": disabled,
                "labels": list(labels),
                "name": name,
                "producer": producer,
            }
        )
        producer_counts[producer] += 1

    if not selected:
        raise TimingError(f"cohort {cohort.aggregate} selected zero CTest cases")
    producers_without_cases = sorted(set(members) - producer_counts.keys())
    if producers_without_cases:
        raise TimingError(
            f"{cohort.aggregate} producers have no selected CTest cases: "
            f"{producers_without_cases!r}"
        )
    selected.sort(key=lambda record: str(record["name"]))
    return selected, registered


def _load_average() -> dict[str, object]:
    try:
        one, five, fifteen = os.getloadavg()
    except (AttributeError, OSError):
        return {"available": False}
    diagnostics: dict[str, object] = {
        "available": True,
        "load_1m": round(one, 3),
        "load_5m": round(five, 3),
        "load_15m": round(fifteen, 3),
    }
    try:
        fields = Path("/proc/loadavg").read_text(encoding="ascii").split()
    except OSError:
        fields = []
    if len(fields) >= 4 and "/" in fields[3]:
        runnable, total = fields[3].split("/", 1)
        if runnable.isdigit() and total.isdigit():
            diagnostics["runnable_processes"] = int(runnable)
            diagnostics["total_processes"] = int(total)
    return diagnostics


def _host_identity(parallel: int) -> dict[str, object]:
    github_fields = {
        key.lower(): os.environ[key]
        for key in (
            "GITHUB_REF",
            "GITHUB_REPOSITORY",
            "GITHUB_RUN_ATTEMPT",
            "GITHUB_RUN_ID",
            "GITHUB_SHA",
            "ImageOS",
            "ImageVersion",
            "RUNNER_ARCH",
            "RUNNER_ENVIRONMENT",
            "RUNNER_OS",
        )
        if os.environ.get(key)
    }
    return {
        "github": github_fields,
        "logical_cpu_count": os.cpu_count(),
        "machine": platform.machine(),
        "parallel_jobs": parallel,
        "python": platform.python_version(),
        "release": platform.release(),
        "system": platform.system(),
    }


def _local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def _duration_us(raw_value: str | None, context: str) -> int:
    try:
        value = Decimal(raw_value or "")
    except InvalidOperation as error:
        raise TimingError(f"{context} has invalid JUnit time {raw_value!r}") from error
    if not value.is_finite() or value < 0:
        raise TimingError(f"{context} has invalid JUnit time {raw_value!r}")
    return int(
        (value * MICROSECONDS_PER_SECOND).quantize(
            Decimal(1), rounding=ROUND_HALF_UP
        )
    )


def _junit_labels(testcase: ElementTree.Element, context: str) -> tuple[str, ...]:
    values = [
        property_node.get("value")
        for property_node in testcase.iter()
        if _local_name(property_node.tag) == "property"
        and property_node.get("name") == "cmake_labels"
    ]
    if len(values) != 1 or values[0] is None:
        raise TimingError(f"{context} must have exactly one cmake_labels property")
    labels = tuple(sorted(filter(None, values[0].split(";"))))
    if not labels or len(labels) != len(set(labels)):
        raise TimingError(f"{context} has invalid or duplicate JUnit labels")
    return labels


def _junit_status(testcase: ElementTree.Element, context: str) -> str:
    outcomes = Counter(
        _local_name(node.tag)
        for node in testcase
        if _local_name(node.tag) in {"error", "failure", "skipped"}
    )
    if sum(outcomes.values()) > 1:
        raise TimingError(f"{context} has multiple JUnit outcome records")
    if outcomes["error"]:
        return "error"
    if outcomes["failure"]:
        return "failed"
    if outcomes["skipped"]:
        return "skipped"

    status = (testcase.get("status") or "run").lower()
    if status == "run":
        return "passed"
    if status == "disabled":
        return "disabled"
    raise TimingError(f"{context} has unsupported JUnit status {status!r}")


def _parse_junit(
    path: Path, expected: Sequence[Mapping[str, object]]
) -> list[dict[str, object]]:
    if not path.is_file() or path.stat().st_size == 0:
        raise TimingError(f"CTest did not create nonempty JUnit output {path}")
    try:
        root = ElementTree.parse(path).getroot()
    except (OSError, ElementTree.ParseError) as error:
        raise TimingError(f"cannot parse CTest JUnit output {path}: {error}") from error
    if _local_name(root.tag) not in {"testsuite", "testsuites"}:
        raise TimingError(f"{path}: unsupported JUnit root {root.tag!r}")

    expected_by_name = {str(record["name"]): record for record in expected}
    actual: dict[str, dict[str, object]] = {}
    for testcase in root.iter():
        if _local_name(testcase.tag) != "testcase":
            continue
        name = testcase.get("name")
        if not name:
            raise TimingError(f"{path}: JUnit testcase omits its name")
        if name in actual:
            raise TimingError(f"{path}: JUnit repeats testcase {name!r}")
        if name not in expected_by_name:
            raise TimingError(f"{path}: JUnit contains unexpected testcase {name!r}")
        context = f"JUnit testcase {name!r}"
        expected_record = expected_by_name[name]
        labels = _junit_labels(testcase, context)
        if labels != tuple(expected_record["labels"]):
            raise TimingError(
                f"{context} labels drifted: "
                f"expected={expected_record['labels']!r}, actual={labels!r}"
            )
        status = _junit_status(testcase, context)
        expected_disabled = bool(expected_record["disabled"])
        if expected_disabled != (status == "disabled"):
            raise TimingError(
                f"{context} disabled status drifted: "
                f"expected_disabled={expected_disabled}, actual={status!r}"
            )
        actual[name] = {
            "duration_us": _duration_us(testcase.get("time"), context),
            "status": status,
        }

    missing = sorted(expected_by_name.keys() - actual.keys())
    if missing:
        raise TimingError(f"{path}: JUnit omits selected testcases {missing!r}")
    return [
        {"name": name, **actual[name]}
        for name in sorted(actual)
    ]


def _median(values: Sequence[int]) -> int:
    ordered = sorted(values)
    middle = len(ordered) // 2
    if len(ordered) % 2:
        return ordered[middle]
    return (ordered[middle - 1] + ordered[middle]) // 2


def _p95(values: Sequence[int]) -> int:
    ordered = sorted(values)
    return ordered[math.ceil(len(ordered) * 0.95) - 1]


def _cost_data_path(build_dir: Path) -> Path:
    return build_dir / "Testing" / "Temporary" / "CTestCostData.txt"


def _read_optional_bytes(path: Path) -> bytes | None:
    try:
        return path.read_bytes()
    except FileNotFoundError:
        return None
    except OSError as error:
        raise TimingError(f"cannot read CTest cost data {path}: {error}") from error


def _restore_cost_data(path: Path, contents: bytes | None) -> None:
    try:
        if contents is None:
            path.unlink(missing_ok=True)
            return
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_suffix(path.suffix + ".tmp")
        temporary.write_bytes(contents)
        temporary.replace(path)
    except OSError as error:
        raise TimingError(f"cannot restore CTest cost data {path}: {error}") from error


def _ctest_command(
    build_dir: Path, cohort: Cohort, parallel: int, junit_path: Path
) -> list[str]:
    command = [
        "ctest",
        "--test-dir",
        str(build_dir),
        "--output-on-failure",
    ]
    if cohort.include_any:
        command.extend(("-L", f"^({'|'.join(cohort.include_any)})$"))
    command.extend(
        (
            "-LE",
            f"^({'|'.join(cohort.exclude_any)})$",
            "--no-tests=error",
            "--timeout",
            str(cohort.timeout_seconds),
            "--parallel",
            str(parallel),
            "--output-junit",
            str(junit_path),
        )
    )
    return command


def _run_sample(
    *,
    build_dir: Path,
    cohort: Cohort,
    expected: Sequence[Mapping[str, object]],
    index: int,
    output: Path,
    parallel: int,
) -> tuple[dict[str, object], list[dict[str, object]]]:
    prefix = f"sample-{index:02d}"
    junit_path = output / "samples" / f"{prefix}.junit.xml"
    log_path = output / "samples" / f"{prefix}.log"
    command = _ctest_command(build_dir, cohort, parallel, junit_path)
    started_at = _timestamp_utc()
    load_before = _load_average()
    started = time.monotonic_ns()
    try:
        completed = subprocess.run(
            command,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
    except OSError as error:
        raise TimingError(f"cannot run {shlex.join(command)}: {error}") from error
    wall_time_us = (time.monotonic_ns() - started) // 1_000
    load_after = _load_average()
    finished_at = _timestamp_utc()
    try:
        log_path.write_text(completed.stdout, encoding="utf-8")
    except OSError as error:
        raise TimingError(f"cannot write CTest log {log_path}: {error}") from error
    results = _parse_junit(junit_path, expected)
    return (
        {
            "ctest_returncode": completed.returncode,
            "finished_at_utc": finished_at,
            "index": index,
            "junit": f"samples/{junit_path.name}",
            "load_after": load_after,
            "load_before": load_before,
            "log": f"samples/{log_path.name}",
            "started_at_utc": started_at,
            "wall_time_us": wall_time_us,
        },
        results,
    )


def _build_report(
    *,
    cohort_name: str,
    cohort: Cohort,
    expected: Sequence[Mapping[str, object]],
    sample_records: Sequence[Mapping[str, object]],
    case_samples: Mapping[str, Sequence[Mapping[str, object]]],
    parallel: int,
    original_cost_data: bytes | None,
) -> dict[str, object]:
    cases: list[dict[str, object]] = []
    status_counts = Counter[str]()
    for expected_record in expected:
        name = str(expected_record["name"])
        samples = case_samples[name]
        durations = [int(sample["duration_us"]) for sample in samples]
        statuses = [str(sample["status"]) for sample in samples]
        status_counts.update(statuses)
        cases.append(
            {
                "disabled": bool(expected_record["disabled"]),
                "duration_us": {
                    "median": _median(durations),
                    "p95": _p95(durations),
                    "samples": durations,
                },
                "executable": expected_record["producer"],
                "labels": expected_record["labels"],
                "name": name,
                "statuses": statuses,
            }
        )

    wall_times = [int(sample["wall_time_us"]) for sample in sample_records]
    returncodes = [int(sample["ctest_returncode"]) for sample in sample_records]
    failure_statuses = status_counts["error"] + status_counts["failed"]
    valid = not failure_statuses and all(code == 0 for code in returncodes)
    return {
        "cases": cases,
        "host": _host_identity(parallel),
        "identity": {
            "aggregate": cohort.aggregate,
            "cohort": cohort_name,
            "parallel_jobs": parallel,
            "selector": {
                "exclude_any": list(cohort.exclude_any),
                "include_any": list(cohort.include_any),
            },
            "timeout_seconds": cohort.timeout_seconds,
        },
        "samples": list(sample_records),
        "schema": REPORT_SCHEMA,
        "selection_digest": _digest(expected),
        "summary": {
            "ctest_nonzero_sample_count": sum(code != 0 for code in returncodes),
            "disabled_result_count": status_counts["disabled"],
            "error_result_count": status_counts["error"],
            "failed_result_count": status_counts["failed"],
            "passed_result_count": status_counts["passed"],
            "sample_count": len(sample_records),
            "selected_test_count": len(expected),
            "skipped_result_count": status_counts["skipped"],
            "valid": valid,
            "wall_time_us": {
                "median": _median(wall_times),
                "p95": _p95(wall_times),
                "samples": wall_times,
            },
        },
        "test_cost_data": {
            "baseline_sha256": (
                hashlib.sha256(original_cost_data).hexdigest()
                if original_cost_data is not None
                else None
            ),
            "reset_before_each_sample": True,
            "restored_after_collection": True,
        },
    }


def _collect(
    *,
    build_dir: Path,
    cohort_name: str,
    samples: int,
    output: Path,
    parallel: int,
) -> dict[str, object]:
    build_dir = build_dir.resolve()
    if not build_dir.is_dir():
        raise TimingError(f"configured build directory is missing: {build_dir}")
    cohort = COHORTS[cohort_name]
    expected, _registered = _capture_inventory(build_dir, cohort)
    output = _prepare_output(output)

    cost_path = _cost_data_path(build_dir)
    original_cost_data = _read_optional_bytes(cost_path)
    sample_records: list[dict[str, object]] = []
    case_samples: dict[str, list[dict[str, object]]] = {
        str(record["name"]): [] for record in expected
    }
    try:
        for index in range(1, samples + 1):
            _restore_cost_data(cost_path, original_cost_data)
            sample_record, results = _run_sample(
                build_dir=build_dir,
                cohort=cohort,
                expected=expected,
                index=index,
                output=output,
                parallel=parallel,
            )
            sample_records.append(sample_record)
            for result in results:
                case_samples[str(result["name"])].append(result)
        final_inventory, _registered = _capture_inventory(build_dir, cohort)
        if final_inventory != expected:
            raise TimingError("CTest inventory drifted during timing collection")
    finally:
        _restore_cost_data(cost_path, original_cost_data)

    report = _build_report(
        cohort_name=cohort_name,
        cohort=cohort,
        expected=expected,
        sample_records=sample_records,
        case_samples=case_samples,
        parallel=parallel,
        original_cost_data=original_cost_data,
    )
    _write_json(output / "report.json", report)
    return report


def _default_parallel() -> int:
    try:
        result = subprocess.run(
            ["nproc"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    except OSError:
        result = None
    if result is not None and result.returncode == 0:
        try:
            value = int(result.stdout.strip())
        except ValueError:
            value = 0
        if value > 0:
            return value
    return max(1, os.cpu_count() or 1)


def _positive_int(value: str) -> int:
    parsed = int(value)
    if parsed < 1:
        raise argparse.ArgumentTypeError("expected a positive integer")
    return parsed


def _parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--cohort", choices=sorted(COHORTS), default="pr-fast")
    parser.add_argument("--samples", required=True, type=_positive_int)
    parser.add_argument("--parallel", type=_positive_int, default=_default_parallel())
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = _parse_args(sys.argv[1:] if argv is None else argv)
    try:
        report = _collect(
            build_dir=arguments.build_dir,
            cohort_name=arguments.cohort,
            samples=arguments.samples,
            output=arguments.output,
            parallel=arguments.parallel,
        )
    except TimingError as error:
        print(f"BLOCKED: {error}", file=sys.stderr)
        return 3

    print(
        json.dumps(
            {
                "identity": report["identity"],
                "schema": report["schema"],
                "summary": report["summary"],
            },
            ensure_ascii=True,
            sort_keys=True,
        )
    )
    if not report["summary"]["valid"]:
        print(
            "BLOCKED: one or more measured CTest samples failed; "
            "raw JUnit and logs were retained",
            file=sys.stderr,
        )
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
