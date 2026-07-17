#!/usr/bin/env python3
from __future__ import annotations

import json
import re
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
CPU_ENGINE_CONFIG_ROOTS = (
    ("tests/contract/runtime/Test.AssetImportFormatCoverage.cpp", "HeadlessConfig"),
    ("tests/contract/runtime/Test.EditorUiHost.cpp", "HeadlessConfig"),
    ("tests/contract/runtime/Test.GizmoInteractionEngineWiring.cpp", "HeadlessConfig"),
    (
        "tests/contract/runtime/Test.GpuAssetCacheFallbackBootstrap.cpp",
        "SingleWorkerEngineConfig",
    ),
    ("tests/contract/runtime/Test.GraphGeometryExtraction.cpp", "HeadlessConfig"),
    ("tests/contract/runtime/Test.ImGuiAdapterEngineWiring.cpp", "HeadlessConfig"),
    ("tests/contract/runtime/Test.ImGuiAdapterEngineWiring.cpp", "InputRoutingConfig"),
    ("tests/contract/runtime/Test.MeshGeometryExtraction.cpp", "HeadlessConfig"),
    ("tests/contract/runtime/Test.MeshPrimitiveViewExtraction.cpp", "HeadlessConfig"),
    ("tests/contract/runtime/Test.PointCloudGeometryExtraction.cpp", "HeadlessConfig"),
    ("tests/contract/runtime/Test.ProceduralGeometryExtraction.cpp", "HeadlessConfig"),
    ("tests/contract/runtime/Test.RenderExtractionContract.cpp", "HeadlessConfig"),
    ("tests/contract/runtime/Test.RenderWorldPoolEngineWiring.cpp", "PoolConfig"),
    ("tests/contract/runtime/Test.RuntimeConfigControlFacade.cpp", "HeadlessConfig"),
    ("tests/contract/runtime/Test.RuntimeInputActions.cpp", "InputActionConfig"),
    ("tests/contract/runtime/Test.RuntimeJobService.cpp", "NullWindowHeadlessConfig"),
    ("tests/contract/runtime/Test.RuntimeKernelEvents.cpp", "NullWindowHeadlessConfig"),
    ("tests/contract/runtime/Test.RuntimeModule.cpp", "NullWindowHeadlessConfig"),
    (
        "tests/contract/runtime/Test.RuntimeReferenceScene.cpp",
        "SingleWorkerEngineConfig",
    ),
    ("tests/contract/runtime/Test.RuntimeRenderRecipeActivation.cpp", "HeadlessConfig"),
    ("tests/contract/runtime/Test.RuntimeSceneLifecycle.cpp", "HeadlessConfig"),
    (
        "tests/contract/runtime/Test.RuntimeVulkanBreadcrumb.cpp",
        "SingleWorkerEngineConfig",
    ),
    (
        "tests/contract/runtime/Test.RuntimeWorldRegistry.cpp",
        "NullWindowHeadlessConfig",
    ),
    ("tests/contract/runtime/Test.SandboxEditorMeshMethods.cpp", "HeadlessConfig"),
    ("tests/contract/runtime/Test.SandboxEditorSceneCommands.cpp", "HeadlessConfig"),
    ("tests/contract/runtime/Test.SandboxEditorSessionLifecycle.cpp", "HeadlessConfig"),
    ("tests/contract/runtime/Test.SandboxEditorVisualization.cpp", "HeadlessConfig"),
    ("tests/contract/runtime/Test.SelectionSnapshotExtraction.cpp", "HeadlessConfig"),
    (
        "tests/contract/runtime/Test.SelectionStableLookupComposition.cpp",
        "HeadlessConfig",
    ),
    ("tests/integration/runtime/Test.RuntimeSandboxAcceptance.cpp", "HeadlessConfig"),
    ("tests/integration/runtime/Test.SandboxDomainPanels.cpp", "HeadlessConfig"),
    ("tests/integration/runtime/Test.SandboxEditorPresentation.cpp", "HeadlessConfig"),
    (
        "tests/integration/runtime/Test.SandboxParameterizationPanel.cpp",
        "HeadlessConfig",
    ),
)
CPU_MULTIWORKER_SOURCE_TARGETS = {
    "tests/unit/core/Test.CoreTasks.cpp": "IntrinsicCoreWrapperUnitTests",
    "tests/unit/core/Test.CoreFrameGraph.cpp": "IntrinsicCoreWrapperUnitTests",
    "tests/unit/core/Test.Core.TaskGraphLegacy.cpp": "IntrinsicCoreWrapperUnitTests",
    "tests/unit/core/Test.Core.TaskGraphCompletionLifetime.cpp": (
        "IntrinsicCoreWrapperUnitTests"
    ),
    "tests/integration/runtime/Test.CoreFrameGraphParallel.cpp": (
        "IntrinsicRuntimeIntegrationTests"
    ),
    "tests/integration/runtime/Test.CoreGraphStress.cpp": (
        "IntrinsicRuntimeIntegrationTests"
    ),
    "tests/contract/runtime/Test.RuntimeJobService.cpp": (
        "IntrinsicRuntimeContractTests"
    ),
    "tests/contract/runtime/Test.RuntimeWorldRegistry.cpp": (
        "IntrinsicRuntimeContractTests"
    ),
    "tests/contract/runtime/Test.ClusteringModule.cpp": (
        "IntrinsicRuntimeContractTests"
    ),
    "tests/contract/graphics/Test.RenderGraphParallelRecording.cpp": (
        "IntrinsicGraphicsContractCpuTests"
    ),
    "tests/contract/graphics/Test.RendererFrameLifecycle.cpp": (
        "IntrinsicGraphicsContractCpuTests"
    ),
}


