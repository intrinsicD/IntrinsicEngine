#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
VALIDATOR = REPO_ROOT / "tools" / "benchmark" / "validate_benchmark_results.py"


class BenchmarkResultValidatorTests(unittest.TestCase):
    def _run_validator(self, root: Path) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(VALIDATOR), "--root", str(root), "--strict"],
            cwd=REPO_ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def test_missing_result_root_is_reported_as_blocked_prerequisite(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            missing_root = Path(tmp) / "missing-benchmark-root"
            result = self._run_validator(missing_root)

        self.assertEqual(result.returncode, 3)
        self.assertIn("BLOCKED", result.stdout)
        self.assertIn("IntrinsicBenchmarks", result.stdout)
        self.assertEqual(result.stderr, "")

    def test_malformed_result_json_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "result.json").write_text("{", encoding="utf-8")
            result = self._run_validator(root)

        self.assertEqual(result.returncode, 1)
        self.assertIn("Benchmark result JSON validation FAILED", result.stdout)
        self.assertIn("invalid JSON", result.stdout)
        self.assertEqual(result.stderr, "")

    def test_schema_invalid_result_fails_in_strict_mode(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "result.json").write_text("{}\n", encoding="utf-8")
            result = self._run_validator(root)

        self.assertEqual(result.returncode, 1)
        self.assertIn("Benchmark result JSON validation FAILED", result.stdout)
        self.assertIn("missing required fields", result.stdout)
        self.assertIn("field 'metrics' must be an object", result.stdout)
        self.assertEqual(result.stderr, "")


if __name__ == "__main__":
    unittest.main()
