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
CCACHE_STATS_SCHEMA_VERSION = 1
CCACHE_SUMMARY_FIELDS = (
    "hit_count",
    "miss_count",
    "cache_size_kib",
    "error_count",
)
BUILD_CONFIGURATION_FIELDS = (
    ("EXTRINSIC_PLATFORM", "extrinsic_platform"),
    ("EXTRINSIC_BACKEND", "extrinsic_backend"),
    ("INTRINSIC_PLATFORM_BACKEND", "intrinsic_platform_backend"),
    ("INTRINSIC_HEADLESS_NO_GLFW", "intrinsic_headless_no_glfw"),
    (
        "INTRINSIC_PLATFORM_BACKEND_SELECTED",
        "intrinsic_platform_backend_selected",
    ),
)


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
    if (
        isinstance(elapsed, bool)
        or not isinstance(elapsed, (int, float))
        or elapsed < 0
    ):
        return None, f"phase report has invalid elapsed_seconds: {path}"
    if isinstance(returncode, bool) or not isinstance(returncode, int):
        return None, f"phase report has invalid returncode: {path}"
    return payload, None


def _read_ccache_stats(path: Path) -> tuple[dict[str, int] | None, str | None]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return None, f"missing ccache stats JSON: {path}"
    except UnicodeError as exc:
        return None, f"invalid ccache stats encoding {path}: {exc}"
    except json.JSONDecodeError as exc:
        return None, f"invalid ccache stats JSON {path}: {exc}"
    except OSError as exc:
        return None, f"could not read ccache stats JSON {path}: {exc}"

    if not isinstance(payload, dict):
        return None, f"ccache stats root must be an object: {path}"

    schema_version = payload.get("schema_version")
    if (
        isinstance(schema_version, bool)
        or not isinstance(schema_version, int)
        or schema_version != CCACHE_STATS_SCHEMA_VERSION
    ):
        return (
            None,
            "ccache stats schema_version must be integer "
            f"{CCACHE_STATS_SCHEMA_VERSION}: {path}",
        )

    summary = payload.get("summary")
    if not isinstance(summary, dict):
        return None, f"ccache stats summary must be an object: {path}"

    validated: dict[str, int] = {}
    for field in CCACHE_SUMMARY_FIELDS:
        value = summary.get(field)
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            return (
                None,
                f"ccache stats summary.{field} must be a non-negative integer: {path}",
            )
        validated[field] = value

    raw = payload.get("raw")
    if not isinstance(raw, dict):
        return None, f"ccache stats raw counters must be an object: {path}"

    return validated, None


