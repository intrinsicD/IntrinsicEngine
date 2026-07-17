#!/usr/bin/env python3
from __future__ import annotations

import json
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parents[3]
WORKFLOW_ROOT = REPO_ROOT / ".github" / "workflows"
BENCHMARK_CMAKE = REPO_ROOT / "benchmarks" / "CMakeLists.txt"
BENCHMARK_WORKFLOW = WORKFLOW_ROOT / "ci-bench-smoke.yml"
DOCS_WORKFLOW = WORKFLOW_ROOT / "ci-docs.yml"
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
WARM_CONFIGURE_BUDGET_SECONDS = 40.0
WARM_CONFIGURE_CALL_COUNTS = {
    "pr-fast.yml": 1,
    "ci-linux-clang.yml": 1,
    "ci-sanitizers.yml": 1,
    "ci-vulkan.yml": 1,
    "ci-bench-smoke.yml": 1,
    "ci-source-coverage.yml": 1,
    "nightly-deep.yml": 2,
}
TIMING_WORKFLOW_BUILD_DIRS = {
    "pr-fast.yml": "build/ci-fast",
    "ci-linux-clang.yml": "build/ci",
    "ci-vulkan.yml": "build/ci-vulkan",
    "ci-bench-smoke.yml": "build/ci",
    "ci-sanitizers.yml": "build/ci",
    "ci-source-coverage.yml": "build/ci-coverage-cpu",
}


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


