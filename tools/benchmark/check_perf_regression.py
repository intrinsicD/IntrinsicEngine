#!/usr/bin/env python3
"""Schema-aware frame-performance comparison for canonical benchmark results."""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from validate_benchmark_results import (  # noqa: E402
    load_json,
    load_manifests,
    validate_result_data,
)


def numeric_metric(metrics: dict, name: str) -> float:
    value = metrics.get(name)
    if type(value) not in {int, float} or not math.isfinite(value):
        raise ValueError(f"metrics.{name} is missing or not finite numeric")
    return float(value)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("result", type=Path)
    parser.add_argument(
        "--manifests-root",
        type=Path,
        default=Path(__file__).resolve().parents[2] / "benchmarks",
    )
    parser.add_argument("--avg-ms", type=float)
    parser.add_argument("--p99-ms", type=float)
    parser.add_argument("--min-fps", type=float)
    args = parser.parse_args()
    if not any(value is not None for value in (args.avg_ms, args.p99_ms, args.min_fps)):
        parser.error("at least one threshold is required")
    try:
        manifests = load_manifests(args.manifests_root)
        data = load_json(args.result)
        _, errors = validate_result_data(
            data, args.result, manifests, args.manifests_root
        )
        if errors:
            raise ValueError("; ".join(errors))
        metrics = data["metrics"]
        checks: list[tuple[str, float, str, float, bool]] = []
        if args.avg_ms is not None:
            actual = numeric_metric(metrics, "avgFrameTimeMs")
            checks.append(
                ("avgFrameTimeMs", actual, "<=", args.avg_ms, actual <= args.avg_ms)
            )
        if args.p99_ms is not None:
            actual = numeric_metric(metrics, "p99FrameTimeMs")
            checks.append(
                ("p99FrameTimeMs", actual, "<=", args.p99_ms, actual <= args.p99_ms)
            )
        if args.min_fps is not None:
            actual = numeric_metric(metrics, "avgFPS")
            checks.append(
                ("avgFPS", actual, ">=", args.min_fps, actual >= args.min_fps)
            )
        frames = metrics.get("totalFrames", "not declared")
    except (OSError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    print("Performance regression comparison")
    print(f"Result: {args.result}")
    print(f"Frames: {frames}")
    failed = False
    for name, actual, operator, limit, passed in checks:
        label = "PASS" if passed else "FAIL"
        print(f"{label}: {name} = {actual:g} {operator} {limit:g}")
        failed = failed or not passed
    print("RESULT: REGRESSION DETECTED" if failed else "RESULT: PASS")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
