#!/usr/bin/env python3
"""Regression tests for the geometry benchmark manifests introduced by GEOM-009.

Mirrors the structure of `Test_BenchmarkResultValidator.py`: invokes the
manifest validator as a subprocess against the in-repo geometry manifest
directory and asserts that strict validation succeeds and discovers the
expected smoke benchmark.
"""
from __future__ import annotations

import subprocess
import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
VALIDATOR = REPO_ROOT / "tools" / "benchmark" / "validate_benchmark_manifests.py"
GEOMETRY_MANIFEST_DIR = REPO_ROOT / "benchmarks" / "geometry" / "manifests"


class BenchmarkManifestValidatorTests(unittest.TestCase):
    def test_geometry_smoke_manifest_passes_strict_validation(self) -> None:
        self.assertTrue(
            GEOMETRY_MANIFEST_DIR.exists(),
            f"missing geometry manifest directory: {GEOMETRY_MANIFEST_DIR}",
        )

        result = subprocess.run(
            [
                sys.executable,
                str(VALIDATOR),
                "--root",
                str(GEOMETRY_MANIFEST_DIR),
                "--strict",
            ],
            cwd=REPO_ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        self.assertEqual(
            result.returncode,
            0,
            msg=(
                "validate_benchmark_manifests.py --strict failed\n"
                f"stdout:\n{result.stdout}\n"
                f"stderr:\n{result.stderr}\n"
            ),
        )

    def test_geometry_smoke_benchmark_id_is_present(self) -> None:
        # Cheap textual check that the canonical halfedge smoke manifest
        # ships the expected stable benchmark_id. The C++ runner embeds the
        # same string via Intrinsic::Bench::Geometry::kHalfedgeSmokeBenchmarkId,
        # so this guards against accidental drift between the manifest and
        # the runner output.
        manifest_path = GEOMETRY_MANIFEST_DIR / "geometry_halfedge_smoke.yaml"
        self.assertTrue(manifest_path.exists(), f"missing manifest: {manifest_path}")

        contents = manifest_path.read_text(encoding="utf-8")
        self.assertIn("benchmark_id: geometry.halfedge.smoke", contents)
        self.assertIn("- runtime_ms", contents)
        self.assertIn("- quality_error_l2", contents)


if __name__ == "__main__":
    unittest.main()