def _load_workflow(name: str) -> tuple[dict[str, object], str]:
    path = WORKFLOW_ROOT / name
    text = path.read_text(encoding="utf-8")
    payload = yaml.safe_load(text)
    if not isinstance(payload, dict):
        raise AssertionError(f"workflow root is not a mapping: {path}")
    return payload, text


def _function_body(text: str, function_name: str) -> str:
    signature = re.search(
        rf"\b{re.escape(function_name)}\s*\([^)]*\)\s*\{{",
        text,
    )
    if signature is None:
        raise AssertionError(f"function not found: {function_name}")

    opening_brace = signature.end() - 1
    depth = 0
    for offset in range(opening_brace, len(text)):
        if text[offset] == "{":
            depth += 1
        elif text[offset] == "}":
            depth -= 1
            if depth == 0:
                return text[opening_brace : offset + 1]
    raise AssertionError(f"unterminated function body: {function_name}")


def _gtest_bodies(text: str) -> dict[str, str]:
    declarations = list(
        re.finditer(
            r"\bTEST(?:_F|_P)?\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,\s*"
            r"([A-Za-z_][A-Za-z0-9_]*)\s*\)",
            text,
        )
    )
    bodies: dict[str, str] = {}
    for index, declaration in enumerate(declarations):
        end = (
            declarations[index + 1].start()
            if index + 1 < len(declarations)
            else len(text)
        )
        name = f"{declaration.group(1)}.{declaration.group(2)}"
        bodies[name] = text[declaration.end() : end]
    return bodies


def _scheduler_peak_slots(requested_workers: int) -> int:
    actual_workers = (
        requested_workers - 1 if requested_workers > 2 else requested_workers
    )
    return actual_workers + 1


