#!/usr/bin/env python3
"""BUG-149: run_and_seal.py seals exactly the producer's output root and propagates failures."""
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parents[3]
WRAPPER = REPO_ROOT / "tools" / "benchmark" / "run_and_seal.py"

MANIFEST = {
    "benchmark_id": "geometry.fixture.smoke",
    "method": "geometry.fixture",
    "dataset": "builtin.fixture.small",
    "params": {"intent": "smoke", "warmup_iterations": 1, "measured_iterations": 3},
    "metrics": ["runtime_ms", "quality_error_l2"],
    "thresholds": {"smoke_runtime_ms_max": 10.0, "quality_error_l2_max": 0.0},
}


def raw_result(benchmark_id: str = "geometry.fixture.smoke") -> str:
    return (
        json.dumps(
            {
                "benchmark_id": benchmark_id,
                "method": "geometry.fixture",
                "backend": "cpu_reference",
                "dataset": "builtin.fixture.small",
                "commit": "local-dev",
                "metrics": {"runtime_ms": 1.0, "quality_error_l2": 0.0},
                "diagnostics": {"warmup_iterations": 1},
                "status": "passed",
            }
        )
        + "\n"
    )


class RunAndSealTests(unittest.TestCase):
    def setUp(self) -> None:
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        self.manifests = self.root / "manifests"
        self.manifests.mkdir()
        (self.manifests / "fixture.yaml").write_text(
            yaml.safe_dump(MANIFEST, sort_keys=False), encoding="utf-8"
        )
        # Sealing the parent would rewrite the sealable sibling and fail on the malformed one.
        self.parent = self.root / "parent"
        self.parent.mkdir()
        (self.parent / "sealable-sibling.json").write_text(raw_result(), encoding="utf-8")
        (self.parent / "malformed-sibling.json").write_text("{not json\n", encoding="utf-8")
        self.siblings = {p: p.read_bytes() for p in self.parent.glob("*.json")}

    def run_wrapper(self, output: Path, producer_body: str) -> subprocess.CompletedProcess[str]:
        producer = self.root / "producer.py"
        producer.write_text(
            f"#!{sys.executable}\nimport sys\nfrom pathlib import Path\n"
            f"out = Path(sys.argv[1])\n{producer_body}\n",
            encoding="utf-8",
        )
        producer.chmod(0o755)
        completed = subprocess.run(
            [
                sys.executable,
                str(WRAPPER),
                "--executable",
                str(producer),
                "--output",
                str(output),
                "--manifests-root",
                str(self.manifests),
                "--run-id",
                "bug149-test",
            ],
            cwd=self.root,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        for path, content in self.siblings.items():
            self.assertEqual(path.read_bytes(), content, f"{path.name} changed\n{completed.stdout}")
        return completed

    def schema_version(self, path: Path) -> object:
        return json.loads(path.read_text(encoding="utf-8")).get("schema_version")

    def test_existing_dotted_directory_is_sealed_exactly(self) -> None:
        output = self.parent / "method037-bench.E4nKT7"
        output.mkdir()
        completed = self.run_wrapper(output, f"(out / 'result.json').write_text({raw_result()!r})")
        self.assertEqual(completed.returncode, 0, completed.stdout)
        self.assertIn("Sealed 1 ", completed.stdout)
        self.assertEqual(self.schema_version(output / "result.json"), 2)

    def test_plain_directory_created_by_producer(self) -> None:
        output = self.parent / "run"
        completed = self.run_wrapper(
            output, f"out.mkdir()\n(out / 'result.json').write_text({raw_result()!r})"
        )
        self.assertEqual(completed.returncode, 0, completed.stdout)
        self.assertIn("Sealed 1 ", completed.stdout)
        self.assertEqual(self.schema_version(output / "result.json"), 2)

    def test_explicit_json_file_seals_its_run_directory(self) -> None:
        output = self.parent / "run" / "result.json"
        completed = self.run_wrapper(
            output,
            "out.parent.mkdir()\n"
            f"out.write_text({raw_result()!r})\n"
            f"(out.parent / 'extra.json').write_text({raw_result()!r})",
        )
        self.assertEqual(completed.returncode, 0, completed.stdout)
        self.assertIn("Sealed 2 ", completed.stdout)
        self.assertEqual(self.schema_version(output), 2)
        self.assertEqual(self.schema_version(output.parent / "extra.json"), 2)

    def test_missing_dotted_output_is_refused(self) -> None:
        completed = self.run_wrapper(self.parent / "method037-bench.missing", "pass")
        self.assertEqual(completed.returncode, 2, completed.stdout)
        self.assertNotIn("Sealed", completed.stdout)
        self.assertNotIn("sealing FAILED", completed.stdout)

    def test_missing_output_keeps_producer_failure_code(self) -> None:
        completed = self.run_wrapper(self.parent / "method037-bench.missing", "sys.exit(3)")
        self.assertEqual(completed.returncode, 3, completed.stdout)
        self.assertNotIn("Sealed", completed.stdout)

    def test_non_json_output_file_is_refused(self) -> None:
        output = self.parent / "method037-bench.X"
        completed = self.run_wrapper(output, "out.write_text('not a result directory')")
        self.assertEqual(completed.returncode, 2, completed.stdout)
        self.assertNotIn("Sealed", completed.stdout)
        self.assertNotIn("sealing FAILED", completed.stdout)

    def test_producer_failure_is_returned_after_sealing(self) -> None:
        output = self.parent / "method037-bench.E4nKT7"
        output.mkdir()
        completed = self.run_wrapper(
            output, f"(out / 'result.json').write_text({raw_result()!r})\nsys.exit(3)"
        )
        self.assertEqual(completed.returncode, 3, completed.stdout)
        self.assertIn("Sealed 1 ", completed.stdout)
        self.assertEqual(self.schema_version(output / "result.json"), 2)

    def test_sealer_failure_is_returned(self) -> None:
        output = self.parent / "method037-bench.E4nKT7"
        output.mkdir()
        completed = self.run_wrapper(
            output, f"(out / 'result.json').write_text({raw_result('geometry.unknown')!r})"
        )
        self.assertEqual(completed.returncode, 1, completed.stdout)
        self.assertIn("sealing FAILED", completed.stdout)
        self.assertIn("no manifest", completed.stdout)
        errors = [line for line in completed.stdout.splitlines() if line.startswith(" - ")]
        self.assertEqual(len(errors), 1, completed.stdout)


if __name__ == "__main__":
    unittest.main()
