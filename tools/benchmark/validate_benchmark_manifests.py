#!/usr/bin/env python3
"""Validate benchmark manifests under benchmarks/**."""

from __future__ import annotations

import argparse
from pathlib import Path

try:
    import yaml
except ImportError as exc:  # pragma: no cover - dependency issue path
    print("ERROR: PyYAML is required. Install with `python3 -m pip install pyyaml`.")
    raise SystemExit(2) from exc

REQUIRED_FIELDS = {
    "benchmark_id",
    "method",
    "dataset",
    "params",
    "metrics",
    "thresholds",
}

PLACEHOLDER_PREFIXES = ("TODO:", "TBD", "PLACEHOLDER:")

ALLOWED_METRICS = {
    "runtime_ms",
    "memory_peak_bytes",
    "quality_error_l2",
    "quality_error_linf",
    "throughput_items_per_sec",
    "gpu_time_ms",
}


def _is_placeholder(value: str) -> bool:
    return any(value.strip().startswith(prefix) for prefix in PLACEHOLDER_PREFIXES)


def _load_manifest(path: Path) -> dict:
    try:
        with path.open("r", encoding="utf-8") as handle:
            data = yaml.safe_load(handle)
    except yaml.YAMLError as exc:
        raise ValueError(f"invalid YAML ({exc})") from exc

    if not isinstance(data, dict):
        raise ValueError("manifest root must be a YAML mapping/object")

    return data


def _validate_field_types(data: dict, errors: list[str]) -> None:
    if not isinstance(data.get("benchmark_id"), str) or not data["benchmark_id"].strip():
        errors.append("field 'benchmark_id' must be a non-empty string")

    method = data.get("method")
    if not isinstance(method, str) or not method.strip():
        errors.append("field 'method' must be a non-empty string")

    dataset = data.get("dataset")
    if not isinstance(dataset, str) or not dataset.strip():
        errors.append("field 'dataset' must be a non-empty string")

    if not isinstance(data.get("params"), dict):
        errors.append("field 'params' must be a mapping/object")

    metrics = data.get("metrics")
    if not isinstance(metrics, list) or not metrics:
        errors.append("field 'metrics' must be a non-empty list")
    else:
        invalid_metrics = [m for m in metrics if not isinstance(m, str) or m not in ALLOWED_METRICS]
        if invalid_metrics:
            errors.append(
                "field 'metrics' contains invalid value(s): "
                + ", ".join(str(v) for v in invalid_metrics)
                + "; allowed metrics: "
                + ", ".join(sorted(ALLOWED_METRICS))
            )

    if not isinstance(data.get("thresholds"), dict):
        errors.append("field 'thresholds' must be a mapping/object")


def validate_manifest(path: Path, strict: bool = False) -> tuple[str | None, list[str]]:
    errors: list[str] = []

    try:
        data = _load_manifest(path)
    except ValueError as exc:
        return None, [f"{path}: {exc}"]

    missing = sorted(REQUIRED_FIELDS.difference(data.keys()))
    if missing:
        errors.append(f"missing required fields: {', '.join(missing)}")

    _validate_field_types(data, errors)

    benchmark_id = data.get("benchmark_id") if isinstance(data.get("benchmark_id"), str) else None

    method = data.get("method")
    dataset = data.get("dataset")

    if isinstance(method, str) and strict and not _is_placeholder(method) and "." not in method:
        errors.append("field 'method' should use a namespaced ID like 'geometry.example'")

    if isinstance(dataset, str) and strict and not _is_placeholder(dataset) and "." not in dataset:
        errors.append("field 'dataset' should use a namespaced ID like 'builtin.mesh.small'")

    return benchmark_id, [f"{path}: {e}" for e in errors]


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate IntrinsicEngine benchmark manifests.")
    parser.add_argument("--root", type=Path, default=Path("benchmarks"), help="Benchmarks root directory")
    parser.add_argument("--strict", action="store_true", help="Enable strict schema checks")
    args = parser.parse_args()

    if not args.root.exists():
        print(f"ERROR: root path does not exist: {args.root}")
        return 2

    manifests = sorted(args.root.rglob("*.yaml"))
    if not manifests:
        print(f"WARNING: no benchmark manifests found under {args.root}")
        return 0

    all_errors: list[str] = []
    ids: dict[str, Path] = {}

    for manifest in manifests:
        benchmark_id, errors = validate_manifest(manifest, strict=args.strict)
        all_errors.extend(errors)

        if benchmark_id:
            if benchmark_id in ids:
                all_errors.append(
                    f"duplicate benchmark_id '{benchmark_id}' found in {ids[benchmark_id]} and {manifest}"
                )
            else:
                ids[benchmark_id] = manifest

    if all_errors:
        print("Benchmark manifest validation FAILED:")
        for err in all_errors:
            print(f" - {err}")
        return 1

    print(f"Benchmark manifest validation passed for {len(manifests)} file(s).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
