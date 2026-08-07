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
        """The docs-sync step must route a real diff range per event.

        The event-payload SHAs reach the script through step-level ``env``
        rather than inline ``${{ }}`` expressions in the ``run`` body, so the
        binding and its use are asserted separately.
        """
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
        docs_sync_step = by_name["Validate documentation synchronization (strict mode)"]
        docs_sync_env = docs_sync_step.get("env", {})
        docs_sync = docs_sync_step["run"]

        expected_env = {
            "EVENT_NAME": "${{ github.event_name }}",
            "PR_BASE_SHA": "${{ github.event.pull_request.base.sha }}",
            "PR_HEAD_SHA": "${{ github.event.pull_request.head.sha }}",
            "MERGE_GROUP_BASE_SHA": "${{ github.event.merge_group.base_sha }}",
            "MERGE_GROUP_HEAD_SHA": "${{ github.event.merge_group.head_sha }}",
        }
        self.assertEqual(
            {name: docs_sync_env.get(name) for name in expected_env},
            expected_env,
        )

        for event, base_var, head_var in (
            ("pull_request", "PR_BASE_SHA", "PR_HEAD_SHA"),
            ("merge_group", "MERGE_GROUP_BASE_SHA", "MERGE_GROUP_HEAD_SHA"),
        ):
            with self.subTest(event=event):
                self.assertIn(f"  {event})", docs_sync)
                self.assertIn(
                    f'[[ -n "${base_var}" && -n "${head_var}" ]] ||', docs_sync
                )
                self.assertIn(f'base_ref="${base_var}"', docs_sync)
                self.assertIn(f'head_ref="${head_var}"', docs_sync)

        self.assertIn("tools/docs/check_docs_sync.py", docs_sync)
        self.assertIn("--diff-mode", docs_sync)
        self.assertIn('--base-ref "$base_ref"', docs_sync)
        self.assertIn('--head-ref "$head_ref"', docs_sync)
        self.assertIn("--strict", docs_sync)

        policy_lines = [
            line.strip()
            for line in by_name["Validate structural CI policy regressions"][
                "run"
            ].splitlines()
            if line.strip()
        ]
        for line in policy_lines:
            with self.subTest(line=line):
                self.assertTrue(line.startswith("python3 "), line)
        policy_scripts = [line[len("python3 ") :] for line in policy_lines]
        self.assertEqual(
            policy_scripts,
            [
                "tests/regression/tooling/Test.CheckTaskStateLinks.py",
                "tests/regression/tooling/Test.CheckKernelConvergence.py",
                "tests/regression/tooling/Test.CheckAraClaims.py",
            ],
        )
        for script in policy_scripts:
            with self.subTest(script=script):
                self.assertTrue((REPO_ROOT / script).is_file(), script)

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
