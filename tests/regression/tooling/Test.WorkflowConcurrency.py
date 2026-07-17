#!/usr/bin/env python3
from __future__ import annotations

import unittest
from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parents[3]
WORKFLOW_ROOT = REPO_ROOT / ".github" / "workflows"
EXPECTED_GROUP = (
    "${{ github.workflow }}-${{ github.event.pull_request.number || github.ref }}"
)
WORKFLOWS = {
    "pr-fast.yml": ("ci-gate-timing-pr-fast", 1, True),
    "ci-linux-clang.yml": (
        "ci-gate-timing-ci-linux-clang",
        2,
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

    def test_pr_fast_keeps_kernel_convergence_guards(self) -> None:
        _, pr_fast = _load_workflow("pr-fast.yml")
        self.assertEqual(
            pr_fast.count(
                "python3 tests/regression/tooling/Test.CheckKernelConvergence.py"
            ),
            1,
        )
        self.assertEqual(
            pr_fast.count(
                "python3 tools/repo/check_kernel_convergence.py --root . --strict"
            ),
            1,
        )

    def test_specialized_workflows_build_only_selected_aggregates(self) -> None:
        _, pr_fast = _load_workflow("pr-fast.yml")
        self.assertIn(
            "cmake --build --preset ci --target IntrinsicPrFastTests",
            pr_fast,
        )
        self.assertIn(
            "--inventory build/ci/test-inventories/IntrinsicPrFastTests.txt",
            pr_fast,
        )
        self.assertNotIn(
            "cmake --build --preset ci --target IntrinsicTests",
            pr_fast,
        )

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
                self.assertEqual(concurrency.get("group"), EXPECTED_GROUP)
                self.assertEqual(
                    concurrency.get("cancel-in-progress"),
                    expected_cancellation,
                )

    def test_compile_heavy_workflows_emit_one_validated_result_artifact(self) -> None:
        for name, (artifact_name, expected_test_reports, _) in WORKFLOWS.items():
            with self.subTest(workflow=name):
                _, text = _load_workflow(name)
                self.assertEqual(text.count("aggregate_gate_timing.py"), 1)
                self.assertIn(
                    "validate_benchmark_results.py --root build/",
                    text,
                )
                self.assertIn("actions/upload-artifact@v4", text)
                self.assertIn(f"name: {artifact_name}", text)
                self.assertIn("if: always()", text)
                self.assertIn("/ci-timing/result/result.json", text)
                self.assertEqual(text.count("--test-json"), expected_test_reports)

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
