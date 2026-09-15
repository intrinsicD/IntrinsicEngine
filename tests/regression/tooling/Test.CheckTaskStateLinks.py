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


def write_retirement_tree(root: Path) -> None:
    """Create the canonical retirement directories the navigation links cite.

    The links under test must resolve to files that really exist in the
    fixture, so a passing navigation case cannot be confused with a missing
    target and a rejected nested path cannot be confused with a typo.
    """
    tasks = root / "tasks"
    (tasks / "active").mkdir(parents=True, exist_ok=True)
    (tasks / "backlog").mkdir(parents=True, exist_ok=True)
    (tasks / "done" / "subdir").mkdir(parents=True, exist_ok=True)
    (tasks / "archive" / "2026").mkdir(parents=True, exist_ok=True)
    (tasks / "done" / "README.md").write_text("# Done index\n", encoding="utf-8")
    (tasks / "done" / "RETIREMENT-LOG.md").write_text(
        "# Retirement log\n", encoding="utf-8"
    )
    (tasks / "done" / "subdir" / "README.md").write_text(
        "# Nested done index\n", encoding="utf-8"
    )
    (tasks / "done" / "NOTES.md").write_text("# Notes\n", encoding="utf-8")
    (tasks / "archive" / "README.md").write_text("# Archive index\n", encoding="utf-8")
    (tasks / "archive" / "2026" / "README.md").write_text(
        "# Nested archive index\n", encoding="utf-8"
    )


def write_index(root: Path, relative_path: str, body: str) -> None:
    """Write a live index file, creating its category directory if needed."""
    path = root / relative_path
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(body, encoding="utf-8")


def navigation_block(prefix: str) -> str:
    """Canonical retirement navigation, written relative to ``prefix``."""
    return (
        f"Retirement navigation: [done tasks]({prefix}done/), "
        f"[done root]({prefix}done), "
        f"[archive]({prefix}archive/), "
        f"[archive root]({prefix}archive), "
        f"[done index]({prefix}done/README.md), "
        f"[done index section]({prefix}done/README.md#index), "
        f"[archive index]({prefix}archive/README.md), "
        f"[archive index section]({prefix}archive/README.md#index), "
        f"[retirement log]({prefix}done/RETIREMENT-LOG.md)\n"
    )


# Live indexes that must accept canonical navigation: both state-only
# entrypoints and a category README, each linking from its own depth.
NAVIGATION_ENTRYPOINTS = (
    ("state-only backlog index", "tasks/backlog/README.md", "../", "# Backlog\n\n"),
    ("state-only active index", "tasks/active/README.md", "../", "# Active\n\n"),
    (
        "category index",
        "tasks/backlog/runtime/README.md",
        "../../",
        "# Runtime backlog\n\n## Related queues\n\n",
    ),
)

# The same live indexes, paired with the diagnostic each validator emits and
# the retired task IDs its fixture files use.
REJECTING_ENTRYPOINTS = (
    (
        "tasks/backlog/README.md",
        "../",
        "# Backlog\n\n",
        "state-only index links retired task",
        ("TASK-010", "TASK-011"),
    ),
    (
        "tasks/backlog/runtime/README.md",
        "../../",
        "# Runtime backlog\n\n## Related queues\n\n",
        "category index links retired task",
        ("TASK-020", "TASK-021"),
    ),
)


class CheckTaskStateLinksTests(unittest.TestCase):
    def assertNoLifecycleProseFindings(self, stdout: str) -> None:
        for noise in ("line claims", "link targets tasks/", "multiple lifecycle"):
            self.assertNotIn(noise, stdout)

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
                "tests/regression/tooling/Test.SourceDocumentationAudit.py",
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

    # --- BUG-196: canonical retirement navigation is not task membership ---

    def test_live_indexes_allow_canonical_retirement_navigation(self) -> None:
        for label, index_path, prefix, heading in NAVIGATION_ENTRYPOINTS:
            with self.subTest(entrypoint=label):
                with tempfile.TemporaryDirectory() as tmp:
                    root = Path(tmp)
                    write_retirement_tree(root)
                    write_index(root, index_path, heading + navigation_block(prefix))

                    result = run_checker(root)

                self.assertEqual(result.returncode, 0, result.stdout)
                self.assertIn("No task-state link findings", result.stdout)
                self.assertNoLifecycleProseFindings(result.stdout)

    def test_live_indexes_still_reject_retired_task_links(self) -> None:
        for index_path, prefix, heading, diagnostic, (
            done_id,
            archive_id,
        ) in REJECTING_ENTRYPOINTS:
            with self.subTest(diagnostic=diagnostic):
                with tempfile.TemporaryDirectory() as tmp:
                    root = Path(tmp)
                    write_retirement_tree(root)
                    write_task(
                        root / "tasks" / "done" / f"{done_id}-retired.md", done_id
                    )
                    write_task(
                        root / "tasks" / "archive" / "2026" / f"{archive_id}-swept.md",
                        archive_id,
                    )
                    write_index(
                        root,
                        index_path,
                        heading
                        + f"- [{done_id} record]({prefix}done/{done_id}-retired.md)\n"
                        + f"- [{archive_id} record]"
                        f"({prefix}archive/2026/{archive_id}-swept.md#outcome)\n"
                        # Misleading link text: the target, not the label,
                        # decides membership, so this is a second finding.
                        + f"- [done README]({prefix}done/{done_id}-retired.md)\n",
                    )

                    result = run_checker(root)

                self.assertEqual(result.returncode, 1, result.stdout)
                self.assertEqual(
                    result.stdout.count(f"{diagnostic} {done_id}-retired.md"),
                    2,
                    result.stdout,
                )
                self.assertIn(f"{diagnostic} {archive_id}-swept.md", result.stdout)
                self.assertNoLifecycleProseFindings(result.stdout)

    def test_live_indexes_still_reject_noncanonical_navigation(self) -> None:
        for index_path, prefix, heading, diagnostic, _ids in REJECTING_ENTRYPOINTS:
            with self.subTest(diagnostic=diagnostic):
                with tempfile.TemporaryDirectory() as tmp:
                    root = Path(tmp)
                    write_retirement_tree(root)
                    write_index(
                        root,
                        index_path,
                        heading
                        + f"- [other direct document]({prefix}done/NOTES.md)\n"
                        + f"- [nested done index]({prefix}done/subdir/README.md)\n"
                        + f"- [nested archive index]({prefix}archive/2026/README.md)\n"
                        + f"- [nested done directory]({prefix}done/subdir/)\n"
                        + f"- [nested archive directory]({prefix}archive/2026/)\n",
                    )

                    result = run_checker(root)

                self.assertEqual(result.returncode, 1, result.stdout)
                self.assertEqual(
                    result.stdout.count(f"{diagnostic} README.md"), 2, result.stdout
                )
                self.assertIn(f"{diagnostic} NOTES.md", result.stdout)
                self.assertIn(f"{diagnostic} subdir", result.stdout)
                self.assertIn(f"{diagnostic} 2026", result.stdout)
                self.assertNoLifecycleProseFindings(result.stdout)


if __name__ == "__main__":
    unittest.main()
