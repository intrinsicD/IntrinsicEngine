#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
CHECKER = REPO_ROOT / "tools" / "repo" / "check_layering_allowlist_quality.py"


def write_allowlist(root: Path, task_id: str) -> None:
    allowlist = root / "tools" / "repo" / "layering_allowlist.yaml"
    allowlist.parent.mkdir(parents=True)
    allowlist.write_text(
        "\n".join(
            [
                "exceptions:",
                "  - from: legacy",
                "    to: graphics",
                "    file_glob: src/legacy/Interface/**",
                f"    task: {task_id}",
                "    expires: when fixture migration is retired",
                "    reason: fixture exception",
                "",
            ]
        ),
        encoding="utf-8",
    )


def write_task(root: Path, state: str, task_id: str) -> None:
    task_path = root / "tasks" / state / f"{task_id}-fixture-task.md"
    task_path.parent.mkdir(parents=True)
    task_path.write_text(f"# {task_id} — Fixture task\n", encoding="utf-8")


def run_checker(root: Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(CHECKER), "--root", str(root), *args],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


class CheckLayeringAllowlistQualityTests(unittest.TestCase):
    def test_backlog_task_owner_passes_strict_mode(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_allowlist(root, "HARDEN-001")
            write_task(root, "backlog", "HARDEN-001")

            result = run_checker(root, "--strict")

        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("No findings", result.stdout)

    def test_active_task_owner_passes_strict_mode(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_allowlist(root, "HARDEN-002")
            write_task(root, "active", "HARDEN-002")

            result = run_checker(root, "--strict")

        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("No findings", result.stdout)

    def test_done_task_owner_fails_strict_mode(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_allowlist(root, "HARDEN-003")
            write_task(root, "done", "HARDEN-003")

            result = run_checker(root, "--strict")

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("Entry #1", result.stdout)
        self.assertIn("from='legacy'", result.stdout)
        self.assertIn("to='graphics'", result.stdout)
        self.assertIn("file_glob='src/legacy/Interface/**'", result.stdout)
        self.assertIn("task owner 'HARDEN-003' is retired/not open", result.stdout)

    def test_missing_task_owner_fails_strict_mode(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_allowlist(root, "HARDEN-004")

            result = run_checker(root, "--strict")

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("Entry #1", result.stdout)
        self.assertIn("unknown task owner 'HARDEN-004'", result.stdout)

    def test_duplicate_task_owner_across_lifecycle_dirs_fails_strict_mode(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_allowlist(root, "HARDEN-005")
            write_task(root, "backlog", "HARDEN-005")
            write_task(root, "done", "HARDEN-005")

            result = run_checker(root, "--strict")

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("task owner 'HARDEN-005' appears in multiple lifecycle directories", result.stdout)
        self.assertIn("backlog", result.stdout)
        self.assertIn("done", result.stdout)

    def test_warning_mode_reports_done_owner_without_failing(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_allowlist(root, "HARDEN-006")
            write_task(root, "done", "HARDEN-006")

            result = run_checker(root)

        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("Findings: 1", result.stdout)
        self.assertIn("WARNING MODE", result.stdout)
        self.assertIn("task owner 'HARDEN-006' is retired/not open", result.stdout)


if __name__ == "__main__":
    unittest.main()
