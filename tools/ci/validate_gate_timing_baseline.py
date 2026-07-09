#!/usr/bin/env python3
"""Validate the CI-003 historical gate-latency baseline and statistics."""

from __future__ import annotations

import argparse
import json
import math
import re
import statistics
from pathlib import Path

SCHEMA_VERSION = 1
SOURCE_BENCHMARK_ID = "ci.gate-latency.github-ubuntu-24.04.v1"
BENCHMARK_ID = f"{SOURCE_BENCHMARK_ID}.aggregate-baseline"
METHOD = "ci.gate-latency"
BACKEND = "external_baseline"
DATASET = "github.hosted.ubuntu_24_04.x86_64"
COMMIT = "historical-multi-commit"
METRICS = (
    "configure_time_ms",
    "build_time_ms",
    "test_time_ms",
    "total_time_ms",
)
EXPECTED_CONTEXTS = {
    "pr-fast": {
        "workflow_id": 267903569,
        "workflow_file": ".github/workflows/pr-fast.yml",
        "preset": "ci",
        "sanitizer": "combined-project-default",
        "selected_test_count": 3526,
        "test_timing_valid": True,
    },
    "ci-linux-clang": {
        "workflow_id": 267907025,
        "workflow_file": ".github/workflows/ci-linux-clang.yml",
        "preset": "ci",
        "sanitizer": "combined-project-default",
        "selected_test_count": 3594,
        "test_timing_valid": True,
    },
    "ci-sanitizers-asan": {
        "workflow_id": 267909997,
        "workflow_file": ".github/workflows/ci-sanitizers.yml",
        "preset": "ci",
        "sanitizer": "asan",
        "selected_test_count": 3592,
        "test_timing_valid": True,
    },
    "ci-sanitizers-ubsan": {
        "workflow_id": 267909997,
        "workflow_file": ".github/workflows/ci-sanitizers.yml",
        "preset": "ci",
        "sanitizer": "ubsan",
        "selected_test_count": 3592,
        "test_timing_valid": True,
    },
    "ci-vulkan": {
        "workflow_id": 276548514,
        "workflow_file": ".github/workflows/ci-vulkan.yml",
        "preset": "ci-vulkan",
        "sanitizer": "combined-project-default",
        "selected_test_count": 60,
        "test_timing_valid": False,
        "test_timing_note": (
            "All sampled test steps failed before the later Xvfb frame-pacing "
            "step existed or executed; use build timing only until post-BUG-064 "
            "samples accumulate."
        ),
    },
    "ci-bench-smoke": {
        "workflow_id": 267917628,
        "workflow_file": ".github/workflows/ci-bench-smoke.yml",
        "preset": "ci",
        "sanitizer": "combined-project-default",
        "selected_test_count": 0,
        "test_timing_valid": True,
        "selected_test_count_applicable": False,
        "test_timing_note": (
            "test_time_ms measures the IntrinsicBenchmarks runner target, "
            "not CTest."
        ),
    },
}
EXPECTED_COMMITS = {
    "0f73194130da128b4a2aa413cd3a2a9b1944f000",
    "e42c0e1d7a48444cb5867c6b92fefaa31676084c",
    "c0c23324cf0fcd73e77f50e2a3c351ed30560951",
    "a0a23f3d616d48f931dc3cf462c8cc1b4faf8670",
    "a168957d4d4c48d1c444a092ba6ce5f9ef137cac",
}


def _nearest_rank_p95(values: list[int]) -> int:
    ordered = sorted(values)
    return ordered[math.ceil(0.95 * len(ordered)) - 1]


