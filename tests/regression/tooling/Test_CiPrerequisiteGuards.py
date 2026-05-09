#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import io
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from types import SimpleNamespace

REPO_ROOT = Path(__file__).resolve().parents[3]
SCRIPT_PATH = REPO_ROOT / "tools" / "ci" / "check_prerequisites.py"

spec = importlib.util.spec_from_file_location("check_prerequisites", SCRIPT_PATH)
assert spec is not None and spec.loader is not None
check_prerequisites = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check_prerequisites)


class CiPrerequisiteGuardTests(unittest.TestCase):
    def test_missing_declared_test_binary_reports_blocked(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            build_dir = Path(tmp)
            (build_dir / "build.ninja").write_text("build bin/IntrinsicCoreTests: CXX_EXECUTABLE_LINKER\n")
            args = SimpleNamespace(
                build_dir=build_dir,
                skip_undeclared=True,
                targets=["IntrinsicCoreTests"],
            )

            stdout = io.StringIO()
            with redirect_stdout(stdout):
                result = check_prerequisites.check_test_binaries(args)

            self.assertEqual(result, check_prerequisites.BLOCKED_EXIT_CODE)
            output = stdout.getvalue()
            self.assertIn("BLOCKED", output)
            self.assertIn("IntrinsicCoreTests", output)
            self.assertIn("producer target", output)

    def test_undeclared_optional_test_binary_is_skipped(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            build_dir = Path(tmp)
            (build_dir / "build.ninja").write_text("# no optional target in this configuration\n")
            args = SimpleNamespace(
                build_dir=build_dir,
                skip_undeclared=True,
                targets=["IntrinsicGraphicsVulkanContractTests"],
            )

            stdout = io.StringIO()
            with redirect_stdout(stdout):
                result = check_prerequisites.check_test_binaries(args)

            self.assertEqual(result, 0)
            self.assertIn("skipped undeclared optional", stdout.getvalue())

    def test_missing_path_reports_producer_target(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            missing_path = Path(tmp) / "benchmark"
            args = SimpleNamespace(
                path=missing_path,
                kind="dir",
                producer_target="IntrinsicBenchmarks",
                prior_log="build.log",
            )

            stdout = io.StringIO()
            with redirect_stdout(stdout):
                result = check_prerequisites.check_path(args)

            self.assertEqual(result, check_prerequisites.BLOCKED_EXIT_CODE)
            output = stdout.getvalue()
            self.assertIn("BLOCKED", output)
            self.assertIn("IntrinsicBenchmarks", output)
            self.assertIn("build.log", output)


if __name__ == "__main__":
    unittest.main()

