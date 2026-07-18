#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import subprocess
import tempfile
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


def _run_script(
    script: str,
    env: dict[str, str],
) -> subprocess.CompletedProcess[str]:
    runtime_env = os.environ.copy()
    runtime_env.update(env)
    return subprocess.run(
        [
            "bash",
            "--noprofile",
            "--norc",
            "-eu",
            "-o",
            "pipefail",
            "-c",
            script,
        ],
        cwd=REPO_ROOT,
        env=runtime_env,
        text=True,
        capture_output=True,
        check=False,
    )


def _run_workflow_step(
    workflow_name: str,
    job_name: str,
    step_name: str,
    env: dict[str, str],
) -> subprocess.CompletedProcess[str]:
    payload, _ = _load_workflow(workflow_name)
    step = _named_steps(payload["jobs"][job_name])[step_name]
    script = step.get("run")
    if not isinstance(script, str):
        raise AssertionError(f"{workflow_name}:{job_name}:{step_name} has no script")
    return _run_script(script, env)


def _run_route_step(
    workflow_name: str,
    job_name: str,
    step_name: str,
    *,
    event_name: str,
    pr_base: str = "pr-base",
    pr_head: str = "pr-head",
    merge_base: str = "merge-base",
    merge_head: str = "merge-head",
    stub_exit: int = 0,
) -> tuple[subprocess.CompletedProcess[str], list[str]]:
    with tempfile.TemporaryDirectory(prefix="intrinsic-ci009-route-") as temp:
        temp_root = Path(temp)
        bin_dir = temp_root / "bin"
        bin_dir.mkdir()
        captured_args = temp_root / "args.txt"
        python_stub = bin_dir / "python3"
        python_stub.write_text(
            "#!/usr/bin/env bash\n"
            'printf "%s\\n" "$@" > "$CAPTURE_ARGS"\n'
            'exit "$STUB_EXIT"\n',
            encoding="utf-8",
        )
        python_stub.chmod(0o755)
        result = _run_workflow_step(
            workflow_name,
            job_name,
            step_name,
            {
                "EVENT_NAME": event_name,
                "PR_BASE_SHA": pr_base,
                "PR_HEAD_SHA": pr_head,
                "MERGE_GROUP_BASE_SHA": merge_base,
                "MERGE_GROUP_HEAD_SHA": merge_head,
                "GITHUB_OUTPUT": str(temp_root / "github-output.txt"),
                "GITHUB_STEP_SUMMARY": str(temp_root / "summary.md"),
                "CAPTURE_ARGS": str(captured_args),
                "STUB_EXIT": str(stub_exit),
                "PATH": f"{bin_dir}:{os.environ['PATH']}",
            },
        )
        args = (
            captured_args.read_text(encoding="utf-8").splitlines()
            if captured_args.exists()
            else []
        )
        return result, args


