#!/usr/bin/env python3
from __future__ import annotations

import unittest
from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parents[3]
WORKFLOW_ROOT = REPO_ROOT / ".github" / "workflows"
EXPECTED_GROUP = "${{ github.workflow }}-${{ github.event.pull_request.number || github.ref }}"
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
}


def _load_workflow(name: str) -> tuple[dict[str, object], str]:
    path = WORKFLOW_ROOT / name
    text = path.read_text(encoding="utf-8")
    payload = yaml.safe_load(text)
    if not isinstance(payload, dict):
        raise AssertionError(f"workflow root is not a mapping: {path}")
    return payload, text


class WorkflowConcurrencyTests(unittest.TestCase):
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

        _, vulkan = _load_workflow("ci-vulkan.yml")
        self.assertIn(
            "--target ExtrinsicSandbox IntrinsicGpuVulkanTests",
            vulkan,
        )
        self.assertIn(
            "--inventory "
            "build/ci-vulkan/test-inventories/IntrinsicGpuVulkanTests.txt",
            vulkan,
        )
        self.assertNotIn(
            "--target ExtrinsicSandbox IntrinsicTests",
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
                    "validate_benchmark_results.py --root "
                    "build/",
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
