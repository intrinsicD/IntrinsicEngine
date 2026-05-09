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
    def test_missing_result_root_is_reported_as_blocked_prerequisite(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            missing_root = Path(tmp) / "missing-benchmark-root"
            result = subprocess.run(
                [sys.executable, str(VALIDATOR), "--root", str(missing_root), "--strict"],
                cwd=REPO_ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )

        self.assertEqual(result.returncode, 3)
        self.assertIn("BLOCKED", result.stdout)
        self.assertIn("IntrinsicBenchmarks", result.stdout)
        self.assertEqual(result.stderr, "")


if __name__ == "__main__":
    unittest.main()

