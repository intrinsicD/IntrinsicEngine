#!/usr/bin/env python3
"""Regression tests for ``tools/repo/check_layering.py``.

Exercises the fixture tree under
``tests/contract/repo/layering_fixtures/`` to cover:

- module-import detection across promoted ``Extrinsic.<Layer>.*`` prefixes,
- CMake ``target_link_libraries`` detection for promoted target names,
- allowlist semantics still apply,
- ``--exclude`` skips negative fixtures when scanning the fixture tree in
  bulk.
"""

from __future__ import annotations

import subprocess
import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
CHECKER = REPO_ROOT / "tools" / "repo" / "check_layering.py"
FIXTURES = REPO_ROOT / "tests" / "contract" / "repo" / "layering_fixtures"


def run_checker(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(CHECKER), *args],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


class CheckLayeringFixtureTests(unittest.TestCase):
    def test_positive_clean_core_passes(self) -> None:
        result = run_checker(
            "--root",
            str(FIXTURES / "positive_clean_core"),
            "--strict",
        )
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("No layering violations found", result.stdout)

    def test_positive_clean_graphics_rhi_passes(self) -> None:
        result = run_checker(
            "--root",
            str(FIXTURES / "positive_clean_graphics_rhi"),
            "--strict",
        )
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("No layering violations found", result.stdout)

    def test_positive_clean_graphics_passes(self) -> None:
        result = run_checker(
            "--root",
            str(FIXTURES / "positive_clean_graphics"),
            "--strict",
        )
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("No layering violations found", result.stdout)

    def test_positive_clean_runtime_passes(self) -> None:
        result = run_checker(
            "--root",
            str(FIXTURES / "positive_clean_runtime"),
            "--strict",
        )
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("No layering violations found", result.stdout)

    def test_positive_clean_geometry_passes(self) -> None:
        result = run_checker(
            "--root",
            str(FIXTURES / "positive_clean_geometry"),
            "--strict",
        )
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("No layering violations found", result.stdout)

    def test_positive_clean_physics_passes(self) -> None:
        result = run_checker(
            "--root",
            str(FIXTURES / "positive_clean_physics"),
            "--strict",
        )
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("No layering violations found", result.stdout)

    def test_negative_rhi_imports_platform_window_fails(self) -> None:
        result = run_checker(
            "--root",
            str(FIXTURES / "negative_rhi_imports_platform_window"),
            "--strict",
        )
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("graphics_rhi cannot depend on platform", result.stdout)
        self.assertIn("Extrinsic.Platform.Window", result.stdout)
        self.assertIn("[import]", result.stdout)

    def test_negative_rhi_links_platform_fails(self) -> None:
        result = run_checker(
            "--root",
            str(FIXTURES / "negative_rhi_links_platform"),
            "--strict",
        )
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("graphics_rhi cannot depend on platform", result.stdout)
        self.assertIn("ExtrinsicPlatform", result.stdout)
        self.assertIn("[cmake_link]", result.stdout)

    def test_negative_graphics_imports_ecs_fails(self) -> None:
        result = run_checker(
            "--root",
            str(FIXTURES / "negative_graphics_imports_ecs"),
            "--strict",
        )
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("graphics cannot depend on ecs", result.stdout)
        self.assertIn("Extrinsic.ECS.Events", result.stdout)

    def test_negative_platform_imports_graphics_fails(self) -> None:
        result = run_checker(
            "--root",
            str(FIXTURES / "negative_platform_imports_graphics"),
            "--strict",
        )
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("platform cannot depend on graphics", result.stdout)
        self.assertIn("Extrinsic.Graphics.Pass.Surface", result.stdout)

    def test_negative_core_imports_geometry_fails(self) -> None:
        result = run_checker(
            "--root",
            str(FIXTURES / "negative_core_imports_geometry"),
            "--strict",
        )
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("core cannot depend on geometry", result.stdout)
        self.assertIn("Extrinsic.Geometry.HalfedgeMesh", result.stdout)

    def test_negative_physics_imports_higher_layers_fails(self) -> None:
        result = run_checker(
            "--root",
            str(FIXTURES / "negative_physics_imports_higher_layers"),
            "--strict",
        )
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("physics cannot depend on ecs", result.stdout)
        self.assertIn("physics cannot depend on runtime", result.stdout)
        self.assertIn("physics cannot depend on graphics", result.stdout)
        self.assertIn("physics cannot depend on platform", result.stdout)
        self.assertIn("physics cannot depend on app", result.stdout)
        self.assertIn("Extrinsic.Runtime.Engine", result.stdout)

    def test_bulk_fixture_scan_with_exclude_passes(self) -> None:
        result = run_checker(
            "--root",
            str(FIXTURES),
            "--strict",
            "--exclude",
            "negative_*",
        )
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("No layering violations found", result.stdout)

    def test_bulk_fixture_scan_without_exclude_fails(self) -> None:
        result = run_checker(
            "--root",
            str(FIXTURES),
            "--strict",
        )
        self.assertEqual(result.returncode, 1, result.stdout)


class CheckLayeringSrcRegressionTests(unittest.TestCase):
    """Regression tests against the real ``src/`` tree.

    These are expected-failure assertions during the WORKSHOP-001 / -002
    transition window: WORKSHOP-001 surfaces the known
    ``graphics/rhi -> Extrinsic.Platform.Window`` violation, and
    WORKSHOP-002 removes it. Once WORKSHOP-002 lands these tests must flip
    to ``returncode == 0`` — see WORKSHOP-002's verification block.
    """

    def test_src_strict_run_reports_expected_violation(self) -> None:
        result = run_checker("--root", "src", "--strict")
        if result.returncode == 0:
            self.skipTest(
                "src/ strict run is clean — WORKSHOP-002 has landed; "
                "this expected-failure assertion should be retired."
            )
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("Extrinsic.Platform.Window", result.stdout)


if __name__ == "__main__":
    unittest.main()
