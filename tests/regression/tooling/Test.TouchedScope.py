#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

REPO_ROOT = Path(__file__).resolve().parents[3]
SCRIPT_PATH = REPO_ROOT / "tools" / "ci" / "touched_scope.py"

spec = importlib.util.spec_from_file_location("touched_scope", SCRIPT_PATH)
assert spec is not None and spec.loader is not None
touched_scope = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = touched_scope
spec.loader.exec_module(touched_scope)


def make_args(build_dir: str) -> SimpleNamespace:
    return SimpleNamespace(
        root=".",
        build_dir=build_dir,
        preset="ci",
        preset_build_dir="build/ci",
        timeout=60,
        jobs=4,
    )


class TouchedScopeTests(unittest.TestCase):
    def test_geometry_source_selects_geometry_build_and_label(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            plan = touched_scope.analyze_changed_files(["src/geometry/Geometry.PointCloud.IO.cpp"])
            commands = touched_scope.commands_for_plan(plan, make_args(tmp))
            command_text = "\n".join(command.shell_text() for command in commands)

        self.assertFalse(plan.broad_cpu_gate)
        self.assertIn("IntrinsicGeometryTests", command_text)
        self.assertIn("-L geometry", command_text)
        self.assertIn("check_layering.py", command_text)

    def test_docs_and_tasks_select_structural_checks_without_cpp_build(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            plan = touched_scope.analyze_changed_files([
                "docs/build-troubleshooting.md",
                "tasks/active/CI-002-touched-scope-verification-helper.md",
            ])
            commands = touched_scope.commands_for_plan(plan, make_args(tmp))
            command_text = "\n".join(command.shell_text() for command in commands)

        self.assertFalse(plan.broad_cpu_gate)
        self.assertNotIn("cmake --build", command_text)
        self.assertIn("check_doc_links.py", command_text)
        self.assertIn("check_task_policy.py", command_text)

    def test_cmake_changes_select_broad_cpu_gate(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            plan = touched_scope.analyze_changed_files(["cmake/IntrinsicModule.cmake"])
            commands = touched_scope.commands_for_plan(plan, make_args(tmp))
            command_text = "\n".join(command.shell_text() for command in commands)

        self.assertTrue(plan.broad_cpu_gate)
        self.assertIn("cmake --preset ci", command_text)
        self.assertIn("--target IntrinsicTests", command_text)
        self.assertIn("ctest --test-dir build/ci", command_text)

    def test_undeclared_optional_target_is_omitted_from_narrow_build(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            build_dir = Path(tmp)
            (build_dir / "build.ninja").write_text("build bin/IntrinsicGraphicsRhiCpuUnitTests: CXX_EXECUTABLE_LINKER\n")
            plan = touched_scope.analyze_changed_files(["src/graphics/rhi/RHI.Device.cppm"])
            commands = touched_scope.commands_for_plan(plan, make_args(tmp))
            command_text = "\n".join(command.shell_text() for command in commands)

        self.assertFalse(plan.broad_cpu_gate)
        self.assertIn("IntrinsicGraphicsRhiCpuUnitTests", command_text)
        self.assertNotIn("IntrinsicGraphicsContractTests", command_text)
        self.assertIn("-L graphics", command_text)


if __name__ == "__main__":
    unittest.main()