def _source_multiworker_budgets() -> set[tuple[str, str, int]]:
    budgets: set[tuple[str, str, int]] = set()
    literal_patterns = (
        re.compile(
            r"(?:[A-Za-z_][A-Za-z0-9_]*::)*Scheduler::Initialize"
            r"\(\s*([0-9]+)u?\s*\)"
        ),
        re.compile(
            r"(?:SchedulerScope|SchedulerFixture)\s+[A-Za-z_][A-Za-z0-9_]*"
            r"\s*[\{\(]\s*([0-9]+)u?\s*[\}\)]"
        ),
    )

    for relative_path, target in CPU_MULTIWORKER_SOURCE_TARGETS.items():
        source = (REPO_ROOT / relative_path).read_text(encoding="utf-8")
        for name, body in _gtest_bodies(source).items():
            if "hardware_concurrency" in body:
                raise AssertionError(
                    f"host-derived scheduler pool in {relative_path}:{name}"
                )

            case_budgets = {
                int(match.group(1))
                for pattern in literal_patterns
                for match in pattern.finditer(body)
                if int(match.group(1)) > 1
            }
            for match in re.finditer(
                r"(?:[A-Za-z_][A-Za-z0-9_]*::)*Scheduler::Initialize"
                r"\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)",
                body,
            ):
                symbol = match.group(1)
                declaration = re.search(
                    rf"\bconstexpr\s+(?:unsigned|int)\s+{re.escape(symbol)}"
                    r"\s*=\s*([0-9]+)u?\s*;",
                    body,
                )
                if declaration is not None and int(declaration.group(1)) > 1:
                    case_budgets.add(int(declaration.group(1)))

            if (
                relative_path == "tests/contract/runtime/Test.ClusteringModule.cpp"
                and re.search(r"NullWindowHeadlessConfig\(\s*\)", body)
            ):
                case_budgets.add(2)

            if len(case_budgets) > 1:
                raise AssertionError(
                    f"ambiguous worker budgets in {relative_path}:{name}: "
                    f"{sorted(case_budgets)}"
                )
            for budget in case_budgets:
                budgets.add((target, name, _scheduler_peak_slots(budget)))
    return budgets


