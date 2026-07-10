#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parents[3]
CHECKER = REPO_ROOT / "tools" / "agents" / "check_task_state_links.py"
CI_DOCS_WORKFLOW = REPO_ROOT / ".github" / "workflows" / "ci-docs.yml"


def write_task(path: Path, task_id: str) -> None:
    path.write_text(f"# {task_id} — Fixture task\n", encoding="utf-8")


def run_checker(root: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(CHECKER), "--root", str(root), "--strict"],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


class CheckTaskStateLinksTests(unittest.TestCase):
    def test_ci_docs_enforces_task_state_and_docs_sync_strictly(self) -> None:
        payload = yaml.safe_load(CI_DOCS_WORKFLOW.read_text(encoding="utf-8"))
        steps = payload["jobs"]["docs-validation"]["steps"]
        by_name = {step["name"]: step for step in steps}

        checkout = by_name["Checkout"]
        self.assertEqual(checkout["with"]["fetch-depth"], 0)
        self.assertFalse(checkout["with"]["persist-credentials"])

        self.assertEqual(
            by_name["Validate task state links (strict mode)"]["run"],
            "python3 tools/agents/check_task_state_links.py --root . --strict",
        )
        docs_sync = by_name["Validate documentation synchronization (strict mode)"][
            "run"
        ]
        self.assertIn("github.event.pull_request.base.sha", docs_sync)
        self.assertIn("tools/docs/check_docs_sync.py", docs_sync)
        self.assertIn("--diff-mode", docs_sync)
        self.assertIn('--base-ref "$base_ref"', docs_sync)
        self.assertIn("--strict", docs_sync)

        policy_test = by_name["Validate structural CI policy regressions"]["run"]
        self.assertEqual(
            policy_test,
            "python3 tests/regression/tooling/Test.CheckTaskStateLinks.py",
        )

    def test_link_to_wrong_lifecycle_directory_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "tasks" / "backlog" / "architecture").mkdir(parents=True)
            (root / "tasks" / "active").mkdir(parents=True)
            write_task(
                root
                / "tasks"
                / "backlog"
                / "architecture"
                / "TASK-001-real-location.md",
                "TASK-001",
            )
            (root / "tasks" / "backlog" / "README.md").write_text(
                "[`TASK-001`](../active/TASK-001-real-location.md) (active)\n",
                encoding="utf-8",
            )

            result = run_checker(root)

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("TASK-001: link targets tasks/active", result.stdout)
        self.assertIn("actual location is backlog:", result.stdout)

    def test_stale_lifecycle_prose_near_task_id_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "tasks" / "backlog").mkdir(parents=True)
            (root / "tasks" / "done").mkdir(parents=True)
            write_task(root / "tasks" / "done" / "TASK-002-complete.md", "TASK-002")
            (root / "tasks" / "backlog" / "README.md").write_text(
                "[`TASK-002`](../done/TASK-002-complete.md) (active)\n",
                encoding="utf-8",
            )

            result = run_checker(root)

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("TASK-002: line claims active", result.stdout)
        self.assertIn("actual location is done:", result.stdout)

    def test_historical_done_link_with_done_claim_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            category = root / "tasks" / "backlog" / "architecture"
            category.mkdir(parents=True)
            (root / "tasks" / "done").mkdir(parents=True)
            write_task(root / "tasks" / "done" / "TASK-003-complete.md", "TASK-003")
            (category / "README.md").write_text(
                "## Retired\n\n[`TASK-003`](../../done/TASK-003-complete.md) (done)\n",
                encoding="utf-8",
            )

            result = run_checker(root)

        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("No task-state link findings", result.stdout)


if __name__ == "__main__":
    unittest.main()