def _argument_value(arguments: list[str], flag: str) -> str:
    try:
        return arguments[arguments.index(flag) + 1]
    except (ValueError, IndexError) as error:
        raise AssertionError(f"missing argument {flag}: {arguments}") from error


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
        linux, _ = _load_workflow("ci-linux-clang.yml")
        self.assertEqual(
            _triggers(linux)["push"],
            {"branches": ["main"]},
        )

    def test_route_scripts_execute_event_specific_refs_and_fail_closed(
        self,
    ) -> None:
        route_steps = (
            (
                "pr-fast.yml",
                "pr-fast",
                "Plan touched scope",
                "tools/ci/touched_scope.py",
            ),
            (
                "ci-release.yml",
                "release_route",
                "Plan Release confidence scope",
                "tools/ci/touched_scope.py",
            ),
            (
                "ci-docs.yml",
                "docs-validation",
                "Validate documentation synchronization (strict mode)",
                "tools/docs/check_docs_sync.py",
            ),
        )
        event_refs = (
            ("pull_request", "pr-base", "pr-head"),
            ("merge_group", "merge-base", "merge-head"),
            ("workflow_dispatch", "origin/main", "HEAD"),
        )
        expected_env = {
            "EVENT_NAME": "${{ github.event_name }}",
            "PR_BASE_SHA": "${{ github.event.pull_request.base.sha }}",
            "PR_HEAD_SHA": "${{ github.event.pull_request.head.sha }}",
            "MERGE_GROUP_BASE_SHA": "${{ github.event.merge_group.base_sha }}",
            "MERGE_GROUP_HEAD_SHA": "${{ github.event.merge_group.head_sha }}",
        }
        for workflow, job, step, script_path in route_steps:
            payload, _ = _load_workflow(workflow)
            route_step = _named_steps(payload["jobs"][job])[step]
            self.assertEqual(route_step["env"], expected_env)
            for event_name, expected_base, expected_head in event_refs:
                with self.subTest(workflow=workflow, event=event_name):
                    result, arguments = _run_route_step(
                        workflow,
                        job,
                        step,
                        event_name=event_name,
                    )
                    self.assertEqual(
                        result.returncode,
                        0,
                        result.stdout + result.stderr,
                    )
                    self.assertEqual(arguments[0], script_path)
                    self.assertEqual(
                        _argument_value(arguments, "--base-ref"),
                        expected_base,
                    )
                    self.assertEqual(
                        _argument_value(arguments, "--head-ref"),
                        expected_head,
                    )

            for event_name, missing in (
                ("pull_request", {"pr_base": "", "pr_head": ""}),
                ("merge_group", {"merge_base": "", "merge_head": ""}),
            ):
                with self.subTest(
                    workflow=workflow,
                    event=event_name,
                    state="missing-refs",
                ):
                    result, arguments = _run_route_step(
                        workflow,
                        job,
                        step,
                        event_name=event_name,
                        **missing,
                    )
                    self.assertNotEqual(result.returncode, 0)
                    self.assertEqual(arguments, [])

            with self.subTest(workflow=workflow, state="route-command-failure"):
                result, _ = _run_route_step(
                    workflow,
                    job,
                    step,
                    event_name="merge_group",
                    stub_exit=17,
                )
                self.assertEqual(result.returncode, 17)

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
                self.assertIn('!= "skipped"', command)
                self.assertIn("exit 1", command)

    def test_cpu_required_result_script_executes_success_failure_and_skip_cases(
        self,
    ) -> None:
        base = {
            "EVENT_NAME": "pull_request",
            "PR_DRAFT": "false",
            "CPU_RESULT": "success",
            "SANITIZER_RESULT": "success",
            "PARITY_RESULT": "success",
        }
        cases = (
            ("opened-ready", {}, True),
            ("reopened-ready", {}, True),
            ("synchronize-ready", {}, True),
            ("ready-for-review", {}, True),
            ("merge-group", {"EVENT_NAME": "merge_group"}, True),
            (
                "converted-to-draft",
                {
                    "PR_DRAFT": "true",
                    "CPU_RESULT": "skipped",
                    "SANITIZER_RESULT": "skipped",
                    "PARITY_RESULT": "skipped",
                },
                True,
            ),
            (
                "draft-implementation-ran",
                {
                    "PR_DRAFT": "true",
                    "CPU_RESULT": "success",
                    "SANITIZER_RESULT": "skipped",
                    "PARITY_RESULT": "skipped",
                },
                False,
            ),
            ("cpu-failed", {"CPU_RESULT": "failure"}, False),
            ("sanitizer-skipped", {"SANITIZER_RESULT": "skipped"}, False),
            ("parity-cancelled", {"PARITY_RESULT": "cancelled"}, False),
        )
        for name, overrides, succeeds in cases:
            with self.subTest(case=name):
                env = base | overrides
                result = _run_workflow_step(
                    "ci-linux-clang.yml",
                    "required-ci-linux-clang",
                    "Resolve ci-linux-clang lifecycle result",
                    env,
                )
                if succeeds:
                    self.assertEqual(
                        result.returncode,
                        0,
                        result.stdout + result.stderr,
                    )
                else:
                    self.assertNotEqual(result.returncode, 0)

    def test_vulkan_required_result_script_executes_failure_and_skip_cases(
        self,
    ) -> None:
        base = {
            "EVENT_NAME": "pull_request",
            "PR_DRAFT": "false",
            "VULKAN_RESULT": "success",
        }
        cases = (
            ("ready", {}, True),
            ("merge-group", {"EVENT_NAME": "merge_group"}, True),
            (
                "draft-skipped",
                {"PR_DRAFT": "true", "VULKAN_RESULT": "skipped"},
                True,
            ),
            (
                "draft-implementation-ran",
                {"PR_DRAFT": "true", "VULKAN_RESULT": "success"},
                False,
            ),
            ("implementation-failed", {"VULKAN_RESULT": "failure"}, False),
            ("implementation-skipped", {"VULKAN_RESULT": "skipped"}, False),
        )
        for name, overrides, succeeds in cases:
            with self.subTest(case=name):
                env = base | overrides
                result = _run_workflow_step(
                    "ci-vulkan.yml",
                    "required-ci-vulkan",
                    "Resolve ci-vulkan lifecycle result",
                    env,
                )
                if succeeds:
                    self.assertEqual(
                        result.returncode,
                        0,
                        result.stdout + result.stderr,
                    )
                else:
                    self.assertNotEqual(result.returncode, 0)

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
        self.assertIn(
            '"$NEEDS_RELEASE" != "true" && "$NEEDS_RELEASE" != "false"',
            command,
        )
        self.assertIn('"$NEEDS_RELEASE" != "true"', command)
        self.assertIn('"$RELEASE_RESULT" != "skipped"', command)
        self.assertIn('"$RELEASE_RESULT" != "success"', command)
        self.assertNotIn("actions/download-artifact", text)
        self.assertFalse((WORKFLOW_ROOT / "ci-bench-smoke.yml").exists())

    def test_release_result_script_executes_route_failure_and_skip_contract(
        self,
    ) -> None:
        base = {
            "EVENT_NAME": "pull_request",
            "PR_DRAFT": "false",
            "NEEDS_RELEASE": "true",
            "ROUTE_RESULT": "success",
            "RELEASE_RESULT": "success",
        }
        cases = (
            ("ready-required-success", {}, True),
            ("merge-group-success", {"EVENT_NAME": "merge_group"}, True),
            (
                "manual-independent-success",
                {
                    "EVENT_NAME": "workflow_dispatch",
                    "NEEDS_RELEASE": "false",
                },
                True,
            ),
            (
                "draft-valid-skip",
                {
                    "PR_DRAFT": "true",
                    "NEEDS_RELEASE": "false",
                    "RELEASE_RESULT": "skipped",
                },
                True,
            ),
            (
                "path-valid-skip",
                {
                    "NEEDS_RELEASE": "false",
                    "RELEASE_RESULT": "skipped",
                },
                True,
            ),
            ("route-failed", {"ROUTE_RESULT": "failure"}, False),
            ("route-cancelled", {"ROUTE_RESULT": "cancelled"}, False),
            ("invalid-route-verdict", {"NEEDS_RELEASE": ""}, False),
            (
                "draft-invalid-success",
                {
                    "PR_DRAFT": "true",
                    "NEEDS_RELEASE": "false",
                    "RELEASE_RESULT": "success",
                },
                False,
            ),
            (
                "path-invalid-success",
                {
                    "NEEDS_RELEASE": "false",
                    "RELEASE_RESULT": "success",
                },
                False,
            ),
            ("required-implementation-failed", {"RELEASE_RESULT": "failure"}, False),
            ("required-implementation-skipped", {"RELEASE_RESULT": "skipped"}, False),
            (
                "manual-implementation-failed",
                {
                    "EVENT_NAME": "workflow_dispatch",
                    "NEEDS_RELEASE": "false",
                    "RELEASE_RESULT": "failure",
                },
                False,
            ),
        )
        for name, overrides, succeeds in cases:
            with self.subTest(case=name):
                env = base | overrides
                result = _run_workflow_step(
                    "ci-release.yml",
                    "required_ci_release",
                    "Resolve ci-release lifecycle result",
                    env,
                )
                if succeeds:
                    self.assertEqual(
                        result.returncode,
                        0,
                        result.stdout + result.stderr,
                    )
                else:
                    self.assertNotEqual(result.returncode, 0)

    def test_complete_source_coverage_is_weekly_and_manual(self) -> None:
        payload, _ = _load_workflow("ci-source-coverage.yml")
        triggers = _triggers(payload)
        self.assertEqual(
            set(triggers),
            {"schedule", "workflow_dispatch"},
        )
        self.assertEqual(triggers["schedule"], [{"cron": "0 3 * * 1"}])

    def test_nightly_no_longer_owns_debug_slo(self) -> None:
        payload, text = _load_workflow("nightly-deep.yml")
        steps = _named_steps(payload["jobs"]["nightly-cpu-deep"])
        self.assertNotIn("Run SLO/performance diagnostic (CI-009)", steps)
        self.assertNotIn("IntrinsicBenchmarkTests", text)
        self.assertNotIn("architecture-slo.junit.xml", text)


if __name__ == "__main__":
    unittest.main()
