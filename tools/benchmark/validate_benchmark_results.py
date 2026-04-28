#!/usr/bin/env python3
"""Validate benchmark result JSON payloads under benchmarks/**."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

REQUIRED_FIELDS = {
    "benchmark_id",
    "method",
    "backend",
    "dataset",
    "commit",
    "metrics",
    "diagnostics",
    "status",
}

ALLOWED_BACKENDS = {
    "cpu_reference",
    "cpu_optimized",
    "gpu_vulkan_compute",
    "gpu_vulkan_graphics",
    "cuda_optional",
    "external_baseline",
}

ALLOWED_STATUS = {"passed", "failed", "skipped", "error"}


def _load_json(path: Path) -> dict:
    try:
        with path.open("r", encoding="utf-8") as handle:
            data = json.load(handle)
    except json.JSONDecodeError as exc:
        raise ValueError(f"invalid JSON ({exc})") from exc

    if not isinstance(data, dict):
        raise ValueError("result root must be a JSON object")

    return data


def _is_non_empty_string(value: object) -> bool:
    return isinstance(value, str) and bool(value.strip())


def _validate_metric_payload(metrics: object, strict: bool, errors: list[str]) -> None:
    if not isinstance(metrics, dict):
        errors.append("field 'metrics' must be an object")
        return

    if strict and not metrics:
        errors.append("field 'metrics' must not be empty in strict mode")

    for key, value in metrics.items():
        if not _is_non_empty_string(key):
            errors.append("field 'metrics' contains an empty or non-string metric name")
            continue

        if isinstance(value, (int, float, bool)):
            continue

        if isinstance(value, dict):
            continue

        errors.append(
            f"metric '{key}' has unsupported value type {type(value).__name__}; "
            "expected number, bool, or object"
        )


def validate_result(path: Path, strict: bool = False) -> tuple[str | None, list[str]]:
    errors: list[str] = []

    try:
        data = _load_json(path)
    except ValueError as exc:
        return None, [f"{path}: {exc}"]

    missing = sorted(REQUIRED_FIELDS.difference(data.keys()))
    if missing:
        errors.append(f"missing required fields: {', '.join(missing)}")

    benchmark_id = data.get("benchmark_id") if isinstance(data.get("benchmark_id"), str) else None

    for key in ("benchmark_id", "method", "backend", "dataset", "commit", "status"):
        if key in data and not _is_non_empty_string(data[key]):
            errors.append(f"field '{key}' must be a non-empty string")

    backend = data.get("backend")
    if isinstance(backend, str) and backend not in ALLOWED_BACKENDS:
        errors.append(
            "field 'backend' has unsupported value "
            f"'{backend}'; allowed values: {', '.join(sorted(ALLOWED_BACKENDS))}"
        )

    status = data.get("status")
    if isinstance(status, str) and status not in ALLOWED_STATUS:
        errors.append(
            "field 'status' has unsupported value "
            f"'{status}'; allowed values: {', '.join(sorted(ALLOWED_STATUS))}"
        )

    _validate_metric_payload(data.get("metrics"), strict, errors)

    diagnostics = data.get("diagnostics")
    if not isinstance(diagnostics, dict):
        errors.append("field 'diagnostics' must be an object")

    return benchmark_id, [f"{path}: {e}" for e in errors]


def _discover_results(root: Path) -> list[Path]:
    return sorted(root.rglob("*.json"))


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate IntrinsicEngine benchmark result JSON files.")
    parser.add_argument(
        "--root",
        type=Path,
        default=Path("benchmarks"),
        help="Root directory to scan for benchmark result JSON files",
    )
    parser.add_argument("--strict", action="store_true", help="Enable strict validation checks")
    args = parser.parse_args()

    if not args.root.exists():
        print(f"ERROR: root path does not exist: {args.root}")
        return 2

    results = _discover_results(args.root)
    if not results:
        print(f"WARNING: no benchmark result JSON files found under {args.root}")
        return 0

    all_errors: list[str] = []
    ids: dict[str, Path] = {}

    for result in results:
        benchmark_id, errors = validate_result(result, strict=args.strict)
        all_errors.extend(errors)

        if benchmark_id:
            if benchmark_id in ids:
                all_errors.append(
                    f"duplicate benchmark_id '{benchmark_id}' found in {ids[benchmark_id]} and {result}"
                )
            else:
                ids[benchmark_id] = result

    if all_errors:
        print("Benchmark result JSON validation FAILED:")
        for err in all_errors:
            print(f" - {err}")
        return 1

    print(f"Benchmark result JSON validation passed for {len(results)} file(s).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
