#!/usr/bin/env python3
"""Regression tests for the feature-boundary ratchet (ARCH-022 rules R1 and R2)."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
CHECKER = REPO_ROOT / "tools" / "repo" / "check_feature_boundaries.py"
BASELINE = Path("tools/repo/feature_boundaries_baseline.json")
OPERATIONS = ["kernel_density", "point_spacing", "outlier_analysis"]


def write(root: Path, rel: str, text: str) -> None:
    path = root / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def schema(operations: object) -> str:
    return json.dumps({"run_operation": {"properties": {"operation": {"enum": operations, "type": "string"}}}})


class FeatureBoundaryRatchet(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        write(self.root, "tests/data/agent_declared_tool_schemas.json", schema(OPERATIONS))
        write(self.root, "src/runtime/Editor/Runtime.EditorThing.cppm", "export module Extrinsic.Runtime.EditorThing;\n")
        write(self.root, "src/runtime/Modules/Alpha/Runtime.Alpha.cppm",
              "export module Extrinsic.Runtime.Alpha;\nimport Extrinsic.Runtime.EditorThing;\nimport Extrinsic.Core.Error;\n")
        write(self.root, "src/runtime/Modules/Alpha/Runtime.Alpha.cpp",
              "module Extrinsic.Runtime.Alpha;\nimport :Detail;\n// KernelDensity and PointSpacing are named here, inside a plugin folder.\n")
        write(self.root, "src/runtime/Modules/Beta/Runtime.Beta.cppm", "export module Extrinsic.Runtime.Beta;\n")
        # A hub: names two operations (one as PascalCase inside an identifier, one in snake_case).
        write(self.root, "src/app/Hub.cpp", "void ApplyEditorKernelDensityConfig(); // \"view.point_spacing\"\n")
        # Not hubs: one operation only; a name that is only a longer word's prefix.
        write(self.root, "src/app/One.cpp", "// outlier_analysis\n")
        write(self.root, "src/runtime/Other.cpp", "// KernelDensityish point_spacings\n")
        self.save_baseline(r1=1, alpha=(1, 0), beta=(0, 0))
        self.git("init", "-q")
        self.git("config", "user.email", "t@example.invalid")
        self.git("config", "user.name", "t")
        self.commit("base")

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def git(self, *args: str) -> str:
        return subprocess.run(["git", "-C", str(self.root), *args], check=True, capture_output=True, text=True).stdout

    def commit(self, message: str) -> str:
        self.git("add", "-A")
        self.git("commit", "-q", "--allow-empty", "-m", message)
        return self.git("rev-parse", "HEAD").strip()

    def save_baseline(self, *, r1: int, alpha: tuple[int, int] | None, beta: tuple[int, int] | None,
                      **extra: tuple[int, int]) -> None:
        folders = {}
        for name, value in (("Alpha", alpha), ("Beta", beta), *extra.items()):
            if value is not None:
                folders[name if "/" in name else f"src/runtime/Modules/{name}"] = {
                    "editor_internals": value[0], "other_plugins": value[1]}
        write(self.root, str(BASELINE), json.dumps({"schema_version": 1, "r1_files": r1, "r2": folders}, indent=2))

    def run_checker(self, *extra: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run([sys.executable, str(CHECKER), "--root", str(self.root), "--strict", *extra],
                              capture_output=True, text=True)

    def assert_fails(self, needle: str, *extra: str) -> None:
        result = self.run_checker(*extra)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        findings = [line for line in (result.stdout + result.stderr).splitlines() if "ERROR:" in line or "error:" in line]
        self.assertTrue(any(needle in line for line in findings), result.stdout + result.stderr)

    def test_clean_tree_passes(self) -> None:
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("R1 files: 1", result.stdout)

    def test_added_hub_file_fails_r1(self) -> None:
        write(self.root, "src/runtime/Editor/NewHub.cpp", "// OutlierAnalysis kernel_density\n")
        self.assert_fails("R1")

    def test_editor_import_fails_r2(self) -> None:
        write(self.root, "src/runtime/Modules/Beta/Runtime.Beta.cppm",
              "export module Extrinsic.Runtime.Beta;\nimport Extrinsic.Runtime.EditorThing;\n")
        self.assert_fails("Beta: editor_internals")

    def test_cross_plugin_import_fails_r2(self) -> None:
        write(self.root, "src/runtime/Modules/Beta/Runtime.Beta.cppm",
              "export module Extrinsic.Runtime.Beta;\nimport Extrinsic.Runtime.Alpha;\n")
        self.assert_fails("Beta: other_plugins")

    def test_new_folder_without_entry_fails(self) -> None:
        write(self.root, "src/runtime/Modules/Gamma/Runtime.Gamma.cppm", "export module Extrinsic.Runtime.Gamma;\n")
        self.assert_fails("Gamma")

    def test_spike_folder_is_not_a_plugin_but_other_plugin_folders_are(self) -> None:
        editor_import = "import Extrinsic.Runtime.EditorThing;\n"
        write(self.root, "plugins/spike/SpikePlugin.cpp", editor_import)
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        write(self.root, "plugins/other/Other.cpp", editor_import)
        self.assert_fails("plugins/other")

    def test_stale_entry_fails(self) -> None:
        import shutil
        shutil.rmtree(self.root / "src/runtime/Modules/Beta")
        self.assert_fails("stale")

    def test_fall_without_lowered_ceiling_fails_and_lowered_passes(self) -> None:
        (self.root / "src/app/Hub.cpp").unlink()
        self.assert_fails("lower")
        self.save_baseline(r1=0, alpha=(1, 0), beta=(0, 0))
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_ceiling_raised_with_regression_fails(self) -> None:
        write(self.root, "src/runtime/Editor/NewHub.cpp", "// OutlierAnalysis kernel_density\n")
        self.save_baseline(r1=2, alpha=(1, 0), beta=(0, 0))
        self.assert_fails("raised")

    def test_new_folder_entry_above_zero_fails(self) -> None:
        write(self.root, "src/runtime/Modules/Gamma/Runtime.Gamma.cppm",
              "export module Extrinsic.Runtime.Gamma;\nimport Extrinsic.Runtime.EditorThing;\n")
        self.save_baseline(r1=1, alpha=(1, 0), beta=(0, 0), Gamma=(1, 0))
        self.assert_fails("raised")

    def test_a_committed_raise_cannot_become_the_new_baseline(self) -> None:
        # A raise that landed (a failed push stays on main) or hid in an earlier commit of the
        # same push: the lowest ceiling in the baseline's history still holds.
        write(self.root, "src/runtime/Editor/NewHub.cpp", "// OutlierAnalysis kernel_density\n")
        self.save_baseline(r1=2, alpha=(1, 0), beta=(0, 0))
        self.commit("A raises the ceiling")
        write(self.root, "README.md", "unrelated\n")
        self.commit("B leaves it unchanged")
        self.assert_fails("raised")

    def test_a_lowering_on_a_merged_side_branch_still_counts(self) -> None:
        # The side branch lowers R1 to 0; the merge keeps main's tree (R1 1). Git's default
        # history simplification would follow only the first parent and miss the 0.
        self.git("checkout", "-q", "-b", "side")
        (self.root / "src/app/Hub.cpp").unlink()
        self.save_baseline(r1=0, alpha=(1, 0), beta=(0, 0))
        self.commit("side lowers")
        self.git("checkout", "-q", "-")
        write(self.root, "README.md", "main moves on\n")
        self.commit("main")
        self.git("merge", "-q", "-s", "ours", "--no-edit", "side")
        self.assert_fails("raised")

    def test_an_unreadable_baseline_version_fails(self) -> None:
        blob = self.git("rev-parse", f"HEAD:{BASELINE.as_posix()}").strip()
        (self.root / ".git" / "objects" / blob[:2] / blob[2:]).unlink()
        self.assert_fails("history")

    def test_missing_history_fails(self) -> None:
        import shutil
        shutil.rmtree(self.root / ".git")
        self.assert_fails("history")

    def test_one_folder_name_under_both_roots_is_two_plugins(self) -> None:
        write(self.root, "plugins/Alpha/Alpha.Plugin.cppm", "export module Intrinsic.Plugin.Alpha;\nimport Extrinsic.Runtime.Alpha;\n")
        self.save_baseline(r1=1, alpha=(1, 0), beta=(0, 0), **{"plugins/Alpha": (0, 0)})
        self.assert_fails("plugins/Alpha: other_plugins")

    def test_declarations_are_read_as_cpp_not_as_lines(self) -> None:
        cases = {
            "same line": "export module Extrinsic.Runtime.Beta; import Extrinsic.Runtime.EditorThing;\n",
            "comment inside": "export module Extrinsic.Runtime.Beta;\nimport /* why */ Extrinsic.Runtime.EditorThing;\n",
            "fake owner in a comment": "export module Extrinsic.Runtime.Beta;\n/* export module Extrinsic.Runtime.EditorThing; */\n"
                                       "import Extrinsic.Runtime.EditorThing;\n",
            "spaces around the dots": "export module Extrinsic.Runtime.Beta;\n"
                                      "import Extrinsic . Runtime . EditorThing;\n",
            "comment inside the name": "export module Extrinsic.Runtime.Beta;\n"
                                       "import Extrinsic.Runtime./*why*/EditorThing;\n",
            "fake owner in a continued line comment": "export module Extrinsic.Runtime.Beta;\n"
                                                      "// a comment that continues \\\n"
                                                      "export module Extrinsic.Runtime.EditorThing;\n"
                                                      "import Extrinsic.Runtime.EditorThing;\n",
            "a name split by backslash-newline": "export module Extrinsic.Runtime.Beta;\n"
                                                 "import Extrinsic.Runtime.Edi\\\ntorThing;\n",
            "fake owner in a raw string with a backslash-newline": "export module Extrinsic.Runtime.Beta;\n"
                                                                   "auto s = R\"x()x\\\n\" export module Extrinsic.Runtime.EditorThing; )x\";\n"
                                                                   "import Extrinsic.Runtime.EditorThing;\n",
            "fake owner in a raw string": "export module Extrinsic.Runtime.Beta;\n"
                                          "auto s = R\"x(export module Extrinsic.Runtime.EditorThing;)x\";\n"
                                          "import Extrinsic.Runtime.EditorThing;\n",
        }
        for name, text in cases.items():
            with self.subTest(case=name):
                write(self.root, "src/runtime/Modules/Beta/Runtime.Beta.cppm", text)
                self.assert_fails("Beta: editor_internals")
        # An import that only appears in a comment or a string is not an import.
        write(self.root, "src/runtime/Modules/Beta/Runtime.Beta.cppm",
              "export module Extrinsic.Runtime.Beta;\n// import Extrinsic.Runtime.EditorThing;\n"
              "const char* s = \"import Extrinsic.Runtime.EditorThing;\";\n")
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_a_module_declared_with_spaced_tokens_keeps_its_owner(self) -> None:
        write(self.root, "src/runtime/Editor/Runtime.EditorThing.cppm",
              "export module Extrinsic . Runtime . /* spaced */ EditorThing;\n")
        write(self.root, "src/runtime/Modules/Beta/Runtime.Beta.cppm",
              "export module Extrinsic.Runtime.Beta;\nimport Extrinsic.Runtime.EditorThing;\n")
        self.assert_fails("Beta: editor_internals")

    def test_an_editor_module_declared_in_a_header_keeps_its_owner(self) -> None:
        write(self.root, "src/runtime/Editor/Runtime.EditorHeader.hpp", "export module Extrinsic.Runtime.EditorHeader;\n")
        write(self.root, "src/runtime/Modules/Beta/Runtime.Beta.cppm",
              "export module Extrinsic.Runtime.Beta;\nimport Extrinsic.Runtime.EditorHeader;\n")
        self.assert_fails("Beta: editor_internals")

    def test_a_plugin_folder_without_sources_still_needs_an_entry(self) -> None:
        write(self.root, "plugins/New/README.md", "planned plugin\n")
        self.assert_fails("plugins/New")

    def test_empty_or_malformed_enum_fails(self) -> None:
        for broken in ([], "kernel_density", None, ["kernel_density", "kernel_density"], ["   "], ["Kernel Density"]):
            with self.subTest(broken=broken):
                write(self.root, "tests/data/agent_declared_tool_schemas.json", schema(broken))
                self.assert_fails("operation enum")
        write(self.root, "tests/data/agent_declared_tool_schemas.json", json.dumps({"other_tool": {}}))
        self.assert_fails("operation enum")

    def test_ci_docs_runs_the_ratchet(self) -> None:
        workflow = (REPO_ROOT / ".github" / "workflows" / "ci-docs.yml").read_text()
        self.assertIn("python3 tools/repo/check_feature_boundaries.py --root . --strict", workflow)
        self.assertIn("python3 tests/regression/tooling/Test.CheckFeatureBoundaries.py", workflow)


if __name__ == "__main__":
    unittest.main()
