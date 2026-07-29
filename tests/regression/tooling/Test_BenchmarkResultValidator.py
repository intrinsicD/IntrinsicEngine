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
VALIDATOR = REPO_ROOT / "tools" / "benchmark" / "validate_benchmark_results.py"
SEALER = REPO_ROOT / "tools" / "benchmark" / "seal_benchmark_results.py"


class BenchmarkResultValidatorTests(unittest.TestCase):
    def _run_validator(
        self, root: Path, manifests: Path | None = None
    ) -> subprocess.CompletedProcess[str]:
        command = [
            sys.executable,
            str(VALIDATOR),
            "--root",
            str(root),
            "--strict",
        ]
        if manifests is not None:
            command.extend(["--manifests-root", str(manifests)])
        return subprocess.run(
            command,
            cwd=REPO_ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def _fixture(self, root: Path) -> tuple[Path, Path, Path]:
        results = root / "results"
        manifests = root / "manifests"
        results.mkdir()
        manifests.mkdir()
        manifest = {
            "benchmark_id": "geometry.fixture.smoke",
            "method": "geometry.fixture",
            "dataset": "builtin.fixture.small",
            "params": {
                "intent": "smoke",
                "warmup_iterations": 1,
                "measured_iterations": 3,
            },
            "metrics": ["runtime_ms", "quality_error_l2"],
            "thresholds": {
                "smoke_runtime_ms_max": 10.0,
                "quality_error_l2_max": 0.0,
            },
        }
        (manifests / "fixture.yaml").write_text(
            yaml.safe_dump(manifest, sort_keys=False), encoding="utf-8"
        )
        result = results / "result.json"
        result.write_text(
            json.dumps(
                {
                    "benchmark_id": "geometry.fixture.smoke",
                    "method": "geometry.fixture",
                    "backend": "cpu_reference",
                    "dataset": "builtin.fixture.small",
                    "commit": "local-dev",
                    "metrics": {"runtime_ms": 1.0, "quality_error_l2": 0.0},
                    "diagnostics": {"warmup_iterations": 1},
                    "status": "passed",
                },
                indent=2,
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
                "--attempt-id",
                "attempt-001",
                "--replace",
            ],
            cwd=REPO_ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        self.assertEqual(sealed.returncode, 0, sealed.stdout)
        return results, manifests, result

    @staticmethod
    def _load(path: Path) -> dict:
        return json.loads(path.read_text(encoding="utf-8"))

    @staticmethod
    def _write(path: Path, data: dict) -> None:
        path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")

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
            results = root / "results"
            manifests = root / "manifests"
            results.mkdir()
            manifests.mkdir()
            (results / "result.json").write_text("{", encoding="utf-8")
            result = self._run_validator(results, manifests)
        self.assertEqual(result.returncode, 1)
        self.assertIn("invalid JSON", result.stdout)

    def test_schema_invalid_result_fails_in_strict_mode(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            results = root / "results"
            manifests = root / "manifests"
            results.mkdir()
            manifests.mkdir()
            (results / "result.json").write_text("{}\n", encoding="utf-8")
            result = self._run_validator(results, manifests)
        self.assertEqual(result.returncode, 1)
        self.assertIn("missing required fields", result.stdout)
        self.assertIn("schema_version must equal 2", result.stdout)

    def test_canonical_sealed_result_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            results, manifests, _ = self._fixture(Path(tmp))
            result = self._run_validator(results, manifests)
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_duplicate_json_key_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            results, manifests, path = self._fixture(Path(tmp))
            payload = path.read_text(encoding="utf-8")
            path.write_text(
                payload[:-2] + ',\n  "status": "passed"\n}\n',
                encoding="utf-8",
            )
            result = self._run_validator(results, manifests)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("duplicate JSON key", result.stdout)

    def test_non_standard_numeric_constants_are_rejected(self) -> None:
        for constant in ("NaN", "Infinity", "-Infinity"):
            with self.subTest(constant=constant), tempfile.TemporaryDirectory() as tmp:
                results, manifests, path = self._fixture(Path(tmp))
                data = self._load(path)
                data["metrics"]["runtime_ms"] = constant
                payload = json.dumps(data).replace(f'"{constant}"', constant)
                path.write_text(payload, encoding="utf-8")
                result = self._run_validator(results, manifests)
                self.assertEqual(result.returncode, 1, result.stdout)
                self.assertIn("non-standard JSON constant", result.stdout)

    def test_boolean_is_not_a_numeric_metric(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            results, manifests, path = self._fixture(Path(tmp))
            data = self._load(path)
            data["metrics"]["runtime_ms"] = True
            self._write(path, data)
            result = self._run_validator(results, manifests)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("numeric, not boolean", result.stdout)

    def test_nested_invalid_metric_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            results, manifests, path = self._fixture(Path(tmp))
            data = self._load(path)
            data["metrics"]["runtime_ms"] = {"median": {"value": "one"}}
            self._write(path, data)
            result = self._run_validator(results, manifests)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("unsupported type str", result.stdout)

    def test_manifest_method_and_dataset_mismatch_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            results, manifests, path = self._fixture(Path(tmp))
            data = self._load(path)
            data["method"] = "geometry.other"
            data["dataset"] = "builtin.other"
            self._write(path, data)
            result = self._run_validator(results, manifests)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("method mismatch", result.stdout)
        self.assertIn("dataset mismatch", result.stdout)

    def test_undeclared_metric_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            results, manifests, path = self._fixture(Path(tmp))
            data = self._load(path)
            data["metrics"]["secret_metric"] = 1.0
            self._write(path, data)
            result = self._run_validator(results, manifests)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("does not equal manifest metric set", result.stdout)

    def test_threshold_and_status_mismatch_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            results, manifests, path = self._fixture(Path(tmp))
            data = self._load(path)
            data["threshold_disposition"][0]["passed"] = False
            data["status"] = "failed"
            self._write(path, data)
            result = self._run_validator(results, manifests)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("threshold_disposition does not match", result.stdout)
        self.assertIn("status mismatch", result.stdout)

    def test_manifest_digest_and_resolved_params_are_bound(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            results, manifests, path = self._fixture(Path(tmp))
            data = self._load(path)
            data["manifest"]["sha256"] = "0" * 64
            data["resolved_params"]["measured_iterations"] = 99
            self._write(path, data)
            result = self._run_validator(results, manifests)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("manifest sha256 mismatch", result.stdout)
        self.assertIn("resolved_params", result.stdout)

    def test_local_dev_cannot_be_claim_eligible(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            results, manifests, path = self._fixture(Path(tmp))
            data = self._load(path)
            data["claim_eligible"] = True
            data["source"]["state"] = "clean_commit"
            self._write(path, data)
            result = self._run_validator(results, manifests)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("local-dev source", result.stdout)
        self.assertIn("40-hex commit", result.stdout)

    def test_duplicate_run_attempt_identity_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            results, manifests, path = self._fixture(Path(tmp))
            (results / "duplicate.json").write_text(
                path.read_text(encoding="utf-8"), encoding="utf-8"
            )
            result = self._run_validator(results, manifests)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("duplicate run/attempt identity", result.stdout)

    def test_repeated_stable_id_with_distinct_runs_is_allowed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            results, manifests, path = self._fixture(Path(tmp))
            data = self._load(path)
            data["run_id"] = "run-002"
            data["attempt_id"] = "attempt-002"
            self._write(results / "second.json", data)
            result = self._run_validator(results, manifests)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("1 stable benchmark ID", result.stdout)
        self.assertIn("2 append-only run/attempt", result.stdout)


if __name__ == "__main__":
    unittest.main()
