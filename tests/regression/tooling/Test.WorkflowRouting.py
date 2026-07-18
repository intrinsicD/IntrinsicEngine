#!/usr/bin/env python3
from __future__ import annotations

import json
import unittest
from pathlib import Path

import yaml


REPO_ROOT = Path(__file__).resolve().parents[3]
WORKFLOW_ROOT = REPO_ROOT / ".github" / "workflows"
PRESETS_PATH = REPO_ROOT / "CMakePresets.json"
PR_CANDIDATE_EVENTS = [
    "opened",
    "reopened",
    "synchronize",
    "ready_for_review",
    "converted_to_draft",
]
QUICK_WORKFLOWS = ("ci-docs.yml", "pr-fast.yml")
HEAVY_WORKFLOWS = (
    "ci-linux-clang.yml",
    "ci-vulkan.yml",
    "ci-release.yml",
)


def _load_workflow(name: str) -> tuple[dict[str, object], str]:
    path = WORKFLOW_ROOT / name
    text = path.read_text(encoding="utf-8")
    payload = yaml.safe_load(text)
    if not isinstance(payload, dict):
        raise AssertionError(f"workflow root is not a mapping: {path}")
    return payload, text


def _triggers(payload: dict[str, object]) -> dict[str, object]:
    triggers = payload.get("on", payload.get(True))
    if not isinstance(triggers, dict):
        raise AssertionError("workflow triggers are not a mapping")
    return triggers


def _one_line(value: object) -> str:
    if not isinstance(value, str):
        raise AssertionError("expected a string")
    return " ".join(value.split())


def _named_steps(job: object) -> dict[str, dict[str, object]]:
    if not isinstance(job, dict) or not isinstance(job.get("steps"), list):
        raise AssertionError("workflow job has no steps")
    result: dict[str, dict[str, object]] = {}
    for step in job["steps"]:
        if not isinstance(step, dict):
            raise AssertionError("workflow step is not a mapping")
        name = step.get("name")
        if isinstance(name, str):
            result[name] = step
    return result