def _write_build_configuration(
    build_dir: Path,
    *,
    platform: str = "Linux",
    backend: str = "Vulkan",
    intrinsic_platform_backend: str = "Auto",
    headless_no_glfw: str = "OFF",
    platform_backend_selected: str = "Glfw",
) -> None:
    build_dir.mkdir(parents=True, exist_ok=True)
    (build_dir / "CMakeCache.txt").write_text(
        "\n".join(
            (
                f"EXTRINSIC_PLATFORM:STRING={platform}",
                f"EXTRINSIC_BACKEND:STRING={backend}",
                f"INTRINSIC_PLATFORM_BACKEND:STRING={intrinsic_platform_backend}",
                f"INTRINSIC_HEADLESS_NO_GLFW:BOOL={headless_no_glfw}",
                "INTRINSIC_PLATFORM_BACKEND_SELECTED:INTERNAL="
                f"{platform_backend_selected}",
            )
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

            over_budget_path = tmp_path / "over-budget.json"
            over_budget = subprocess.run(
                [
                    sys.executable,
                    str(TIME_COMMAND),
                    "--label",
                    "over-budget",
                    "--warm-cache-hit",
                    "true",
                    "--max-warm-seconds",
                    "0",
                    "--json-out",
                    str(over_budget_path),
                    "--",
                    sys.executable,
                    "-c",
                    "import time; time.sleep(0.01)",
                ],
                cwd=REPO_ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            self.assertEqual(over_budget.returncode, 1)
            self.assertIn("exact vcpkg cache hit", over_budget.stderr)
            over_budget_payload = json.loads(
                over_budget_path.read_text(encoding="utf-8")
            )
            self.assertTrue(over_budget_payload["warm_cache_hit"])
            self.assertEqual(over_budget_payload["command_returncode"], 0)
            self.assertEqual(over_budget_payload["returncode"], 1)

    def test_workflow_warm_configure_budgets_match_hosted_evidence(self) -> None:
        observed: dict[str, list[float]] = {}
        for path in sorted(WORKFLOW_ROOT.glob("*.yml")):
            budgets = [
                float(value)
                for value in re.findall(
                    r"--max-warm-seconds\s+([0-9]+(?:\.[0-9]+)?)",
                    path.read_text(encoding="utf-8"),
                )
            ]
            if budgets:
                observed[path.name] = budgets

        expected = {
            name: [WARM_CONFIGURE_BUDGET_SECONDS] * count
            for name, count in WARM_CONFIGURE_CALL_COUNTS.items()
        }
        self.assertEqual(observed, expected)

    def test_live_timing_workflows_supply_configured_build_directory(self) -> None:
        for workflow_name, build_dir in TIMING_WORKFLOW_BUILD_DIRS.items():
            with self.subTest(workflow=workflow_name):
                workflow = yaml.safe_load(
                    (WORKFLOW_ROOT / workflow_name).read_text(encoding="utf-8")
                )
                aggregator_steps = [
                    step
                    for job in workflow["jobs"].values()
                    for step in job["steps"]
                    if "aggregate_gate_timing.py" in step.get("run", "")
                ]
                self.assertEqual(len(aggregator_steps), 1)
                self.assertIn(
                    f"--build-dir {build_dir}",
                    aggregator_steps[0]["run"],
                )

        ci_linux = (WORKFLOW_ROOT / "ci-linux-clang.yml").read_text(encoding="utf-8")
        self.assertIn(
            '--selected-test-count "$((selected_tests + 2))"',
            ci_linux,
        )
        self.assertIn(
            "--configure-json "
            "build/ci/ci-timing/phases/configure_backend_determinism.json",
            ci_linux,
        )

    def test_benchmark_smoke_registration_matches_dedicated_lane(self) -> None:
        cmake = "\n".join(
            line.split("#", 1)[0]
            for line in BENCHMARK_CMAKE.read_text(encoding="utf-8").splitlines()
        )

        registration = re.search(
            r"intrinsic_register_test_executable\(\s*"
            r"TARGET\s+IntrinsicBenchmarkSmoke\s+"
            r"LABELS\s+(?P<labels>[^\n\)]+)\s*\)",
            cmake,
        )
        self.assertIsNotNone(registration, "missing benchmark smoke registration")
        self.assertEqual(
            set(registration.group("labels").split()),
            {"benchmark", "geometry", "graphics", "physics", "slow"},
        )

        def properties(test_name: str) -> str:
            match = re.search(
                rf"set_tests_properties\(\s*{re.escape(test_name)}\s+"
                r"PROPERTIES(?P<body>.*?)\n\s*\)",
                cmake,
                flags=re.DOTALL,
            )
            self.assertIsNotNone(match, f"missing properties for {test_name}")
            return match.group("body")

        def property_value(block: str, name: str) -> str:
            match = re.search(
                rf"\b{re.escape(name)}\s+(?:\"(?P<quoted>[^\"]+)\"|(?P<bare>\S+))",
                block,
            )
            self.assertIsNotNone(match, f"missing {name} property")
            return match.group("quoted") or match.group("bare")

        run = properties("IntrinsicBenchmarkSmoke.Run")
        validate = properties("IntrinsicBenchmarkSmoke.Validate")
        expected_labels = {"benchmark", "geometry", "graphics", "physics", "slow"}
        self.assertEqual(set(property_value(run, "LABELS").split(";")), expected_labels)
        self.assertEqual(
            set(property_value(validate, "LABELS").split(";")), expected_labels
        )
        self.assertEqual(property_value(run, "TIMEOUT"), "120")
        self.assertEqual(property_value(validate, "TIMEOUT"), "30")
        self.assertEqual(
            property_value(run, "FIXTURES_SETUP"), "IntrinsicBenchmarkSmoke"
        )
        self.assertEqual(
            property_value(validate, "FIXTURES_REQUIRED"),
            "IntrinsicBenchmarkSmoke",
        )

        workflow = yaml.safe_load(BENCHMARK_WORKFLOW.read_text(encoding="utf-8"))
        triggers = workflow.get("on", workflow.get(True, {}))
        self.assertIn("pull_request", triggers)
        job = workflow["jobs"]["benchmark-smoke"]
        self.assertEqual(job["timeout-minutes"], 15)
        steps = job["steps"]
        named_steps = {step.get("name"): step for step in steps}
        runner_step = named_steps["Run benchmark smoke runner"]
        validator_step = named_steps["Validate benchmark result JSON"]
        artifact_step = named_steps["Upload benchmark smoke artifact"]
        self.assertEqual(runner_step["timeout-minutes"], 2)
        self.assertFalse(runner_step.get("continue-on-error", False))
        self.assertIn("--target IntrinsicBenchmarks", runner_step["run"])
        self.assertFalse(validator_step.get("continue-on-error", False))
        self.assertIn(
            "validate_benchmark_results.py --root build/ci/benchmark --strict",
            validator_step["run"],
        )
        self.assertLess(steps.index(runner_step), steps.index(validator_step))
        self.assertLess(steps.index(validator_step), steps.index(artifact_step))
        self.assertEqual(artifact_step["uses"], "actions/upload-artifact@v4")
        self.assertEqual(
            artifact_step["with"]["path"],
            "build/ci/benchmark/IntrinsicBenchmarkSmoke/",
        )
        self.assertEqual(artifact_step["with"]["if-no-files-found"], "error")

        docs_workflow = yaml.safe_load(DOCS_WORKFLOW.read_text(encoding="utf-8"))
        docs_steps = docs_workflow["jobs"]["docs-validation"]["steps"]
        validator_regression_steps = [
            step
            for step in docs_steps
            if "Test_BenchmarkResultValidator.py" in step.get("run", "")
        ]
        self.assertEqual(len(validator_regression_steps), 1)
        self.assertFalse(validator_regression_steps[0].get("continue-on-error", False))

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

    def test_aggregator_reads_configured_backend_identity(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            build_dir = tmp_path / "build"
            _write_build_configuration(
                build_dir,
                platform="Linux",
                backend="Vulkan",
                intrinsic_platform_backend="Auto",
                headless_no_glfw="OFF",
                platform_backend_selected="Glfw",
            )
            result, payload = self._run_fixture_aggregator(
                tmp_path,
                ["--build-dir", str(build_dir)],
            )

        self.assertEqual(result.returncode, 0, msg=result.stdout + result.stderr)
        self.assertEqual(payload["status"], "passed")
        diagnostics = payload["diagnostics"]
        self.assertEqual(diagnostics["extrinsic_platform"], "Linux")
        self.assertEqual(diagnostics["extrinsic_backend"], "Vulkan")
        self.assertEqual(diagnostics["intrinsic_platform_backend"], "Auto")
        self.assertEqual(diagnostics["intrinsic_headless_no_glfw"], "OFF")
        self.assertEqual(
            diagnostics["intrinsic_platform_backend_selected"],
            "Glfw",
        )
        self.assertTrue(diagnostics["build_configuration_available"])
        self.assertEqual(diagnostics["build_configuration_errors"], [])

    def test_aggregator_preserves_legacy_behavior_without_build_directory(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            result, payload = self._run_fixture_aggregator(Path(tmp))

        self.assertEqual(result.returncode, 0, msg=result.stdout + result.stderr)
        self.assertEqual(payload["status"], "passed")
        diagnostics = payload["diagnostics"]
        self.assertEqual(diagnostics["extrinsic_platform"], "")
        self.assertEqual(diagnostics["extrinsic_backend"], "")
        self.assertEqual(diagnostics["intrinsic_platform_backend"], "")
        self.assertEqual(diagnostics["intrinsic_headless_no_glfw"], "")
        self.assertEqual(diagnostics["intrinsic_platform_backend_selected"], "")
        self.assertFalse(diagnostics["build_configuration_available"])
        self.assertEqual(diagnostics["build_configuration_errors"], [])

    def test_aggregator_fails_closed_when_configured_cache_is_missing(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            build_dir = tmp_path / "missing-build"
            result, payload = self._run_fixture_aggregator(
                tmp_path,
                ["--build-dir", str(build_dir)],
            )

        self.assertEqual(result.returncode, 1)
        self.assertEqual(payload["status"], "error")
        diagnostics = payload["diagnostics"]
        self.assertFalse(diagnostics["build_configuration_available"])
        self.assertEqual(diagnostics["extrinsic_platform"], "")
        self.assertEqual(diagnostics["extrinsic_backend"], "")
        self.assertEqual(diagnostics["intrinsic_platform_backend"], "")
        self.assertEqual(diagnostics["intrinsic_headless_no_glfw"], "")
        self.assertEqual(diagnostics["intrinsic_platform_backend_selected"], "")
        self.assertIn(
            "missing configured CMake cache",
            diagnostics["build_configuration_errors"][0],
        )

    def test_aggregator_fails_closed_when_backend_identity_is_incomplete(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            build_dir = tmp_path / "build"
            build_dir.mkdir()
            (build_dir / "CMakeCache.txt").write_text(
                "\n".join(
                    (
                        "EXTRINSIC_PLATFORM:STRING=Linux",
                        "EXTRINSIC_BACKEND:STRING=Null",
                        "INTRINSIC_PLATFORM_BACKEND:STRING=Null",
                        "INTRINSIC_HEADLESS_NO_GLFW:BOOL=ON",
                    )
                )
                + "\n",
                encoding="utf-8",
            )
            result, payload = self._run_fixture_aggregator(
                tmp_path,
                ["--build-dir", str(build_dir)],
            )

        self.assertEqual(result.returncode, 1)
        self.assertEqual(payload["status"], "error")
        diagnostics = payload["diagnostics"]
        self.assertFalse(diagnostics["build_configuration_available"])
        self.assertEqual(diagnostics["extrinsic_platform"], "Linux")
        self.assertEqual(diagnostics["extrinsic_backend"], "Null")
        self.assertEqual(diagnostics["intrinsic_platform_backend"], "Null")
        self.assertEqual(diagnostics["intrinsic_headless_no_glfw"], "ON")
        self.assertEqual(diagnostics["intrinsic_platform_backend_selected"], "")
        self.assertIn(
            "INTRINSIC_PLATFORM_BACKEND_SELECTED",
            diagnostics["build_configuration_errors"][0],
        )

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