def _validate_population(
    population: object,
    statistics_payload: object,
    observed_ids: set[tuple[int, int]],
    errors: list[str],
) -> None:
    if not isinstance(population, dict):
        errors.append("population entry must be an object")
        return

    gate = population.get("gate")
    if not isinstance(gate, str) or not gate:
        errors.append("population gate must be a non-empty string")
        return

    expected_context = EXPECTED_CONTEXTS.get(gate)
    if expected_context is None:
        errors.append(f"{gate}: unexpected gate")
    else:
        for field, expected in expected_context.items():
            if population.get(field) != expected:
                errors.append(
                    f"{gate}: {field} must be {expected!r}, "
                    f"got {population.get(field)!r}"
                )

    samples = population.get("samples")
    if not isinstance(samples, list) or len(samples) < 5:
        errors.append(f"{gate}: at least five samples are required")
        return

    metric_values: dict[str, list[int]] = {metric: [] for metric in METRICS}
    for index, sample in enumerate(samples):
        prefix = f"{gate} sample {index + 1}"
        if not isinstance(sample, dict):
            errors.append(f"{prefix}: sample must be an object")
            continue

        run_id = sample.get("run_id")
        job_id = sample.get("job_id")
        if (
            isinstance(run_id, bool)
            or not isinstance(run_id, int)
            or run_id <= 0
            or isinstance(job_id, bool)
            or not isinstance(job_id, int)
            or job_id <= 0
        ):
            errors.append(f"{prefix}: run_id and job_id must be positive integers")
        elif (run_id, job_id) in observed_ids:
            errors.append(f"{prefix}: duplicate run/job pair")
        else:
            observed_ids.add((run_id, job_id))

        commit = sample.get("commit")
        if not isinstance(commit, str) or re.fullmatch(r"[0-9a-f]{40}", commit) is None:
            errors.append(f"{prefix}: commit must be a full lowercase hexadecimal SHA")

        if sample.get("event") != "pull_request":
            errors.append(f"{prefix}: event must be pull_request")
        if sample.get("conclusion") not in {"success", "failure"}:
            errors.append(f"{prefix}: conclusion must be success or failure")

        metrics = sample.get("metrics")
        if not isinstance(metrics, dict):
            errors.append(f"{prefix}: metrics must be an object")
            continue
        for metric in METRICS:
            value = metrics.get(metric)
            if isinstance(value, bool) or not isinstance(value, int) or value < 0:
                errors.append(f"{prefix}: {metric} must be a non-negative integer")
            else:
                metric_values[metric].append(value)

        if all(isinstance(metrics.get(metric), int) for metric in METRICS):
            expected_total = sum(
                int(metrics[metric])
                for metric in ("configure_time_ms", "build_time_ms", "test_time_ms")
            )
            if metrics["total_time_ms"] != expected_total:
                errors.append(
                    f"{prefix}: total_time_ms must equal configure + build + test"
                )
            job_time = sample.get("job_time_ms")
            if (
                isinstance(job_time, bool)
                or not isinstance(job_time, int)
                or job_time < metrics["total_time_ms"]
            ):
                errors.append(
                    f"{prefix}: job_time_ms must be an integer no smaller than "
                    "total_time_ms"
                )

    if not isinstance(statistics_payload, dict):
        errors.append(f"{gate}: statistics must be an object")
        return

    for metric, values in metric_values.items():
        if len(values) != len(samples):
            continue
        expected = {
            "median": int(statistics.median(values)),
            "p95": _nearest_rank_p95(values),
        }
        if statistics_payload.get(metric) != expected:
            errors.append(
                f"{gate}: {metric} statistics mismatch; "
                f"expected {expected}, got {statistics_payload.get(metric)}"
            )


