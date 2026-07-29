#!/usr/bin/env python3
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parents[3]
SEALER = REPO_ROOT / "tools/benchmark/seal_benchmark_results.py"
COMPARATOR = REPO_ROOT / "tools/benchmark/check_perf_regression.py"


class BenchmarkComparisonTests(unittest.TestCase):
    def fixture(self, root: Path) -> tuple[Path, Path]:
        manifests = root / "manifests"
        results = root / "results"
        manifests.mkdir()
        results.mkdir()
        (manifests / "frame.yaml").write_text(
            yaml.safe_dump(
                {
                    "benchmark_id": "rendering.frame.fixture",
                    "method": "rendering.frame",
                    "dataset": "builtin.frame.fixture",
                    "params": {
                        "intent": "smoke",
                        "warmup_iterations": 1,
                        "measured_iterations": 4,
                    },
                    "metrics": [
                        "avgFrameTimeMs",
                        "p99FrameTimeMs",
                        "avgFPS",
                        "totalFrames",
                    ],
                    "thresholds": {},
                },
                sort_keys=False,
            ),
            encoding="utf-8",
        )
        result = results / "result.json"
        result.write_text(
            json.dumps(
                {
                    "benchmark_id": "rendering.frame.fixture",
                    "method": "rendering.frame",
                    "backend": "cpu_optimized",
                    "dataset": "builtin.frame.fixture",
                    "commit": "local-dev",
                    "metrics": {
                        "avgFrameTimeMs": 10.0,
                        "p99FrameTimeMs": 15.0,
                        "avgFPS": 100.0,
                        "totalFrames": 4,
                    },
                    # A textual extractor would incorrectly take this first.
                    "diagnostics": {"avgFrameTimeMs": 9999.0},
                    "status": "passed",
                }
            )
            + "\n",
            encoding="utf-8",
        )
        sealed = subprocess.run(
            [
                sys.executable,
                str(SEALER),
                "--root",
                str(results),
                "--manifests-root",
                str(manifests),
                "--run-id",
                "run-001",
                "--replace",
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        self.assertEqual(sealed.returncode, 0, sealed.stdout)
        return result, manifests

    def compare(
        self, result: Path, manifests: Path, *thresholds: str
    ) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                str(COMPARATOR),
                str(result),
                "--manifests-root",
                str(manifests),
                *thresholds,
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )

    def test_comparison_reads_canonical_metrics_not_first_textual_key(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            result, manifests = self.fixture(Path(tmp))
            compared = self.compare(result, manifests, "--avg-ms", "11")
        self.assertEqual(compared.returncode, 0, compared.stdout)
        self.assertIn("avgFrameTimeMs = 10", compared.stdout)

    def test_regression_returns_one(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            result, manifests = self.fixture(Path(tmp))
            compared = self.compare(
                result,
                manifests,
                "--p99-ms",
                "14",
                "--min-fps",
                "90",
            )
        self.assertEqual(compared.returncode, 1, compared.stdout)
        self.assertIn("REGRESSION DETECTED", compared.stdout)

    def test_invalid_canonical_result_is_not_compared(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            result, manifests = self.fixture(Path(tmp))
            payload = json.loads(result.read_text(encoding="utf-8"))
            payload["metrics"]["avgFrameTimeMs"] = True
            result.write_text(json.dumps(payload), encoding="utf-8")
            compared = self.compare(result, manifests, "--avg-ms", "11")
        self.assertEqual(compared.returncode, 2, compared.stdout)
        self.assertIn("numeric, not boolean", compared.stdout)


if __name__ == "__main__":
    unittest.main()
