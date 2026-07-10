#!/usr/bin/env python3
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
TIME_COMMAND = REPO_ROOT / "tools" / "ci" / "time_command.py"
AGGREGATOR = REPO_ROOT / "tools" / "ci" / "aggregate_gate_timing.py"
MANIFEST_VALIDATOR = (
    REPO_ROOT / "tools" / "benchmark" / "validate_benchmark_manifests.py"
)
RESULT_VALIDATOR = REPO_ROOT / "tools" / "benchmark" / "validate_benchmark_results.py"
BASELINE_VALIDATOR = REPO_ROOT / "tools" / "ci" / "validate_gate_timing_baseline.py"
CI_MANIFEST_ROOT = REPO_ROOT / "benchmarks" / "ci"
CI_BASELINE = (
    REPO_ROOT
    / "benchmarks"
    / "baselines"
    / "ci_gate_latency_github_ubuntu_24_04_v1.json"
)


def _write_phase(path: Path, elapsed_seconds: float, returncode: int = 0) -> None:
    path.write_text(
        json.dumps(
            {
                "schema_version": 1,
                "label": path.stem,
                "command": ["fixture"],
                "elapsed_seconds": elapsed_seconds,
                "elapsed_ms": round(elapsed_seconds * 1000),
                "returncode": returncode,
            }
        )
        + "\n",
        encoding="utf-8",
    )


def _write_ccache_stats(path: Path, error_count: int = 0) -> None:
    path.write_text(
        json.dumps(
            {
                "schema_version": 1,
                "summary": {
                    "hit_count": 0,
                    "miss_count": 0,
                    "cache_size_kib": 128,
                    "error_count": error_count,
                },
                "raw": {},
            }
        )
        + "\n",
        encoding="utf-8",
    )