def validate(path: Path) -> list[str]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return [f"baseline does not exist: {path}"]
    except json.JSONDecodeError as exc:
        return [f"invalid baseline JSON: {exc}"]

    if not isinstance(payload, dict):
        return ["baseline root must be an object"]

    errors: list[str] = []
    expected_root = {
        "schema_version": SCHEMA_VERSION,
        "benchmark_id": BENCHMARK_ID,
        "method": METHOD,
        "backend": BACKEND,
        "dataset": DATASET,
        "commit": COMMIT,
        "status": "passed",
    }
    for field, expected in expected_root.items():
        if payload.get(field) != expected:
            errors.append(
                f"{field} must be {expected!r}, got {payload.get(field)!r}"
            )

    timestamp = payload.get("timestamp_utc")
    if not isinstance(timestamp, str) or not timestamp.endswith("Z"):
        errors.append("timestamp_utc must be a UTC timestamp ending in Z")

    metrics = payload.get("metrics")
    if not isinstance(metrics, dict):
        errors.append("metrics must be an object")
        statistics_by_gate: object = None
    else:
        expected_metric_counts = {
            "population_count": 6,
            "sample_count": 30,
            "warm_population_count": 0,
        }
        for field, expected in expected_metric_counts.items():
            if metrics.get(field) != expected:
                errors.append(
                    f"metrics.{field} must be {expected}, got {metrics.get(field)!r}"
                )
        statistics_by_gate = metrics.get("cold_population_statistics")
    if not isinstance(statistics_by_gate, dict):
        errors.append("metrics.cold_population_statistics must be an object")
        statistics_by_gate = {}

    diagnostics = payload.get("diagnostics")
    if not isinstance(diagnostics, dict):
        errors.append("diagnostics must be an object")
        return errors
    if diagnostics.get("source_benchmark_id") != SOURCE_BENCHMARK_ID:
        errors.append(f"diagnostics.source_benchmark_id must be {SOURCE_BENCHMARK_ID}")

    expected_source_verification = {
        "verified_date_utc": "2026-07-09",
        "api_jobs_verified": 30,
        "api_runs_verified": 25,
        "verified_fields": [
            "workflow_id",
            "run_job_association",
            "commit",
            "event",
            "conclusion",
            "runner_image",
            "phase_durations",
            "job_duration",
            "vcpkg_cache_step",
        ],
    }
    source_verification = diagnostics.get("source_verification")
    if not isinstance(source_verification, dict):
        errors.append("diagnostics.source_verification must be an object")
        source_verification = {}
    elif source_verification != expected_source_verification:
        errors.append("diagnostics.source_verification does not match the API audit")

    methodology = diagnostics.get("methodology")
    if not isinstance(methodology, dict):
        errors.append("methodology must be an object")
    else:
        expected_methodology = {
            "source": "GitHub Actions jobs API step timestamps",
            "runner_image": "ubuntu-24.04",
            "compiler": "clang-20",
            "workflow_generation": "pre-ci003-2026-07-09",
            "sample_time_resolution_seconds": 1,
            "measured_total_definition": "configure_plus_build_plus_test",
            "percentile_method": "nearest_rank",
            "compile_cache_state": "cold",
            "vcpkg_cache_hit": True,
            "warm_compile_sample_count": 0,
        }
        for field, expected in expected_methodology.items():
            if methodology.get(field) != expected:
                errors.append(
                    f"methodology.{field} must be {expected!r}, "
                    f"got {methodology.get(field)!r}"
                )

    populations = diagnostics.get("populations")
    if not isinstance(populations, list):
        errors.append("populations must be a list")
        return errors

    observed_gates = [
        population.get("gate")
        for population in populations
        if isinstance(population, dict)
    ]
    expected_gates = set(EXPECTED_CONTEXTS)
    if set(observed_gates) != expected_gates or len(observed_gates) != len(expected_gates):
        errors.append(
            f"gate set mismatch; expected one each of {sorted(expected_gates)}, "
            f"got {sorted(str(gate) for gate in observed_gates)}"
        )
    if set(statistics_by_gate) != expected_gates:
        errors.append(
            "statistics gate set mismatch; "
            f"expected {sorted(expected_gates)}, got {sorted(statistics_by_gate)}"
        )

    observed_ids: set[tuple[int, int]] = set()
    for population in populations:
        gate = population.get("gate") if isinstance(population, dict) else None
        statistics_payload = (
            statistics_by_gate.get(gate) if isinstance(gate, str) else None
        )
        _validate_population(
            population,
            statistics_payload,
            observed_ids,
            errors,
        )

        if isinstance(population, dict) and isinstance(population.get("samples"), list):
            commits = {
                sample.get("commit")
                for sample in population["samples"]
                if isinstance(sample, dict)
            }
            if commits != EXPECTED_COMMITS:
                errors.append(
                    f"{gate}: commit cohort mismatch; "
                    f"expected {sorted(EXPECTED_COMMITS)}, "
                    f"got {sorted(str(commit) for commit in commits)}"
                )

    if len(observed_ids) != source_verification.get("api_jobs_verified"):
        errors.append("source verification job count does not match retained samples")
    if (
        len({run_id for run_id, _ in observed_ids})
        != source_verification.get("api_runs_verified")
    ):
        errors.append("source verification run count does not match retained samples")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--baseline",
        type=Path,
        default=Path(
            "benchmarks/baselines/ci_gate_latency_github_ubuntu_24_04_v1.json"
        ),
    )
    args = parser.parse_args()

    errors = validate(args.baseline)
    if errors:
        print("CI gate timing baseline validation FAILED:")
        for error in errors:
            print(f" - {error}")
        return 1

    print(f"CI gate timing baseline validation passed: {args.baseline}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
