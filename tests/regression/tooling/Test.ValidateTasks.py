#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
VALIDATOR = REPO_ROOT / "tools" / "agents" / "validate_tasks.py"


def run_validator(root: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(VALIDATOR), "--root", str(root), "--strict"],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


class ValidateTasksTests(unittest.TestCase):
    def test_canonical_repository_invocation_discovers_tasks(self) -> None:
        result = run_validator(REPO_ROOT / "tasks")

        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertRegex(result.stdout, r"Validated [1-9][0-9]* task file\(s\)")
        self.assertNotIn("No task markdown files found", result.stdout)

    def test_strict_mode_rejects_empty_task_root(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)

            result = run_validator(root)

        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("No task markdown files found", result.stdout)
        self.assertIn(str(root / "active"), result.stdout)
        self.assertIn(str(root / "backlog"), result.stdout)
        self.assertIn(str(root / "done"), result.stdout)


if __name__ == "__main__":
    unittest.main()
