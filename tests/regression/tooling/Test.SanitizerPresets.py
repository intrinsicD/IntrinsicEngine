#!/usr/bin/env python3
from __future__ import annotations

import json
import unittest
from pathlib import Path

import yaml


REPO_ROOT = Path(__file__).resolve().parents[3]
PRESETS_PATH = REPO_ROOT / "CMakePresets.json"
ROOT_CMAKE = REPO_ROOT / "CMakeLists.txt"
TEST_CMAKE = REPO_ROOT / "tests" / "CMakeLists.txt"
WORKFLOW_ROOT = REPO_ROOT / ".github" / "workflows"
DOCS_WORKFLOW = WORKFLOW_ROOT / "ci-docs.yml"
SANITIZER_WORKFLOW = WORKFLOW_ROOT / "ci-sanitizers.yml"
NIGHTLY_WORKFLOW = WORKFLOW_ROOT / "nightly-deep.yml"

CPU_EXCLUSION = 'gpu|vulkan|slow|flaky-quarantine'
SANITIZER_MATRIX = [
    {"name": "asan", "preset": "ci-asan", "build_dir": "build/ci-asan"},
    {"name": "ubsan", "preset": "ci-ubsan", "build_dir": "build/ci-ubsan"},
]
LINUX_WINDOWING_PACKAGES = {
    "libx11-dev",
    "libxcursor-dev",
    "libxrandr-dev",
    "libxinerama-dev",
    "libxi-dev",
    "libxext-dev",
    "libxfixes-dev",
    "libwayland-dev",
    "libxkbcommon-dev",
    "libgl1-mesa-dev",
    "libvulkan-dev",
    "vulkan-tools",
    "spirv-tools",
}


def _load_workflow(name: str) -> tuple[dict[str, object], str]:
    path = WORKFLOW_ROOT / name
    text = path.read_text(encoding="utf-8")
    payload = yaml.safe_load(text)
    if not isinstance(payload, dict):
        raise AssertionError(f"workflow root is not a mapping: {path}")
    return payload, text


def _named_steps(job: object) -> dict[str, dict[str, object]]:
    if not isinstance(job, dict) or not isinstance(job.get("steps"), list):
        raise AssertionError("workflow job has no steps")
    steps: dict[str, dict[str, object]] = {}
    for step in job["steps"]:
        if not isinstance(step, dict):
            raise AssertionError("workflow contains a malformed step")
        name = step.get("name")
        if isinstance(name, str):
            steps[name] = step
    return steps


def _one_line(command: object) -> str:
    if not isinstance(command, str):
        raise AssertionError("workflow step has no command")
    return " ".join(command.split())


class SanitizerPresetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        payload = json.loads(PRESETS_PATH.read_text(encoding="utf-8"))
        cls.configure_presets = {
            preset["name"]: preset for preset in payload["configurePresets"]
        }
        cls.build_presets = {
            preset["name"]: preset for preset in payload["buildPresets"]
        }

    def _resolved_configure_preset(self, name: str) -> dict[str, object]:
        resolving: set[str] = set()

        def resolve(current_name: str) -> dict[str, object]:
            if current_name in resolving:
                raise AssertionError(f"preset inheritance cycle at {current_name}")
            resolving.add(current_name)
            current = self.configure_presets[current_name]
            merged: dict[str, object] = {}
            cache: dict[str, object] = {}
            inherited = current.get("inherits", [])
            if isinstance(inherited, str):
                inherited = [inherited]
            for parent_name in inherited:
                parent = resolve(parent_name)
                merged.update(
                    {
                        key: value
                        for key, value in parent.items()
                        if key != "cacheVariables"
                    }
                )
                cache.update(parent.get("cacheVariables", {}))
            merged.update(
                {
                    key: value
                    for key, value in current.items()
                    if key not in {"cacheVariables", "inherits"}
                }
            )
            cache.update(current.get("cacheVariables", {}))
            merged["cacheVariables"] = cache
            resolving.remove(current_name)
            return merged

        resolved = resolve(name)

        def expand(value: object) -> object:
            if not isinstance(value, str):
                return value
            return value.replace("${sourceDir}", str(REPO_ROOT)).replace(
                "${presetName}", name
            )

        resolved["binaryDir"] = expand(resolved.get("binaryDir"))
        resolved["cacheVariables"] = {
            key: expand(value)
            for key, value in resolved["cacheVariables"].items()
        }
        return resolved

    def test_required_presets_have_explicit_isolated_sanitizer_identity(self) -> None:
        expected = {
            "ci": ("OFF", "none"),
            "ci-asan": ("ON", "address"),
            "ci-ubsan": ("ON", "undefined"),
            "ci-vulkan": ("ON", "address,undefined"),
            "ci-coverage-cpu": ("OFF", "none"),
        }
        build_dirs: set[str] = set()
        install_dirs: set[str] = set()
        for name, (enabled, mode) in expected.items():
            with self.subTest(preset=name):
                preset = self._resolved_configure_preset(name)
                cache = preset["cacheVariables"]
                self.assertEqual(cache["INTRINSIC_ENABLE_SANITIZERS"], enabled)
                self.assertEqual(cache["INTRINSIC_SANITIZER_MODE"], mode)
                self.assertEqual(
                    preset["binaryDir"],
                    str(REPO_ROOT / "build" / name),
                )
                self.assertEqual(
                    cache["VCPKG_INSTALLED_DIR"],
                    str(REPO_ROOT / "external" / "vcpkg-installed" / name),
                )
                self.assertEqual(
                    self.build_presets[name]["configurePreset"],
                    name,
                )
                build_dirs.add(str(preset["binaryDir"]))
                install_dirs.add(str(cache["VCPKG_INSTALLED_DIR"]))

        self.assertEqual(len(build_dirs), len(expected))
        self.assertEqual(len(install_dirs), len(expected))

    def test_cmake_resolves_exact_capabilities_and_fail_hard_flags(self) -> None:
        root_cmake = ROOT_CMAKE.read_text(encoding="utf-8")
        test_cmake = TEST_CMAKE.read_text(encoding="utf-8")

        for mode in ("none", "address", "undefined", "address,undefined"):
            with self.subTest(mode=mode):
                self.assertIn(mode, root_cmake)
        for identity in ("none", "asan", "ubsan", "asan-ubsan"):
            with self.subTest(identity=identity):
                self.assertIn(identity, root_cmake)
        self.assertIn("Unsupported INTRINSIC_SANITIZER_MODE", root_cmake)
        self.assertIn("-fsanitize=${_intrinsic_sanitizer_mode}", root_cmake)
        self.assertIn("-fno-sanitize-recover=undefined", root_cmake)
        self.assertIn("INTRINSIC_SANITIZER_HAS_ADDRESS", test_cmake)
        self.assertNotIn(
            "AND INTRINSIC_ENABLE_SANITIZERS\n"
            "       AND TARGET IntrinsicGlfwLifecycleLsanProcess",
            test_cmake,
        )

    def test_sanitizer_workflow_uses_named_presets_and_canonical_cpu_selector(
        self,
    ) -> None:
        payload, text = _load_workflow("ci-sanitizers.yml")
        job = payload["jobs"]["sanitizer-tests"]
        matrix = job["strategy"]["matrix"]["sanitizer"]
        self.assertEqual(matrix, SANITIZER_MATRIX)
        steps = _named_steps(job)

        install = _one_line(steps["Install system dependencies"]["run"])
        for package in LINUX_WINDOWING_PACKAGES:
            with self.subTest(package=package):
                self.assertIn(package, install)

        configure = _one_line(
            steps["Configure (${{ matrix.sanitizer.name }})"]["run"]
        )
        self.assertIn(
            "-- cmake --preset ${{ matrix.sanitizer.preset }} --fresh",
            configure,
        )
        self.assertIn(
            "${{ matrix.sanitizer.build_dir }}/ci-timing/phases/configure.json",
            configure,
        )

        build = _one_line(
            steps["Build IntrinsicCpuTests (${{ matrix.sanitizer.name }})"]["run"]
        )
        self.assertIn(
            "cmake --build --preset ${{ matrix.sanitizer.preset }} "
            "--target IntrinsicCpuTests",
            build,
        )
        self.assertNotIn("IntrinsicTests", build)

        reconcile = _one_line(
            steps["Reconcile CPU test routing (${{ matrix.sanitizer.name }})"][
                "run"
            ]
        )
        self.assertIn("--aggregate IntrinsicCpuTests", reconcile)
        self.assertIn("--build-dir ${{ matrix.sanitizer.build_dir }}", reconcile)

        capture = _one_line(
            steps["Capture CPU test selection (${{ matrix.sanitizer.name }})"]["run"]
        )
        self.assertIn("tools/ci/cpu_test_selection.py capture", capture)
        self.assertIn("--build-dir ${{ matrix.sanitizer.build_dir }}", capture)
        self.assertIn("--preset ${{ matrix.sanitizer.preset }}", capture)
        self.assertIn(
            "--expected-sanitizer ${{ matrix.sanitizer.name }}",
            capture,
        )

        test = _one_line(
            steps["Run selected CPU tests (${{ matrix.sanitizer.name }})"]["run"]
        )
        self.assertIn(f'-LE "{CPU_EXCLUSION}"', test)
        self.assertIn("--no-tests=error", test)
        self.assertNotIn(" -L ", test)
        self.assertNotIn(" -j", test)
        self.assertNotIn("--parallel", test)

        timing = _one_line(
            steps["Aggregate gate timing result (${{ matrix.sanitizer.name }})"][
                "run"
            ]
        )
        self.assertIn("--preset ${{ matrix.sanitizer.preset }}", timing)
        self.assertIn("--build-dir ${{ matrix.sanitizer.build_dir }}", timing)
        self.assertIn("--sanitizer '${{ matrix.sanitizer.name }}'", timing)
        self.assertIn(
            "steps.cpu-selection.outputs.selected-test-count",
            timing,
        )

        for forbidden in (
            "-fsanitize=",
            "CMAKE_CXX_FLAGS",
            "CMAKE_EXE_LINKER_FLAGS",
            "CMAKE_SHARED_LINKER_FLAGS",
            "-DINTRINSIC_ENABLE_SANITIZERS",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, text)

    def test_full_cpu_workflow_requires_real_three_variant_parity(self) -> None:
        payload, _ = _load_workflow("ci-linux-clang.yml")
        triggers = payload.get("on", payload.get(True, {}))
        manual = triggers["workflow_dispatch"]["inputs"]["run_sanitizers"]
        self.assertEqual(
            manual,
            {
                "description": "Run the ASan/UBSan matrix and CPU selection parity",
                "required": False,
                "default": True,
                "type": "boolean",
            },
        )
        jobs = payload["jobs"]
        sanitizer_job = jobs["sanitizer-tests"]
        self.assertEqual(
            sanitizer_job["uses"],
            "./.github/workflows/ci-sanitizers.yml",
        )
        expected_condition = (
            "github.event_name == 'pull_request' || "
            "(github.event_name == 'workflow_dispatch' && "
            "inputs.run_sanitizers && !inputs.collect_test_timing && "
            "!inputs.collect_grouped_ctest_evidence)"
        )
        self.assertEqual(sanitizer_job["if"], expected_condition)

        parity_job = jobs["cpu-test-selection-parity"]
        self.assertEqual(parity_job["if"], expected_condition)
        self.assertEqual(
            parity_job["needs"],
            ["ci-linux-clang", "sanitizer-tests"],
        )
        steps = _named_steps(parity_job)
        compare = _one_line(steps["Enforce CPU test-selection parity"]["run"])
        self.assertIn("tools/ci/cpu_test_selection.py compare", compare)
        for sanitizer in ("none", "asan", "ubsan"):
            with self.subTest(sanitizer=sanitizer):
                self.assertIn(f"--require-sanitizer {sanitizer}", compare)
                self.assertIn(
                    f"build/cpu-test-selection/{sanitizer}/selection.json",
                    compare,
                )
        self.assertIn(
            "--output build/cpu-test-selection/parity.json",
            compare,
        )
        upload = steps["Upload CPU test-selection parity"]
        self.assertEqual(
            upload["with"]["name"],
            "ci-cpu-test-selection-parity",
        )
        self.assertEqual(
            upload["with"]["if-no-files-found"],
            "error",
        )

    def test_full_cpu_workflow_captures_unsanitized_selection(self) -> None:
        payload, _ = _load_workflow("ci-linux-clang.yml")
        steps = _named_steps(payload["jobs"]["ci-linux-clang"])
        capture = _one_line(steps["Capture CPU test selection"]["run"])
        self.assertIn("tools/ci/cpu_test_selection.py capture", capture)
        self.assertIn("--build-dir build/ci", capture)
        self.assertIn("--preset ci", capture)
        self.assertIn("--expected-sanitizer none", capture)

        timing = _one_line(steps["Aggregate gate timing result"]["run"])
        self.assertIn("--preset ci", timing)
        self.assertIn("--build-dir build/ci", timing)
        self.assertIn("--sanitizer none", timing)
        self.assertIn(
            "steps.cpu-selection.outputs.selected-test-count",
            timing,
        )
        self.assertNotIn("Run FrameGraph SLO gate", steps)
        self.assertNotIn("IntrinsicBenchmarkTests", timing)

    def test_live_workflows_report_explicit_sanitizer_identity(self) -> None:
        expected = {
            "ci-bench-smoke.yml": ("benchmark-smoke", "none"),
            "ci-source-coverage.yml": ("cpu-source-coverage", "none"),
            "ci-vulkan.yml": ("ci-vulkan", "asan-ubsan"),
        }
        for workflow_name, (job_name, sanitizer) in expected.items():
            with self.subTest(workflow=workflow_name):
                payload, _ = _load_workflow(workflow_name)
                steps = _named_steps(payload["jobs"][job_name])
                timing = _one_line(steps["Aggregate gate timing result"]["run"])
                self.assertIn(f"--sanitizer {sanitizer}", timing)

    def test_nightly_never_reconfigures_sanitizers_or_performance_tree(
        self,
    ) -> None:
        payload, text = _load_workflow("nightly-deep.yml")
        for forbidden in (
            "Run sanitizer-extended",
            "-fsanitize=",
            "INTRINSIC_ENABLE_SANITIZERS",
            "CMAKE_CXX_FLAGS",
            "CMAKE_EXE_LINKER_FLAGS",
            "CMAKE_SHARED_LINKER_FLAGS",
            "ci-asan",
            "ci-ubsan",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, text)

        cpu_steps = _named_steps(payload["jobs"]["nightly-cpu-deep"])
        self.assertIn(
            "cmake --preset ci --fresh",
            _one_line(cpu_steps["Configure (ci preset)"]["run"]),
        )
        self.assertIn(
            "--build-dir build/ci",
            _one_line(cpu_steps["Run SLO/performance diagnostic (CI-009)"]["run"]),
        )
        fast_cpu = _one_line(cpu_steps["Run full CPU test suite"]["run"])
        self.assertIn(f'-LE "{CPU_EXCLUSION}"', fast_cpu)
        slo_step = cpu_steps["Run SLO/performance diagnostic (CI-009)"]
        self.assertTrue(slo_step["continue-on-error"])
        slo = _one_line(slo_step["run"])
        self.assertIn('-L "^slo$"', slo)
        self.assertIn(
            '-LE "^(gpu|vulkan|flaky-quarantine)$"',
            slo,
        )
        self.assertIn("--no-tests=error", slo)
        self.assertIn(
            "--output-junit reports/architecture-slo.junit.xml",
            slo,
        )
        self.assertNotIn('benchmark|slo', slo)
        self.assertIn(
            "build/ci/bin/IntrinsicBenchmarkSmoke",
            _one_line(
                cpu_steps["Run benchmark smoke and selected deep benchmarks"]["run"]
            ),
        )
        nightly_upload = cpu_steps["Upload nightly reports"]["with"]["path"]
        self.assertIn("build/ci/reports/architecture-slo.junit.xml", nightly_upload)

        gpu_steps = _named_steps(payload["jobs"]["nightly-gpu-optional"])
        self.assertIn(
            "cmake --preset ci-vulkan --fresh",
            _one_line(gpu_steps["Configure (ci-vulkan preset)"]["run"]),
        )
        self.assertIn(
            "cmake --build --preset ci-vulkan "
            "--target ExtrinsicSandbox IntrinsicTests",
            _one_line(gpu_steps["Build IntrinsicTests for GPU-labeled coverage"]["run"]),
        )
        self.assertIn(
            "--test-dir build/ci-vulkan",
            _one_line(gpu_steps["Run optional GPU tests (self-hosted runner)"]["run"]),
        )
        gpu_test = _one_line(
            gpu_steps["Run optional GPU tests (self-hosted runner)"]["run"]
        )
        self.assertIn('-L "gpu" -L "vulkan"', gpu_test)
        self.assertNotIn('gpu|vulkan', gpu_test)

    def test_static_regression_is_required_by_docs_ci(self) -> None:
        payload, text = _load_workflow("ci-docs.yml")
        self.assertEqual(
            text.count("tests/regression/tooling/Test.SanitizerPresets.py"),
            1,
        )
        self.assertEqual(
            text.count("tests/regression/tooling/Test.CpuTestSelection.py"),
            1,
        )
        steps = _named_steps(payload["jobs"]["docs-validation"])
        matching = [
            step
            for step in steps.values()
            if "Test.SanitizerPresets.py" in str(step.get("run", ""))
        ]
        self.assertEqual(len(matching), 1)
        self.assertFalse(matching[0].get("continue-on-error", False))


if __name__ == "__main__":
    unittest.main()