class WorkflowConcurrencyTests(unittest.TestCase):
    def test_cpu_engine_config_roots_use_one_worker(self) -> None:
        self.assertEqual(len(CPU_ENGINE_CONFIG_ROOTS), 33)
        self.assertEqual(
            len({path for path, _ in CPU_ENGINE_CONFIG_ROOTS}),
            32,
        )
        worker_budget = "config.Simulation.WorkerThreadCount = 1u;"

        for relative_path, root in CPU_ENGINE_CONFIG_ROOTS:
            source = (REPO_ROOT / relative_path).read_text(encoding="utf-8")
            with self.subTest(path=relative_path, root=root):
                body = _function_body(source, root)
                self.assertEqual(body.count(worker_budget), 1)

    def test_parallelism_sensitive_engine_config_keeps_explicit_budget(
        self,
    ) -> None:
        source = (
            REPO_ROOT / "tests/contract/runtime/Test.ClusteringModule.cpp"
        ).read_text(encoding="utf-8")
        body = _function_body(source, "NullWindowHeadlessConfig")
        self.assertIn("const unsigned workers = 2u)", source)
        self.assertEqual(
            body.count("config.Simulation.WorkerThreadCount = workers;"),
            1,
        )
        self.assertEqual(source.count("NullWindowHeadlessConfig(),"), 2)
        self.assertEqual(source.count("NullWindowHeadlessConfig(1u),"), 1)

    def test_exact_multiworker_ctest_budgets_match_cpu_sources(self) -> None:
        cmake = (REPO_ROOT / "tests/CMakeLists.txt").read_text(encoding="utf-8")
        budget_block = re.search(
            r"set\(_intrinsic_multiworker_test_budgets\s*"
            r"(?P<body>.*?)\n\)",
            cmake,
            re.DOTALL,
        )
        self.assertIsNotNone(budget_block)
        declared = {
            (target, name, int(budget))
            for target, name, budget in re.findall(
                r'"([^"|]+)\|([^"|]+)\|([0-9]+)"',
                budget_block.group("body"),
            )
        }
        source_budgets = _source_multiworker_budgets()

        self.assertEqual(declared, source_budgets)
        self.assertEqual(len(declared), 41)
        self.assertEqual(
            {
                budget: sum(
                    declared_budget == budget for _, _, declared_budget in declared
                )
                for budget in (3, 4)
            },
            {3: 22, 4: 19},
        )
        self.assertIn(
            "Declared multi-worker test "
            "'${_intrinsic_worker_budget_test}' was not discovered",
            cmake,
        )
        self.assertNotRegex(
            budget_block.group("body"),
            r"\*|Intrinsic[A-Za-z]+Tests\|[^|]*\.\*",
        )

    def test_ci_fast_preset_is_unsanitized_and_headless(self) -> None:
        payload = json.loads(PRESETS.read_text(encoding="utf-8"))
        configure_presets = {
            preset["name"]: preset for preset in payload["configurePresets"]
        }
        build_presets = {preset["name"]: preset for preset in payload["buildPresets"]}
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
        dispatch = triggers["workflow_dispatch"]
        self.assertEqual(
            dispatch["inputs"]["compare_grouped_ctest"],
            {
                "description": ("Compare individual and grouped CTest source coverage"),
                "required": False,
                "default": False,
                "type": "boolean",
            },
        )
        self.assertEqual(
            coverage.count("-- cmake --preset ci-coverage-cpu --fresh"),
            1,
        )
        self.assertEqual(coverage.count("cmake --build"), 1)
        self.assertIn(
            "cmake --build --preset ci-coverage-cpu --target IntrinsicCpuCoverageTests",
            coverage,
        )
        self.assertIn(
            "run_source_coverage.py \\\n"
            "            --build-dir build/ci-coverage-cpu \\\n"
            "            --output build/ci-coverage-cpu/coverage \\\n"
            "            --preset ci-coverage-cpu \\\n"
            "            --cohort cpu-coverage \\\n"
            "            --diff-base HEAD^ \\\n"
            "            --jobs 1",
            coverage,
        )
        self.assertIn(
            "compare_source_coverage.py \\\n"
            "            --baseline build/ci-coverage-cpu/coverage/coverage.json \\\n"
            "            --candidate build/ci-coverage-cpu/coverage/coverage.json \\\n"
            "            --test-only-refactor",
            coverage,
        )
        self.assertIn(
            '-LE "^(benchmark|slo|gpu|vulkan|flaky-quarantine)$"',
            coverage,
        )
        steps = payload["jobs"]["cpu-source-coverage"]["steps"]
        named_steps = {step.get("name"): step for step in steps}
        reconfigure = named_steps["Reconfigure grouped CTest coverage registration"]
        collect = named_steps["Collect grouped CTest source coverage"]
        compare = named_steps["Compare grouped CTest source coverage"]
        baseline_upload = named_steps["Upload CPU source-coverage artifact"]
        upload = named_steps["Upload grouped CTest source-coverage artifact"]
        self.assertLess(
            steps.index(baseline_upload),
            steps.index(reconfigure),
        )
        self.assertEqual(
            reconfigure["if"],
            "${{ inputs.compare_grouped_ctest }}",
        )
        self.assertEqual(
            " ".join(reconfigure["run"].replace("\\\n", " ").split()),
            "cmake --preset ci-coverage-cpu "
            "-B build/ci-coverage-cpu "
            "-DINTRINSIC_GROUP_PURE_CTEST=ON",
        )
        self.assertNotIn("cmake --build", reconfigure["run"])
        self.assertEqual(
            collect["if"],
            "${{ inputs.compare_grouped_ctest }}",
        )
        self.assertIn(
            "--output build/ci-coverage-cpu/coverage-grouped",
            collect["run"],
        )
        self.assertEqual(
            compare["if"],
            "${{ inputs.compare_grouped_ctest }}",
        )
        self.assertIn(
            "--baseline build/ci-coverage-cpu/coverage/coverage.json",
            compare["run"],
        )
        self.assertIn(
            "--candidate build/ci-coverage-cpu/coverage-grouped/coverage.json",
            compare["run"],
        )
        self.assertIn("--test-only-refactor", compare["run"])
        self.assertIn("--require-exact", compare["run"])
        self.assertIn(
            "| tee build/ci-coverage-cpu/coverage-grouped/exact-comparison.txt",
            compare["run"],
        )
        self.assertEqual(
            upload["if"],
            "${{ always() && inputs.compare_grouped_ctest }}",
        )
        self.assertEqual(
            upload["with"]["name"],
            "cpu-source-coverage-grouped-ctest",
        )
        self.assertEqual(
            upload["with"]["path"],
            "build/ci-coverage-cpu/coverage-grouped/",
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
            pr_fast.count('--github-output "$GITHUB_OUTPUT"'),
            5,
        )
        self.assertEqual(
            pr_fast.count('--step-summary "$GITHUB_STEP_SUMMARY"'),
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

        linux_payload, linux = _load_workflow("ci-linux-clang.yml")
        linux_steps = {
            step["name"]: step
            for step in linux_payload["jobs"]["ci-linux-clang"]["steps"]
        }
        configure = " ".join(
            linux_steps["Configure (ci preset)"]["run"].replace("\\", "").split()
        )
        self.assertIn(
            "-- cmake --preset ci --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON",
            configure,
        )
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
        self.assertIn(
            "-- cmake --build --preset ci --target IntrinsicCpuTests",
            linux,
        )
        self.assertNotIn("-- cmake --build --preset ci\n", linux)
        cpu_test = " ".join(
            linux_steps["Run full CPU test suite"]["run"].replace("\\", "").split()
        )
        self.assertIn("--parallel 4", cpu_test)
        self.assertNotIn("nproc", cpu_test)
        self.assertIn(
            "--output-junit reports/cpu.junit.xml",
            cpu_test,
        )
        results = linux_steps["Upload CPU test results"]
        self.assertEqual(results["if"], "always()")
        self.assertEqual(
            results["with"]["name"],
            "ci-cpu-test-results-ci-linux-clang",
        )
        self.assertIn(
            "build/ci/reports/cpu.junit.xml",
            results["with"]["path"],
        )
        self.assertIn(
            "build/ci/reports/grouped-ctest/gtest/",
            results["with"]["path"],
        )
        self.assertEqual(results["with"]["if-no-files-found"], "warn")

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

    def test_compile_hotspot_gates_follow_cpu_correctness(self) -> None:
        for workflow, job_name, test_step, hotspot_step in (
            (
                "ci-linux-clang.yml",
                "ci-linux-clang",
                "Run full CPU test suite",
                "Compile hotspot benchmark gate",
            ),
            (
                "nightly-deep.yml",
                "nightly-cpu-deep",
                "Run scheduled CPU slow correctness cohort",
                "Compile hotspot report",
            ),
        ):
            with self.subTest(workflow=workflow):
                payload, _ = _load_workflow(workflow)
                steps = payload["jobs"][job_name]["steps"]
                named_steps = {step["name"]: step for step in steps}
                self.assertLess(
                    steps.index(named_steps[test_step]),
                    steps.index(named_steps[hotspot_step]),
                )

    def test_manual_test_timing_profile_is_isolated_and_five_sample(self) -> None:
        payload, _ = _load_workflow("ci-linux-clang.yml")
        triggers = payload.get("on", payload.get(True, {}))
        timing_input = triggers["workflow_dispatch"]["inputs"]["collect_test_timing"]
        self.assertEqual(
            timing_input,
            {
                "description": (
                    "Collect five pr-fast, CPU, and CPU-slow timing samples "
                    "instead of gates"
                ),
                "required": False,
                "default": False,
                "type": "boolean",
            },
        )
        grouped_input = triggers["workflow_dispatch"]["inputs"][
            "collect_grouped_ctest_evidence"
        ]
        self.assertEqual(
            grouped_input,
            {
                "description": (
                    "Collect matched individual/grouped CPU timing and parity evidence"
                ),
                "required": False,
                "default": False,
                "type": "boolean",
            },
        )

        jobs = payload["jobs"]
        conflict = jobs["reject-conflicting-evidence-inputs"]
        self.assertEqual(
            " ".join(conflict["if"].split()),
            (
                "github.event_name == 'workflow_dispatch' && "
                "inputs.collect_test_timing && "
                "inputs.collect_grouped_ctest_evidence"
            ),
        )
        self.assertEqual(conflict["runs-on"], "ubuntu-24.04")
        reject = conflict["steps"][0]
        self.assertEqual(reject["name"], "Reject conflicting evidence modes")
        self.assertIn("mutually exclusive", reject["run"])
        self.assertIn("exit 1", reject["run"])

        full_job = jobs["ci-linux-clang"]
        self.assertEqual(
            " ".join(full_job["if"].split()),
            (
                "github.event_name != 'workflow_dispatch' || "
                "(!inputs.collect_test_timing && "
                "!inputs.collect_grouped_ctest_evidence)"
            ),
        )

        profile = jobs["test-timing-profile"]
        self.assertEqual(
            " ".join(profile["if"].split()),
            (
                "github.event_name == 'workflow_dispatch' && "
                "inputs.collect_test_timing && "
                "!inputs.collect_grouped_ctest_evidence"
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
                {
                    "name": "cpu-slow",
                    "preset": "ci",
                    "build_dir": "build/ci",
                    "aggregate": "IntrinsicCpuSlowTests",
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
                self.assertIn(
                    "!inputs.collect_grouped_ctest_evidence",
                    condition,
                )

    def test_grouped_ctest_evidence_is_single_runner_and_minimal(self) -> None:
        payload, _ = _load_workflow("ci-linux-clang.yml")
        job = payload["jobs"]["grouped-ctest-evidence"]
        self.assertEqual(
            " ".join(job["if"].split()),
            (
                "github.event_name == 'workflow_dispatch' && "
                "inputs.collect_grouped_ctest_evidence && "
                "!inputs.collect_test_timing"
            ),
        )
        self.assertEqual(job["runs-on"], "ubuntu-24.04")
        self.assertNotIn("strategy", job)
        steps = {step["name"]: step for step in job["steps"]}

        configure = steps["Configure individual and grouped plans"]["run"]
        self.assertEqual(configure.count("cmake --preset ci"), 2)
        self.assertIn("-B build/ci-grouped", configure)
        self.assertIn("-DINTRINSIC_GROUP_PURE_CTEST=ON", configure)

        build = steps["Build shared CPU plan"]["run"]
        self.assertEqual(build.count("--target IntrinsicCpuTests"), 1)
        self.assertNotIn("build/ci-grouped", build)

        materialize = steps["Materialize grouped binary view"]["run"]
        self.assertIn(
            "find build/ci-grouped/bin -type f -print -quit",
            materialize,
        )
        self.assertIn(
            "cp -al build/ci/bin/. build/ci-grouped/bin/",
            materialize,
        )
        self.assertIn(
            "done < build/ci/test-inventories/IntrinsicCpuTests.txt",
            materialize,
        )
        self.assertIn('[[ ! "$individual" -ef "$grouped" ]]', materialize)
        self.assertIn('[[ "$shared_count" -eq 0 ]]', materialize)
        job_runs = "\n".join(str(step.get("run", "")) for step in job["steps"])
        self.assertEqual(job_runs.count("cmake --build"), 1)
        self.assertNotIn("cmake --build build/ci-grouped", job_runs)
        self.assertLess(
            list(steps).index("Build shared CPU plan"),
            list(steps).index("Materialize grouped binary view"),
        )
        self.assertLess(
            list(steps).index("Materialize grouped binary view"),
            list(steps).index("Reconcile and compare registrations"),
        )
        reconcile = steps["Reconcile and compare registrations"]["run"]
        self.assertEqual(
            reconcile.count("Test.TestGateRouting.py"),
            2,
        )
        reconcile_one_line = " ".join(reconcile.replace("\\", "").split())
        self.assertIn(
            "Test.GroupedCTestParity.py registration",
            reconcile_one_line,
        )

        collect = steps["Collect five matched pairs at j1, j2, and j4"]["run"]
        self.assertIn("for jobs in 1 2 4", collect)
        self.assertIn("for pair_index in 1 2 3 4 5", collect)
        self.assertIn("pair_index % 2 == 0", collect)
        self.assertEqual(
            collect.count("tools/ci/collect_test_timing.py"),
            1,
        )
        self.assertIn("--cohort cpu", collect)
        self.assertIn("--samples 1", collect)
        self.assertIn('--parallel "$jobs"', collect)
        collect_one_line = " ".join(collect.replace("\\", "").split())
        self.assertIn(
            "Test.GroupedCTestParity.py execution",
            collect_one_line,
        )
        for path in (
            '"$evidence_root/j$jobs/pair-$pair/individual/samples/sample-01.junit.xml"',
            '"$evidence_root/j$jobs/pair-$pair/grouped/samples/sample-01.junit.xml"',
            '"$evidence_root/j$jobs/pair-$pair/grouped/samples/sample-01.gtest"',
            '"$evidence_root/j$jobs/pair-$pair/parity.json"',
        ):
            self.assertIn(path, collect)
        for excluded in ("pr-fast", "cpu-slow", "gpu", "vulkan"):
            with self.subTest(excluded=excluded):
                self.assertNotIn(excluded, collect)

        upload = steps["Upload grouped CTest evidence"]
        self.assertEqual(upload["if"], "always()")
        self.assertEqual(
            upload["with"]["name"],
            "ci008-grouped-ctest-evidence",
        )

    def test_nightly_partitions_fast_slow_slo_and_benchmark_owners(self) -> None:
        payload, _ = _load_workflow("nightly-deep.yml")
        triggers = payload.get("on", payload.get(True, {}))
        slow_evidence = triggers["workflow_dispatch"]["inputs"]["slow_evidence_only"]
        self.assertEqual(slow_evidence["type"], "boolean")
        self.assertFalse(slow_evidence["default"])
        steps = payload["jobs"]["nightly-cpu-deep"]["steps"]
        named_steps = {step["name"]: step for step in steps}
        build_partitions = " ".join(
            named_steps["Build nightly CPU target partitions"]["run"].split()
        )
        self.assertIn(
            "targets=(IntrinsicCpuTests IntrinsicCpuSlowTests IntrinsicBenchmarkTests)",
            build_partitions,
        )
        self.assertIn(
            "targets=(IntrinsicCpuSlowTests)",
            build_partitions,
        )
        self.assertIn(
            'cmake --build --preset ci --target "${targets[@]}"',
            build_partitions,
        )
        self.assertNotIn("Build full CPU targets", named_steps)
        self.assertNotIn("Build scheduled CPU slow cohort", named_steps)
        reconcile_slow = named_steps["Reconcile scheduled CPU slow cohort"]["run"]
        self.assertIn("--aggregate IntrinsicCpuSlowTests", reconcile_slow)

        fast = named_steps["Run full CPU test suite"]["run"]
        slow = named_steps["Run scheduled CPU slow correctness cohort"]["run"]
        slo_step = named_steps["Run SLO/performance diagnostic (CI-009)"]
        slo = slo_step["run"]
        benchmark_step = named_steps["Run benchmark smoke and selected deep benchmarks"]
        full_partition_condition = (
            "${{ github.event_name != 'workflow_dispatch' || "
            "!inputs.slow_evidence_only }}"
        )
        for step_name in (
            "Run full CPU test suite",
            "Run SLO/performance diagnostic (CI-009)",
            "Compile hotspot report",
            "Module fanout report",
            "Build benchmark smoke target",
            "Run benchmark smoke and selected deep benchmarks",
        ):
            with self.subTest(step=step_name):
                self.assertEqual(
                    named_steps[step_name]["if"],
                    full_partition_condition,
                )
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
            "--inventory build/ci/test-inventories/IntrinsicCpuSlowTests.txt",
            slow,
        )
        self.assertIn(
            "--output-junit reports/cpu-slow.junit.xml",
            slow,
        )
        self.assertIn("--no-tests=error", slow)
        self.assertFalse(
            named_steps["Run scheduled CPU slow correctness cohort"].get(
                "continue-on-error",
                False,
            )
        )
        self.assertIn('-L "^slo$"', slo)
        self.assertIn(
            "--output-junit reports/architecture-slo.junit.xml",
            slo,
        )
        self.assertTrue(slo_step["continue-on-error"])
        self.assertFalse(benchmark_step.get("continue-on-error", False))
        self.assertLess(
            steps.index(named_steps["Run full CPU test suite"]),
            steps.index(named_steps["Run scheduled CPU slow correctness cohort"]),
        )
        self.assertLess(
            steps.index(named_steps["Run scheduled CPU slow correctness cohort"]),
            steps.index(slo_step),
        )
        upload_paths = named_steps["Upload nightly reports"]["with"]["path"]
        self.assertEqual(
            named_steps["Upload nightly reports"]["if"],
            "always()",
        )
        self.assertEqual(
            named_steps["Upload nightly reports"]["with"]["if-no-files-found"],
            "error",
        )
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
                    SANITIZER_GROUP if name == "ci-sanitizers.yml" else EXPECTED_GROUP
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