class CiTimingTests(unittest.TestCase):
    @staticmethod
    def _run_fixture_aggregator(
        tmp_path: Path,
        extra_args: list[str] | None = None,
    ) -> tuple[subprocess.CompletedProcess[str], dict[str, object]]:
        configure = tmp_path / "configure.json"
        build = tmp_path / "build.json"
        test = tmp_path / "test.json"
        output = tmp_path / "result.json"
        _write_phase(configure, 1.0)
        _write_phase(build, 2.0)
        _write_phase(test, 0.5)
        command = [
            sys.executable,
            str(AGGREGATOR),
            "--configure-json",
            str(configure),
            "--build-json",
            str(build),
            "--test-json",
            str(test),
            "--gate",
            "fixture",
            "--preset",
            "ci",
            "--compiler",
            "clang-20",
            "--runner-image",
            "ubuntu-24.04",
            "--commit",
            "0123456789abcdef",
            "--cache-state",
            "cold",
        ]
        command.extend(extra_args or [])
        command.extend(["--output", str(output)])
        completed = subprocess.run(
            command,
            cwd=REPO_ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        payload = json.loads(output.read_text(encoding="utf-8"))
        return completed, payload

    def test_manifest_passes_strict_validation(self) -> None:
        manifest = (
            CI_MANIFEST_ROOT
            / "manifests"
            / "ci_gate_latency_github_ubuntu_24_04_v1.yaml"
        )
        self.assertTrue(manifest.exists(), f"missing CI timing manifest: {manifest}")
        self.assertIn(
            "benchmark_id: ci.gate-latency.github-ubuntu-24.04.v1",
            manifest.read_text(encoding="utf-8"),
        )
        result = subprocess.run(
            [
                sys.executable,
                str(MANIFEST_VALIDATOR),
                "--root",
                str(CI_MANIFEST_ROOT),
                "--strict",
            ],
            cwd=REPO_ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertEqual(result.returncode, 0, msg=result.stdout + result.stderr)

    def test_time_command_writes_success_and_failure_reports(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            success_path = tmp_path / "success.json"
            success = subprocess.run(
                [
                    sys.executable,
                    str(TIME_COMMAND),
                    "--label",
                    "success",
                    "--json-out",
                    str(success_path),
                    "--",
                    sys.executable,
                    "-c",
                    "pass",
                ],
                cwd=REPO_ROOT,
                check=False,
            )
            self.assertEqual(success.returncode, 0)
            success_payload = json.loads(success_path.read_text(encoding="utf-8"))
            self.assertEqual(success_payload["schema_version"], 1)
            self.assertEqual(success_payload["returncode"], 0)
            self.assertEqual(success_payload["command_returncode"], 0)
            self.assertIsInstance(success_payload["elapsed_ms"], int)
            self.assertGreaterEqual(success_payload["elapsed_ms"], 0)
            self.assertLessEqual(
                abs(
                    success_payload["elapsed_ms"]
                    - success_payload["elapsed_seconds"] * 1000
                ),
                1,
            )
            self.assertTrue(success_payload["started_at_utc"].endswith("Z"))
            self.assertTrue(success_payload["finished_at_utc"].endswith("Z"))

            failure_path = tmp_path / "failure.json"
            failure = subprocess.run(
                [
                    sys.executable,
                    str(TIME_COMMAND),
                    "--label",
                    "failure",
                    "--json-out",
                    str(failure_path),
                    "--",
                    sys.executable,
                    "-c",
                    "raise SystemExit(7)",
                ],
                cwd=REPO_ROOT,
                check=False,
            )
            self.assertEqual(failure.returncode, 7)
            failure_payload = json.loads(failure_path.read_text(encoding="utf-8"))
            self.assertEqual(failure_payload["returncode"], 7)
            self.assertEqual(failure_payload["command_returncode"], 7)

    def test_aggregator_converts_units_and_emits_valid_result(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            configure = tmp_path / "configure.json"
            build = tmp_path / "build.json"
            test = tmp_path / "test.json"
            test_second = tmp_path / "test-second.json"
            ccache_stats = tmp_path / "ccache-stats.json"
            output = tmp_path / "result" / "result.json"
            _write_phase(configure, 1.25)
            _write_phase(build, 2.5)
            _write_phase(test, 0.125)
            _write_phase(test_second, 0.25)
            _write_ccache_stats(ccache_stats)

            result = subprocess.run(
                [
                    sys.executable,
                    str(AGGREGATOR),
                    "--configure-json",
                    str(configure),
                    "--build-json",
                    str(build),
                    "--test-json",
                    str(test),
                    "--test-json",
                    str(test_second),
                    "--gate",
                    "fixture",
                    "--preset",
                    "ci",
                    "--compiler",
                    "clang-20",
                    "--runner-image",
                    "ubuntu-24.04",
                    "--commit",
                    "0123456789abcdef",
                    "--cache-state",
                    "cold",
                    "--selected-test-count",
                    "42",
                    "--ninja-edge-count",
                    "123",
                    "--ccache-stats-json",
                    str(ccache_stats),
                    "--require-ccache-stats",
                    "--vcpkg-cache-hit",
                    "true",
                    "--timestamp-utc",
                    "2026-07-09T20:00:00Z",
                    "--output",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            self.assertEqual(result.returncode, 0, msg=result.stdout + result.stderr)
            payload = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(
                payload["benchmark_id"], "ci.gate-latency.github-ubuntu-24.04.v1"
            )
            self.assertEqual(payload["backend"], "external_baseline")
            self.assertEqual(payload["metrics"]["configure_time_ms"], 1250)
            self.assertEqual(payload["metrics"]["build_time_ms"], 2500)
            self.assertEqual(payload["metrics"]["test_time_ms"], 375)
            self.assertEqual(payload["metrics"]["total_time_ms"], 4125)
            self.assertEqual(payload["diagnostics"]["selected_test_count"], 42)
            self.assertEqual(payload["diagnostics"]["ninja_edge_count"], 123)
            self.assertEqual(payload["diagnostics"]["ccache_cache_size_kib"], 128)
            self.assertEqual(payload["diagnostics"]["ccache_error_count"], 0)
            self.assertTrue(payload["diagnostics"]["ccache_stats_available"])
            self.assertTrue(payload["diagnostics"]["ccache_stats_required"])
            self.assertEqual(payload["diagnostics"]["ccache_stats_health"], "healthy")
            self.assertEqual(payload["diagnostics"]["ccache_stats_errors"], [])
            self.assertEqual(payload["diagnostics"]["phase_report_counts"]["test"], 2)
            self.assertEqual(payload["status"], "passed")

            validation = subprocess.run(
                [
                    sys.executable,
                    str(RESULT_VALIDATOR),
                    "--root",
                    str(output.parent),
                    "--strict",
                ],
                cwd=REPO_ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            self.assertEqual(
                validation.returncode, 0, msg=validation.stdout + validation.stderr
            )

    def test_aggregator_fails_closed_and_preserves_diagnostics(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            configure = tmp_path / "configure.json"
            build = tmp_path / "build.json"
            missing_test = tmp_path / "missing-test.json"
            output = tmp_path / "result.json"
            _write_phase(configure, 1.0)
            _write_phase(build, 2.0)

            result = subprocess.run(
                [
                    sys.executable,
                    str(AGGREGATOR),
                    "--configure-json",
                    str(configure),
                    "--build-json",
                    str(build),
                    "--test-json",
                    str(missing_test),
                    "--gate",
                    "fixture",
                    "--preset",
                    "ci",
                    "--compiler",
                    "clang-20",
                    "--runner-image",
                    "ubuntu-24.04",
                    "--commit",
                    "0123456789abcdef",
                    "--cache-state",
                    "cold",
                    "--output",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            self.assertEqual(result.returncode, 1)
            payload = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(payload["status"], "error")
            self.assertEqual(payload["metrics"]["test_time_ms"], 0)
            self.assertEqual(len(payload["diagnostics"]["phase_errors"]), 1)
            self.assertFalse(payload["diagnostics"]["selected_test_count_available"])

    def test_aggregator_reports_optional_absent_ccache_stats(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            result, payload = self._run_fixture_aggregator(Path(tmp))

        self.assertEqual(result.returncode, 0, msg=result.stdout + result.stderr)
        self.assertEqual(payload["status"], "passed")
        self.assertFalse(payload["diagnostics"]["ccache_stats_available"])
        self.assertFalse(payload["diagnostics"]["ccache_stats_required"])
        self.assertEqual(payload["diagnostics"]["ccache_stats_health"], "not_requested")
        self.assertEqual(payload["diagnostics"]["ccache_stats_errors"], [])

    def test_aggregator_marks_missing_required_ccache_stats_error(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            missing = tmp_path / "missing-ccache-stats.json"
            result, payload = self._run_fixture_aggregator(
                tmp_path,
                [
                    "--ccache-stats-json",
                    str(missing),
                    "--require-ccache-stats",
                ],
            )

        self.assertEqual(result.returncode, 1)
        self.assertEqual(payload["status"], "error")
        self.assertFalse(payload["diagnostics"]["ccache_stats_available"])
        self.assertTrue(payload["diagnostics"]["ccache_stats_required"])
        self.assertEqual(payload["diagnostics"]["ccache_stats_health"], "invalid")
        self.assertIn(
            "missing ccache stats JSON",
            payload["diagnostics"]["ccache_stats_errors"][0],
        )

    def test_aggregator_marks_required_ccache_stats_without_path_error(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            result, payload = self._run_fixture_aggregator(
                Path(tmp),
                ["--require-ccache-stats"],
            )

        self.assertEqual(result.returncode, 1)
        self.assertEqual(payload["status"], "error")
        self.assertFalse(payload["diagnostics"]["ccache_stats_available"])
        self.assertTrue(payload["diagnostics"]["ccache_stats_required"])
        self.assertIn(
            "--ccache-stats-json was not provided",
            payload["diagnostics"]["ccache_stats_errors"][0],
        )

    def test_aggregator_marks_malformed_required_ccache_stats_error(self) -> None:
        malformed_payloads = (
            {"schema_version": 2, "summary": {}, "raw": {}},
            {
                "schema_version": 1,
                "summary": {
                    "hit_count": True,
                    "miss_count": 0,
                    "cache_size_kib": 0,
                    "error_count": 0,
                },
                "raw": {},
            },
        )
        for malformed in malformed_payloads:
            with self.subTest(malformed=malformed):
                with tempfile.TemporaryDirectory() as tmp:
                    tmp_path = Path(tmp)
                    stats = tmp_path / "ccache-stats.json"
                    stats.write_text(json.dumps(malformed), encoding="utf-8")
                    result, payload = self._run_fixture_aggregator(
                        tmp_path,
                        [
                            "--ccache-stats-json",
                            str(stats),
                            "--require-ccache-stats",
                        ],
                    )

                self.assertEqual(result.returncode, 1)
                self.assertEqual(payload["status"], "error")
                self.assertFalse(payload["diagnostics"]["ccache_stats_available"])
                self.assertEqual(
                    payload["diagnostics"]["ccache_stats_health"], "invalid"
                )
                self.assertTrue(payload["diagnostics"]["ccache_stats_errors"])

    def test_aggregator_marks_reported_ccache_errors_failed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            stats = tmp_path / "ccache-stats.json"
            _write_ccache_stats(stats, error_count=3)
            result, payload = self._run_fixture_aggregator(
                tmp_path,
                [
                    "--ccache-stats-json",
                    str(stats),
                    "--require-ccache-stats",
                ],
            )

        self.assertEqual(result.returncode, 1)
        self.assertEqual(payload["status"], "failed")
        self.assertTrue(payload["diagnostics"]["ccache_stats_available"])
        self.assertEqual(payload["diagnostics"]["ccache_error_count"], 3)
        self.assertEqual(
            payload["diagnostics"]["ccache_stats_health"], "errors_reported"
        )
        self.assertEqual(payload["diagnostics"]["ccache_stats_errors"], [])

    def test_historical_baseline_statistics_validate(self) -> None:
        payload = json.loads(CI_BASELINE.read_text(encoding="utf-8"))
        self.assertEqual(
            payload["benchmark_id"],
            "ci.gate-latency.github-ubuntu-24.04.v1.aggregate-baseline",
        )
        self.assertEqual(
            payload["diagnostics"]["source_benchmark_id"],
            "ci.gate-latency.github-ubuntu-24.04.v1",
        )
        result = subprocess.run(
            [sys.executable, str(BASELINE_VALIDATOR)],
            cwd=REPO_ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertEqual(result.returncode, 0, msg=result.stdout + result.stderr)
        self.assertIn("validation passed", result.stdout)

        canonical = subprocess.run(
            [
                sys.executable,
                str(RESULT_VALIDATOR),
                "--root",
                str(CI_BASELINE.parent),
                "--strict",
            ],
            cwd=REPO_ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertEqual(
            canonical.returncode,
            0,
            msg=canonical.stdout + canonical.stderr,
        )

    def test_historical_baseline_rejects_context_drift(self) -> None:
        payload = json.loads(CI_BASELINE.read_text(encoding="utf-8"))
        payload["diagnostics"]["populations"][2]["sanitizer"] = "ubsan"
        with tempfile.TemporaryDirectory() as tmp:
            baseline = Path(tmp) / "baseline.json"
            baseline.write_text(json.dumps(payload), encoding="utf-8")
            result = subprocess.run(
                [
                    sys.executable,
                    str(BASELINE_VALIDATOR),
                    "--baseline",
                    str(baseline),
                ],
                cwd=REPO_ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
        self.assertEqual(result.returncode, 1)
        self.assertIn("sanitizer must be 'asan'", result.stdout)

    def test_historical_baseline_rejects_malformed_audit_metadata(self) -> None:
        payload = json.loads(CI_BASELINE.read_text(encoding="utf-8"))
        payload["diagnostics"]["source_verification"] = []
        with tempfile.TemporaryDirectory() as tmp:
            baseline = Path(tmp) / "baseline.json"
            baseline.write_text(json.dumps(payload), encoding="utf-8")
            result = subprocess.run(
                [
                    sys.executable,
                    str(BASELINE_VALIDATOR),
                    "--baseline",
                    str(baseline),
                ],
                cwd=REPO_ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
        self.assertEqual(result.returncode, 1)
        self.assertIn(
            "diagnostics.source_verification must be an object",
            result.stdout,
        )
        self.assertNotIn("Traceback", result.stderr)


if __name__ == "__main__":
    unittest.main()