class WorkflowRoutingTests(unittest.TestCase):
    def test_quick_feedback_covers_every_candidate_update_and_merge_group(
        self,
    ) -> None:
        for name in QUICK_WORKFLOWS:
            with self.subTest(workflow=name):
                payload, _ = _load_workflow(name)
                triggers = _triggers(payload)
                self.assertEqual(
                    triggers["pull_request"]["types"],
                    PR_CANDIDATE_EVENTS,
                )
                self.assertEqual(
                    triggers["merge_group"]["types"],
                    ["checks_requested"],
                )
                self.assertIn("workflow_dispatch", triggers)

    def test_heavy_workflows_cover_candidate_events_without_path_filters(
        self,
    ) -> None:
        for name in HEAVY_WORKFLOWS:
            with self.subTest(workflow=name):
                payload, text = _load_workflow(name)
                triggers = _triggers(payload)
                self.assertEqual(
                    triggers["pull_request"]["types"],
                    PR_CANDIDATE_EVENTS,
                )
                self.assertEqual(
                    triggers["merge_group"]["types"],
                    ["checks_requested"],
                )
                self.assertIn("workflow_dispatch", triggers)
                self.assertNotIn("paths:", text)
                self.assertNotIn("paths-ignore:", text)

    def test_cpu_and_vulkan_required_results_are_stable_and_draft_safe(
        self,
    ) -> None:
        cases = (
            (
                "ci-linux-clang.yml",
                "ci-linux-clang",
                "full-cpu",
                "required-ci-linux-clang",
                "ci-linux-clang",
                {
                    "ci-linux-clang",
                    "sanitizer-tests",
                    "cpu-test-selection-parity",
                },
            ),
            (
                "ci-vulkan.yml",
                "ci-vulkan",
                "vulkan-tests",
                "required-ci-vulkan",
                "ci-vulkan",
                {"ci-vulkan"},
            ),
        )
        for (
            workflow_name,
            implementation_id,
            implementation_name,
            result_id,
            result_name,
            expected_needs,
        ) in cases:
            with self.subTest(workflow=workflow_name):
                payload, _ = _load_workflow(workflow_name)
                jobs = payload["jobs"]
                implementation = jobs[implementation_id]
                result = jobs[result_id]
                self.assertEqual(implementation["name"], implementation_name)
                implementation_if = _one_line(implementation["if"])
                self.assertIn(
                    "github.event_name != 'pull_request' || "
                    "!github.event.pull_request.draft",
                    implementation_if,
                )
                self.assertEqual(result["name"], result_name)
                needs = result["needs"]
                if isinstance(needs, str):
                    needs = [needs]
                self.assertEqual(set(needs), expected_needs)
                self.assertIn("always()", _one_line(result["if"]))
                resolve = next(iter(_named_steps(result).values()))
                command = _one_line(resolve["run"])
                self.assertIn('"$PR_DRAFT" == "true"', command)
                self.assertIn("exit 1", command)

    def test_cpu_candidate_path_requires_sanitizers_and_selection_parity(
        self,
    ) -> None:
        payload, _ = _load_workflow("ci-linux-clang.yml")
        jobs = payload["jobs"]
        for name in ("sanitizer-tests", "cpu-test-selection-parity"):
            with self.subTest(job=name):
                condition = _one_line(jobs[name]["if"])
                self.assertIn(
                    "github.event_name == 'merge_group'",
                    condition,
                )
                self.assertIn(
                    "github.event_name == 'pull_request' && "
                    "!github.event.pull_request.draft",
                    condition,
                )
        parity_needs = set(jobs["cpu-test-selection-parity"]["needs"])
        self.assertEqual(
            parity_needs,
            {"ci-linux-clang", "sanitizer-tests"},
        )

    def test_release_lane_is_optimized_path_aware_and_always_reports(
        self,
    ) -> None:
        presets = json.loads(PRESETS_PATH.read_text(encoding="utf-8"))
        configure = {
            preset["name"]: preset for preset in presets["configurePresets"]
        }["ci-release"]
        builds = {
            preset["name"]: preset for preset in presets["buildPresets"]
        }
        self.assertEqual(configure["inherits"], "ci")
        self.assertEqual(
            configure["cacheVariables"],
            {
                "CMAKE_BUILD_TYPE": "Release",
                "INTRINSIC_BUILD_SANDBOX": "OFF",
                "INTRINSIC_BUILD_TESTS": "ON",
                "INTRINSIC_BUILD_BENCHMARKS": "ON",
                "INTRINSIC_ENABLE_SANITIZERS": "OFF",
                "INTRINSIC_SANITIZER_MODE": "none",
                "INTRINSIC_ENABLE_SOURCE_COVERAGE": "OFF",
            },
        )
        self.assertEqual(builds["ci-release"]["configurePreset"], "ci-release")

        payload, text = _load_workflow("ci-release.yml")
        route = payload["jobs"]["release_route"]
        route_steps = _named_steps(route)
        plan = _one_line(route_steps["Plan Release confidence scope"]["run"])
        self.assertIn("tools/ci/touched_scope.py", plan)
        self.assertIn("--action plan", plan)
        self.assertIn('--github-output "$GITHUB_OUTPUT"', plan)
        self.assertEqual(
            route["outputs"]["needs_release"],
            "${{ steps.route.outputs.needs_cpp }}",
        )

        optimized = payload["jobs"]["optimized_release"]
        optimized_if = _one_line(optimized["if"])
        self.assertIn("!github.event.pull_request.draft", optimized_if)
        self.assertIn(
            "needs.release_route.outputs.needs_release == 'true'",
            optimized_if,
        )
        steps = _named_steps(optimized)
        build = _one_line(
            steps["Build Release benchmark and SLO targets"]["run"]
        )
        self.assertIn("--preset ci-release", build)
        self.assertIn(
            "--target IntrinsicBenchmarkSmoke IntrinsicBenchmarkTests",
            build,
        )
        slo = steps["Run Release architecture SLO"]
        self.assertFalse(slo.get("continue-on-error", False))
        slo_command = _one_line(slo["run"])
        self.assertIn('-L "^slo$"', slo_command)
        self.assertIn("--parallel 1", slo_command)
        self.assertIn(
            "--output-junit reports/architecture-slo.junit.xml",
            slo_command,
        )
        benchmark = steps["Run Release benchmark smoke"]
        self.assertFalse(benchmark.get("continue-on-error", False))
        self.assertIn(
            "--target IntrinsicBenchmarks",
            _one_line(benchmark["run"]),
        )
        self.assertIn(
            "validate_benchmark_results.py "
            "--root build/ci-release/benchmark --strict",
            _one_line(steps["Validate benchmark result JSON"]["run"]),
        )

        result = payload["jobs"]["required_ci_release"]
        self.assertEqual(result["name"], "ci-release")
        self.assertEqual(
            set(result["needs"]),
            {"release_route", "optimized_release"},
        )
        self.assertEqual(result["if"], "always()")
        resolve = _named_steps(result)["Resolve ci-release lifecycle result"]
        command = _one_line(resolve["run"])
        self.assertIn('"$NEEDS_RELEASE" != "true"', command)
        self.assertIn('"$RELEASE_RESULT" != "skipped"', command)
        self.assertIn('"$RELEASE_RESULT" != "success"', command)
        self.assertNotIn("actions/download-artifact", text)
        self.assertFalse((WORKFLOW_ROOT / "ci-bench-smoke.yml").exists())

    def test_complete_source_coverage_is_weekly_and_manual(self) -> None:
        payload, _ = _load_workflow("ci-source-coverage.yml")
        triggers = _triggers(payload)
        self.assertEqual(
            set(triggers),
            {"schedule", "workflow_dispatch"},
        )
        self.assertEqual(triggers["schedule"], [{"cron": "0 5 * * 1"}])

    def test_nightly_no_longer_owns_debug_slo(self) -> None:
        payload, text = _load_workflow("nightly-deep.yml")
        steps = _named_steps(payload["jobs"]["nightly-cpu-deep"])
        self.assertNotIn("Run SLO/performance diagnostic (CI-009)", steps)
        self.assertNotIn("IntrinsicBenchmarkTests", text)
        self.assertNotIn("architecture-slo.junit.xml", text)


if __name__ == "__main__":
    unittest.main()
