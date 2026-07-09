#!/usr/bin/env python3
"""Aggregate configure/build/test phase reports into one CI gate result."""

from __future__ import annotations

import argparse
import json
import os
from datetime import datetime, timezone
from pathlib import Path

BENCHMARK_ID = "ci.gate-latency.github-ubuntu-24.04.v1"
METHOD_ID = "ci.gate-latency"
DATASET_ID = "github.hosted.ubuntu_24_04.x86_64"
RESULT_SCHEMA_VERSION = 1
PHASE_NAMES = ("configure", "build", "test")


def _timestamp_utc() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _parse_bool(value: str) -> bool:
    normalized = value.strip().lower()
    if normalized == "true":
        return True
    if normalized == "false":
        return False
    raise argparse.ArgumentTypeError("expected 'true' or 'false'")


def _nonnegative_int(value: str) -> int:
    parsed = int(value)
    if parsed < 0:
        raise argparse.ArgumentTypeError("expected a non-negative integer")
    return parsed


def _read_phase(path: Path) -> tuple[dict[str, object] | None, str | None]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return None, f"missing phase report: {path}"
    except json.JSONDecodeError as exc:
        return None, f"invalid phase JSON {path}: {exc}"

    if not isinstance(payload, dict):
        return None, f"phase report root must be an object: {path}"

    elapsed = payload.get("elapsed_seconds")
    returncode = payload.get("returncode")
    if isinstance(elapsed, bool) or not isinstance(elapsed, (int, float)) or elapsed < 0:
        return None, f"phase report has invalid elapsed_seconds: {path}"
    if isinstance(returncode, bool) or not isinstance(returncode, int):
        return None, f"phase report has invalid returncode: {path}"
    return payload, None


def _append_summary(result: dict[str, object]) -> None:
    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if not summary_path:
        return

    metrics = result["metrics"]
    diagnostics = result["diagnostics"]
    assert isinstance(metrics, dict)
    assert isinstance(diagnostics, dict)
    with Path(summary_path).open("a", encoding="utf-8") as summary:
        summary.write(f"### CI gate timing: {diagnostics['gate']}\n\n")
        summary.write("| Phase | Duration |\n")
        summary.write("| --- | ---: |\n")
        for phase in PHASE_NAMES:
            summary.write(f"| {phase} | {metrics[f'{phase}_time_ms'] / 1000.0:.3f} s |\n")
        summary.write(f"| measured total | {metrics['total_time_ms'] / 1000.0:.3f} s |\n\n")
        summary.write(f"- status: `{result['status']}`\n")
        summary.write(f"- cache state: `{diagnostics['cache_state']}`\n")
        summary.write(f"- selected tests: `{diagnostics['selected_test_count']}`\n")
        summary.write(f"- Ninja command edges: `{diagnostics['ninja_edge_count']}`\n\n")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    for phase in PHASE_NAMES:
        parser.add_argument(f"--{phase}-json", type=Path, required=True)
    parser.add_argument("--gate", required=True)
    parser.add_argument("--preset", required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--sanitizer", default="none")
    parser.add_argument("--runner-image", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--cache-state", choices=("cold", "warm"), required=True)
    parser.add_argument("--selected-test-count", type=_nonnegative_int)
    parser.add_argument("--ninja-edge-count", type=_nonnegative_int)
    parser.add_argument("--ccache-hit-count", type=_nonnegative_int)
    parser.add_argument("--ccache-miss-count", type=_nonnegative_int)
    parser.add_argument("--vcpkg-cache-hit", type=_parse_bool)
    parser.add_argument("--job-url")
    parser.add_argument("--timestamp-utc", default=None)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    phase_paths = {
        "configure": args.configure_json,
        "build": args.build_json,
        "test": args.test_json,
    }
    phase_payloads: dict[str, dict[str, object]] = {}
    phase_errors: list[str] = []
    for phase, path in phase_paths.items():
        payload, error = _read_phase(path)
        if error:
            phase_errors.append(error)
        elif payload is not None:
            phase_payloads[phase] = payload

    phase_times_ms = {
        phase: int(round(float(phase_payloads.get(phase, {}).get("elapsed_seconds", 0.0)) * 1000.0))
        for phase in PHASE_NAMES
    }
    phase_returncodes = {
        phase: phase_payloads.get(phase, {}).get("returncode") for phase in PHASE_NAMES
    }

    if phase_errors:
        status = "error"
    elif any(code != 0 for code in phase_returncodes.values()):
        status = "failed"
    else:
        status = "passed"

    diagnostics: dict[str, object] = {
        "schema_version": RESULT_SCHEMA_VERSION,
        "gate": args.gate,
        "preset": args.preset,
        "compiler": args.compiler,
        "sanitizer": args.sanitizer,
        "runner_image": args.runner_image,
        "cache_state": args.cache_state,
        "selected_test_count": args.selected_test_count or 0,
        "selected_test_count_available": args.selected_test_count is not None,
        "ninja_edge_count": args.ninja_edge_count or 0,
        "ninja_edge_count_available": args.ninja_edge_count is not None,
        "ccache_hit_count": args.ccache_hit_count or 0,
        "ccache_miss_count": args.ccache_miss_count or 0,
        "ccache_stats_available": (
            args.ccache_hit_count is not None and args.ccache_miss_count is not None
        ),
        "vcpkg_cache_hit": args.vcpkg_cache_hit or False,
        "vcpkg_cache_state_available": args.vcpkg_cache_hit is not None,
        "phase_order": list(PHASE_NAMES),
        "phase_returncodes": phase_returncodes,
        "phase_errors": phase_errors,
        "total_definition": "sum_measured_configure_build_test_phases",
    }
    if args.job_url:
        diagnostics["job_url"] = args.job_url

    result: dict[str, object] = {
        "benchmark_id": BENCHMARK_ID,
        "method": METHOD_ID,
        "backend": "external_baseline",
        "dataset": DATASET_ID,
        "commit": args.commit,
        "timestamp_utc": args.timestamp_utc or _timestamp_utc(),
        "runner": args.gate,
        "metrics": {
            "configure_time_ms": phase_times_ms["configure"],
            "build_time_ms": phase_times_ms["build"],
            "test_time_ms": phase_times_ms["test"],
            "total_time_ms": sum(phase_times_ms.values()),
        },
        "diagnostics": diagnostics,
        "status": status,
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    _append_summary(result)

    for error in phase_errors:
        print(f"ERROR: {error}")
    print(f"Wrote {args.output} ({status})")
    return 0 if status == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