def _read_build_configuration(
    build_dir: Path,
) -> tuple[dict[str, str], list[str]]:
    cache_path = build_dir / "CMakeCache.txt"
    values = {diagnostic_name: "" for _, diagnostic_name in BUILD_CONFIGURATION_FIELDS}
    try:
        text = cache_path.read_text(encoding="utf-8")
    except FileNotFoundError:
        return values, [f"missing configured CMake cache: {cache_path}"]
    except UnicodeError as exc:
        return values, [f"invalid configured CMake cache encoding {cache_path}: {exc}"]
    except OSError as exc:
        return values, [f"could not read configured CMake cache {cache_path}: {exc}"]

    cache_entries: dict[str, str] = {}
    requested_fields = {name for name, _ in BUILD_CONFIGURATION_FIELDS}
    for line in text.splitlines():
        if not line or line.startswith(("#", "//")) or "=" not in line:
            continue
        key_and_type, value = line.split("=", 1)
        if ":" not in key_and_type:
            continue
        key, _ = key_and_type.split(":", 1)
        if key in requested_fields:
            cache_entries[key] = value.strip()

    missing_fields: list[str] = []
    for cache_name, diagnostic_name in BUILD_CONFIGURATION_FIELDS:
        value = cache_entries.get(cache_name, "")
        values[diagnostic_name] = value
        if not value:
            missing_fields.append(cache_name)

    if missing_fields:
        return values, [
            "configured CMake cache is missing non-empty backend identity entries "
            f"({', '.join(missing_fields)}): {cache_path}"
        ]
    return values, []


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
            summary.write(
                f"| {phase} | {metrics[f'{phase}_time_ms'] / 1000.0:.3f} s |\n"
            )
        summary.write(
            f"| measured total | {metrics['total_time_ms'] / 1000.0:.3f} s |\n\n"
        )
        summary.write(f"- status: `{result['status']}`\n")
        summary.write(f"- cache state: `{diagnostics['cache_state']}`\n")
        summary.write(f"- selected tests: `{diagnostics['selected_test_count']}`\n")
        summary.write(f"- Ninja command edges: `{diagnostics['ninja_edge_count']}`\n")
        summary.write(f"- ccache stats: `{diagnostics['ccache_stats_health']}`\n\n")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    for phase in PHASE_NAMES:
        parser.add_argument(
            f"--{phase}-json", type=Path, action="append", required=True
        )
    parser.add_argument("--gate", required=True)
    parser.add_argument("--preset", required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--sanitizer", default="none")
    parser.add_argument("--runner-image", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--cache-state", choices=("cold", "warm"), required=True)
    parser.add_argument("--selected-test-count", type=_nonnegative_int)
    parser.add_argument("--ninja-edge-count", type=_nonnegative_int)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--ccache-stats-json", type=Path)
    parser.add_argument("--require-ccache-stats", action="store_true")
    parser.add_argument("--vcpkg-cache-hit", type=_parse_bool)
    parser.add_argument("--job-url")
    parser.add_argument("--timestamp-utc", default=None)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    phase_paths: dict[str, list[Path]] = {
        "configure": args.configure_json,
        "build": args.build_json,
        "test": args.test_json,
    }
    phase_payloads: dict[str, list[dict[str, object]]] = {
        phase: [] for phase in PHASE_NAMES
    }
    phase_errors: list[str] = []
    for phase, paths in phase_paths.items():
        for path in paths:
            payload, error = _read_phase(path)
            if error:
                phase_errors.append(error)
            elif payload is not None:
                phase_payloads[phase].append(payload)

    phase_times_ms = {
        phase: int(
            round(
                sum(
                    float(payload["elapsed_seconds"])
                    for payload in phase_payloads[phase]
                )
                * 1000.0
            )
        )
        for phase in PHASE_NAMES
    }
    phase_returncodes = {
        phase: [payload["returncode"] for payload in phase_payloads[phase]]
        for phase in PHASE_NAMES
    }

    ccache_stats: dict[str, int] | None = None
    ccache_stats_errors: list[str] = []
    if args.ccache_stats_json is None:
        if args.require_ccache_stats:
            ccache_stats_errors.append(
                "ccache stats are required but --ccache-stats-json was not provided"
            )
    else:
        ccache_stats, ccache_stats_error = _read_ccache_stats(args.ccache_stats_json)
        if ccache_stats_error:
            ccache_stats_errors.append(ccache_stats_error)

    build_configuration = {
        diagnostic_name: "" for _, diagnostic_name in BUILD_CONFIGURATION_FIELDS
    }
    build_configuration_errors: list[str] = []
    if args.build_dir is not None:
        build_configuration, build_configuration_errors = _read_build_configuration(
            args.build_dir
        )
    build_configuration_available = (
        args.build_dir is not None and not build_configuration_errors
    )

    ccache_stats_available = ccache_stats is not None
    ccache_values = ccache_stats or {field: 0 for field in CCACHE_SUMMARY_FIELDS}
    if ccache_stats_errors:
        ccache_stats_health = "invalid"
    elif not ccache_stats_available:
        ccache_stats_health = "not_requested"
    elif ccache_values["error_count"] > 0:
        ccache_stats_health = "errors_reported"
    else:
        ccache_stats_health = "healthy"

    if phase_errors or ccache_stats_errors or build_configuration_errors:
        status = "error"
    elif (
        any(
            code != 0
            for returncodes in phase_returncodes.values()
            for code in returncodes
        )
        or ccache_values["error_count"] > 0
    ):
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
        "extrinsic_platform": build_configuration["extrinsic_platform"],
        "extrinsic_backend": build_configuration["extrinsic_backend"],
        "intrinsic_platform_backend": build_configuration["intrinsic_platform_backend"],
        "intrinsic_headless_no_glfw": build_configuration["intrinsic_headless_no_glfw"],
        "intrinsic_platform_backend_selected": build_configuration[
            "intrinsic_platform_backend_selected"
        ],
        "build_configuration_available": build_configuration_available,
        "build_configuration_errors": build_configuration_errors,
        "ccache_hit_count": ccache_values["hit_count"],
        "ccache_miss_count": ccache_values["miss_count"],
        "ccache_cache_size_kib": ccache_values["cache_size_kib"],
        "ccache_error_count": ccache_values["error_count"],
        "ccache_stats_available": ccache_stats_available,
        "ccache_stats_required": args.require_ccache_stats,
        "ccache_stats_health": ccache_stats_health,
        "ccache_stats_errors": ccache_stats_errors,
        "vcpkg_cache_hit": args.vcpkg_cache_hit or False,
        "vcpkg_cache_state_available": args.vcpkg_cache_hit is not None,
        "phase_order": list(PHASE_NAMES),
        "phase_report_counts": {
            phase: len(phase_payloads[phase]) for phase in PHASE_NAMES
        },
        "phase_labels": {
            phase: [payload.get("label", "") for payload in phase_payloads[phase]]
            for phase in PHASE_NAMES
        },
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
    args.output.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    _append_summary(result)

    for error in phase_errors:
        print(f"ERROR: {error}")
    for error in ccache_stats_errors:
        print(f"ERROR: {error}")
    for error in build_configuration_errors:
        print(f"ERROR: {error}")
    if ccache_stats_available and ccache_values["error_count"] > 0:
        print(f"FAILED: ccache reported {ccache_values['error_count']} error(s)")
    print(f"Wrote {args.output} ({status})")
    return 0 if status == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
