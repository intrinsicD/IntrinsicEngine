#!/usr/bin/env python3
from __future__ import annotations

import json
import unittest
from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parents[3]
WORKFLOW_ROOT = REPO_ROOT / ".github" / "workflows"
PRESETS = REPO_ROOT / "CMakePresets.json"
EXPECTED_GROUP = (
    "${{ github.workflow }}-${{ github.event.pull_request.number || github.ref }}"
)
SANITIZER_GROUP = (
    "${{ github.workflow }}-sanitizers-"
    "${{ github.event.pull_request.number || github.ref }}"
)
WORKFLOWS = {
    "pr-fast.yml": ("ci-gate-timing-pr-fast", 1, True),
    "ci-linux-clang.yml": (
        "ci-gate-timing-ci-linux-clang",
        1,
        "${{ github.event_name == 'pull_request' }}",
    ),
    "ci-sanitizers.yml": (
        "ci-gate-timing-ci-sanitizers-${{ matrix.sanitizer.name }}",
        1,
        True,
    ),
    "ci-vulkan.yml": ("ci-gate-timing-ci-vulkan", 2, True),
    "ci-bench-smoke.yml": ("ci-gate-timing-ci-bench-smoke", 1, True),
    "ci-source-coverage.yml": (
        "ci-gate-timing-ci-source-coverage",
        1,
        True,
    ),
}


def _load_workflow(name: str) -> tuple[dict[str, object], str]:
    path = WORKFLOW_ROOT / name
    text = path.read_text(encoding="utf-8")
    payload = yaml.safe_load(text)
    if not isinstance(payload, dict):
        raise AssertionError(f"workflow root is not a mapping: {path}")
    return payload, text


class WorkflowConcurrencyTests(unittest.TestCase):
    def test_ci_fast_preset_is_unsanitized_and_headless(self) -> None:
        payload = json.loads(PRESETS.read_text(encoding="utf-8"))
        configure_presets = {
            preset["name"]: preset for preset in payload["configurePresets"]
        }
        build_presets = {
            preset["name"]: preset for preset in payload["buildPresets"]
        }
        ci_fast = configure_presets["ci-fast"]
        self.assertEqual(ci_fast["inherits"], "ci")
        self.assertEqual(
            ci_fast["cacheVariables"],
            {
                "BUILD_TESTING": "ON",
                "EXTRINSIC_PLATFORM": "Linux",
                "EXTRINSIC_BACKEND": "Null",
                "INTRINSIC_PLATFORM_BACKEND": "Null",
                "INTRINSIC_HEADLESS_NO_GLFW": "ON",
                "INTRINSIC_BUILD_SANDBOX": "OFF",
                "INTRINSIC_BUILD_TESTS": "ON",
                "INTRINSIC_BUILD_BENCHMARKS": "OFF",
                "INTRINSIC_ENABLE_CUDA": "OFF",
                "INTRINSIC_ENABLE_SANITIZERS": "OFF",
                "INTRINSIC_SANITIZER_MODE": "none",
                "INTRINSIC_ENABLE_SOURCE_COVERAGE": "OFF",
                "INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN": "OFF",
                "VCPKG_MANIFEST_NO_DEFAULT_FEATURES": "ON",
            },
        )
        self.assertEqual(
            build_presets["ci-fast"]["configurePreset"],
            "ci-fast",
        )

    def test_source_coverage_workflow_is_manual_and_uses_canonical_cohort(
        self,
    ) -> None:
        payload, coverage = _load_workflow("ci-source-coverage.yml")
        triggers = payload.get("on", payload.get(True, {}))
        self.assertEqual(set(triggers), {"workflow_dispatch"})
        self.assertEqual(
            coverage.count("-- cmake --preset ci-coverage-cpu --fresh"),
            1,
        )
        self.assertIn(
            "cmake --build --preset ci-coverage-cpu --target IntrinsicCpuTests",
            coverage,
        )
        self.assertIn(
            "run_source_coverage.py \\\n"
            "            --build-dir build/ci-coverage-cpu \\\n"
            "            --output build/ci-coverage-cpu/coverage \\\n"
            "            --preset ci-coverage-cpu \\\n"
            "            --diff-base HEAD^ \\\n"
            "            --jobs 2",
            coverage,
        )
        self.assertIn(
            "compare_source_coverage.py \\\n"
            "            --baseline build/ci-coverage-cpu/coverage/coverage.json \\\n"
            "            --candidate build/ci-coverage-cpu/coverage/coverage.json \\\n"
            "            --test-only-refactor",
            coverage,
        )

    def test_pr_fast_classifies_and_runs_structural_checks_before_cpp_setup(
        self,
    ) -> None:
        payload, pr_fast = _load_workflow("pr-fast.yml")
        steps = payload["jobs"]["pr-fast"]["steps"]
        named_steps = {step.get("name"): step for step in steps}
        checkout = named_steps["Checkout"]
        plan = named_steps["Plan touched scope"]
        python_dependency = named_steps["Ensure Python validation dependency"]
        structural = named_steps["Run selected structural checks"]
        install = named_steps["Install system dependencies"]

        self.assertEqual(checkout["with"]["fetch-depth"], 0)
        self.assertFalse(checkout["with"]["persist-credentials"])
        self.assertLess(steps.index(plan), steps.index(python_dependency))
        self.assertLess(steps.index(python_dependency), steps.index(structural))
        self.assertLess(steps.index(structural), steps.index(install))
        self.assertIn("base_ref=origin/main", plan["run"])
        self.assertIn("head_ref=HEAD", plan["run"])
        self.assertIn("${{ github.event.pull_request.base.sha }}", plan["run"])
        self.assertIn("${{ github.event.pull_request.head.sha }}", plan["run"])
        self.assertIn("--action plan", plan["run"])
        self.assertIn("--action structural", structural["run"])
        self.assertIn(
            "--output build/ci-fast/ci-routing/route.json",
            plan["run"],
        )
        self.assertIn("if ! python3 -c 'import yaml'", python_dependency["run"])
        self.assertEqual(
            pr_fast.count("--github-output \"$GITHUB_OUTPUT\""),
            5,
        )
        self.assertEqual(
            pr_fast.count("--step-summary \"$GITHUB_STEP_SUMMARY\""),
            5,
        )

    def test_specialized_workflows_build_only_selected_aggregates(self) -> None:
        _, pr_fast = _load_workflow("pr-fast.yml")
        self.assertIn(
            "--action finalize",
            pr_fast,
        )
        self.assertIn(
            "--action build",
            pr_fast,
        )
        self.assertIn("--action test", pr_fast)
        self.assertIn("--build-dir build/ci-fast", pr_fast)
        self.assertIn("-- cmake --preset ci-fast --fresh", pr_fast)
        self.assertNotIn(
            "cmake --build --preset ci --target IntrinsicTests",
            pr_fast,
        )
        self.assertNotIn("IntrinsicPrFastTests", pr_fast)

        _, linux = _load_workflow("ci-linux-clang.yml")
        self.assertIn(
            "--inventory build/ci/test-inventories/IntrinsicCpuTests.txt",
            linux,
        )
        cpu_inventory_position = linux.index(
            "--inventory build/ci/test-inventories/IntrinsicCpuTests.txt"
        )
        self.assertNotIn(
            "--skip-undeclared",
            linux[max(0, cpu_inventory_position - 120) : cpu_inventory_position],
        )
        self.assertIn(
            "Test.TestGateRouting.py \\\n"
            "            --build-dir build/ci \\\n"
            "            --aggregate IntrinsicCpuTests",
            linux,
        )

        _, vulkan = _load_workflow("ci-vulkan.yml")
        self.assertIn(
            "--target ExtrinsicSandbox IntrinsicGpuVulkanTests "
            "IntrinsicRuntimeGpuReadbackSmokeTests "
            "IntrinsicGlfwLifecycleLsanProcess",
            vulkan,
        )
        self.assertIn(
            "--inventory build/ci-vulkan/test-inventories/IntrinsicGpuVulkanTests.txt",
            vulkan,
        )
        self.assertNotIn(
            "--target ExtrinsicSandbox IntrinsicTests",
            vulkan,
        )
        self.assertIn(
            "Test.TestGateRouting.py \\\n"
            "            --build-dir build/ci-vulkan \\\n"
            "            --aggregate IntrinsicGpuVulkanTests",
            vulkan,
        )

    def test_manual_test_timing_profile_is_isolated_and_five_sample(self) -> None:
        payload, _ = _load_workflow("ci-linux-clang.yml")
        triggers = payload.get("on", payload.get(True, {}))
        timing_input = triggers["workflow_dispatch"]["inputs"][
            "collect_test_timing"
        ]
        self.assertEqual(
            timing_input,
            {
                "description": (
                    "Collect five pr-fast and full-CPU timing samples instead "
                    "of gates"
                ),
                "required": False,
                "default": False,
                "type": "boolean",
            },
        )

        jobs = payload["jobs"]
        full_job = jobs["ci-linux-clang"]
        self.assertEqual(
            " ".join(full_job["if"].split()),
            (
                "github.event_name != 'workflow_dispatch' || "
                "!inputs.collect_test_timing"
            ),
        )

        profile = jobs["test-timing-profile"]
        self.assertEqual(
            " ".join(profile["if"].split()),
            (
                "github.event_name == 'workflow_dispatch' && "
                "inputs.collect_test_timing"
            ),
        )
        self.assertFalse(profile["strategy"]["fail-fast"])
        self.assertEqual(
            profile["strategy"]["matrix"]["cohort"],
            [
                {
                    "name": "pr-fast",
                    "preset": "ci-fast",
                    "build_dir": "build/ci-fast",
                    "aggregate": "IntrinsicPrFastTests",
                },
                {
                    "name": "cpu",
                    "preset": "ci",
                    "build_dir": "build/ci",
                    "aggregate": "IntrinsicCpuTests",
                },
            ],
        )
        steps = {step["name"]: step for step in profile["steps"]}
        self.assertIn(
            "cmake --preset ${{ matrix.cohort.preset }} --fresh",
            steps["Configure timing cohort"]["run"],
        )
        self.assertIn(
            "--target ${{ matrix.cohort.aggregate }}",
            steps["Build timing cohort"]["run"],
        )
        reconcile = steps["Reconcile timing cohort"]["run"]
        self.assertIn(
            "--build-dir ${{ matrix.cohort.build_dir }}",
            reconcile,
        )
        self.assertIn(
            "--aggregate ${{ matrix.cohort.aggregate }}",
            reconcile,
        )
        collect = steps["Collect five timing samples"]["run"]
        self.assertIn("tools/ci/collect_test_timing.py", collect)
        self.assertIn("--cohort ${{ matrix.cohort.name }}", collect)
        self.assertIn("--samples 5", collect)
        self.assertIn("--parallel $(nproc)", collect)
        self.assertIn(
            "--output ${{ matrix.cohort.build_dir }}/test-timing",
            collect,
        )
        upload = steps["Upload timing profile"]
        self.assertEqual(upload["if"], "always()")
        self.assertEqual(
            upload["with"]["name"],
            "ci-test-timing-${{ matrix.cohort.name }}",
        )
        self.assertEqual(
            upload["with"]["path"],
            "${{ matrix.cohort.build_dir }}/test-timing/",
        )
        self.assertEqual(upload["with"]["if-no-files-found"], "error")

        for job_name in ("sanitizer-tests", "cpu-test-selection-parity"):
            with self.subTest(job=job_name):
                condition = " ".join(jobs[job_name]["if"].split())
                self.assertIn("inputs.run_sanitizers", condition)
                self.assertIn("!inputs.collect_test_timing", condition)

    def test_nightly_partitions_fast_slow_slo_and_benchmark_owners(self) -> None:
        payload, _ = _load_workflow("nightly-deep.yml")
        steps = payload["jobs"]["nightly-cpu-deep"]["steps"]
        named_steps = {step["name"]: step for step in steps}
        build_slow = named_steps["Build scheduled CPU slow cohort"]["run"]
        self.assertIn(
            "--target IntrinsicCpuSlowTests",
            build_slow,
        )
        reconcile_slow = named_steps[
            "Reconcile scheduled CPU slow cohort"
        ]["run"]
        self.assertIn("--aggregate IntrinsicCpuSlowTests", reconcile_slow)

        fast = named_steps["Run full CPU test suite"]["run"]
        slow = named_steps[
            "Run scheduled CPU slow correctness cohort"
        ]["run"]
        slo_step = named_steps["Run SLO/performance diagnostic (CI-009)"]
        slo = slo_step["run"]
        benchmark_step = named_steps[
            "Run benchmark smoke and selected deep benchmarks"
        ]
        self.assertIn(
            '-LE "gpu|vulkan|slow|flaky-quarantine"',
            fast,
        )
        self.assertIn('-L "^slow$"', slow)
        self.assertIn(
            '-LE "^(benchmark|gpu|slo|vulkan|flaky-quarantine)$"',
            slow,
        )
        self.assertIn(
            "--inventory "
            "build/ci/test-inventories/IntrinsicCpuSlowTests.txt",
            slow,
        )
        self.assertIn(
            "--output-junit build/ci/reports/cpu-slow.junit.xml",
            slow,
        )
        self.assertFalse(
            named_steps["Run scheduled CPU slow correctness cohort"].get(
                "continue-on-error",
                False,
            )
        )
        self.assertIn('-L "^slo$"', slo)
        self.assertTrue(slo_step["continue-on-error"])
        self.assertFalse(benchmark_step.get("continue-on-error", False))
        self.assertLess(
            steps.index(named_steps["Run full CPU test suite"]),
            steps.index(
                named_steps["Run scheduled CPU slow correctness cohort"]
            ),
        )
        self.assertLess(
            steps.index(
                named_steps["Run scheduled CPU slow correctness cohort"]
            ),
            steps.index(slo_step),
        )
        upload_paths = named_steps["Upload nightly reports"]["with"]["path"]
        self.assertIn(
            "build/ci/reports/cpu-slow.junit.xml",
            upload_paths,
        )
        self.assertIn(
            "build/ci/reports/architecture-slo.junit.xml",
            upload_paths,
        )

    def test_vulkan_workflow_retains_non_skipped_readback_evidence(self) -> None:
        payload, vulkan = _load_workflow("ci-vulkan.yml")
        jobs = payload.get("jobs")
        self.assertIsInstance(jobs, dict)
        job = jobs.get("ci-vulkan")
        self.assertIsInstance(job, dict)
        env = job.get("env")
        self.assertIsInstance(env, dict)
        self.assertEqual(
            env.get("VULKAN_READBACK_TEST"),
            "GpuReadbackJobGpuSmoke."
            "VulkanTransferReadbackWritesPropertyAndFollowUpUploadsDerivedColor",
        )
        operational_regex = env.get("VULKAN_OPERATIONAL_TEST_REGEX")
        self.assertIsInstance(operational_regex, str)
        self.assertIn(
            r"GpuReadbackJobGpuSmoke\."
            "VulkanTransferReadbackWritesPropertyAndFollowUpUploadsDerivedColor",
            operational_regex,
        )

        self.assertIn('-E "$VULKAN_OPERATIONAL_TEST_REGEX"', vulkan)
        self.assertGreaterEqual(
            vulkan.count('-R "$VULKAN_OPERATIONAL_TEST_REGEX"'),
            2,
        )
        self.assertIn(
            'if [[ "$operational_test_count" -ne 3 ]]',
            vulkan,
        )
        self.assertIn(
            '--selected-test-count "$((gpu_tests + operational_tests))"',
            vulkan,
        )
        self.assertIn('--output-junit "$operational_junit"', vulkan)
        self.assertIn(
            'failed_outcomes = outcome_tags & {"error", "failure", "skipped"}',
            vulkan,
        )
        self.assertIn(
            "build/ci-vulkan/ci-timing/phases/test_operational_vulkan.json",
            vulkan,
        )
        self.assertIn(
            "build/ci-vulkan/ci-timing/result/operational-vulkan.junit.xml",
            vulkan,
        )
        self.assertIn(
            "--targets ExtrinsicSandbox IntrinsicRuntimeGpuReadbackSmokeTests "
            "IntrinsicGlfwLifecycleLsanProcess",
            vulkan,
        )

    def test_compile_heavy_workflows_cancel_only_matching_stale_runs(self) -> None:
        for name, (_, _, expected_cancellation) in WORKFLOWS.items():
            with self.subTest(workflow=name):
                payload, _ = _load_workflow(name)
                concurrency = payload.get("concurrency")
                self.assertIsInstance(concurrency, dict)
                expected_group = (
                    SANITIZER_GROUP
                    if name == "ci-sanitizers.yml"
                    else EXPECTED_GROUP
                )
                self.assertEqual(concurrency.get("group"), expected_group)
                self.assertEqual(
                    concurrency.get("cancel-in-progress"),
                    expected_cancellation,
                )
        self.assertNotEqual(SANITIZER_GROUP, EXPECTED_GROUP)

    def test_compile_heavy_workflows_emit_one_validated_result_artifact(self) -> None:
        for name, (artifact_name, expected_test_reports, _) in WORKFLOWS.items():
            with self.subTest(workflow=name):
                _, text = _load_workflow(name)
                self.assertEqual(text.count("aggregate_gate_timing.py"), 1)
                self.assertIn(
                    "validate_benchmark_results.py --root ",
                    text,
                )
                self.assertIn("actions/upload-artifact@v4", text)
                self.assertIn(f"name: {artifact_name}", text)
                self.assertIn("if: always()", text)
                self.assertIn("/ci-timing/result/result.json", text)
                self.assertEqual(text.count("--test-json"), expected_test_reports)

    def test_pr_fast_always_publishes_route_and_guards_cpp_steps(self) -> None:
        payload, _ = _load_workflow("pr-fast.yml")
        steps = payload["jobs"]["pr-fast"]["steps"]
        named_steps = {step.get("name"): step for step in steps}
        cpp_steps = {
            "Install system dependencies",
            "Configure ccache pilot",
            "Cache vcpkg binary packages",
            "Bootstrap vcpkg",
            "Enable vcpkg binary cache",
            "Configure (ci-fast preset)",
            "Finalize touched scope",
            "Detect configured compiler and cache identity",
            "Restore compatible ccache store",
            "Validate ccache pilot mode",
            "Run module invalidation ccache probe",
            "Zero ccache stats",
            "Build selected test closure",
            "Collect ccache stats",
            "Run selected tests",
            "Aggregate gate timing result",
            "Validate gate timing result",
            "Upload gate timing result",
            "Upload module invalidation probe result",
            "Save validated ccache store",
        }
        for name in cpp_steps:
            with self.subTest(step=name):
                condition = named_steps[name].get("if", "")
                self.assertIn("steps.route.outputs.needs_cpp == 'true'", condition)

        route_upload = named_steps["Upload touched-scope route"]
        self.assertEqual(route_upload["if"], "always()")
        self.assertEqual(route_upload["uses"], "actions/upload-artifact@v4")
        self.assertEqual(
            route_upload["with"]["name"],
            "ci-pr-fast-touched-scope-route",
        )
        self.assertEqual(
            route_upload["with"]["path"],
            "build/ci-fast/ci-routing/",
        )
        self.assertEqual(route_upload["with"]["if-no-files-found"], "error")
        for name in (
            "Aggregate gate timing result",
            "Validate gate timing result",
            "Upload gate timing result",
            "Upload module invalidation probe result",
        ):
            with self.subTest(always_step=name):
                self.assertEqual(
                    named_steps[name]["if"],
                    "always() && steps.route.outputs.needs_cpp == 'true'",
                )

    def test_structural_workflow_uses_read_only_permissions(self) -> None:
        payload, _ = _load_workflow("ci-docs.yml")
        self.assertEqual(payload["permissions"], {"contents": "read"})

    def test_all_measured_phases_write_versioned_json_inputs(self) -> None:
        for name in WORKFLOWS:
            with self.subTest(workflow=name):
                _, text = _load_workflow(name)
                self.assertIn("time_command.py", text)
                self.assertIn("/ci-timing/phases/configure.json", text)
                self.assertIn("/ci-timing/phases/build.json", text)
                self.assertIn("/ci-timing/phases/test", text)


if __name__ == "__main__":
    unittest.main()
