#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

TOOL = Path(__file__).resolve().parents[3] / "tools/agents/task_claim.py"


def task_text(task_id: str) -> str:
    return f"""---
id: {task_id}
theme: none
depends_on: []
---
# {task_id} — Claim fixture

## Goal
- Exercise task claims.

## Non-goals
- No product behavior.

## Context
- Tooling fixture.

## Required changes
- [ ] Claim the task.

## Tests
- [ ] Run the claim tests.

## Docs
- [ ] Keep the fixture self-describing.

## Acceptance criteria
- [ ] Claims are deterministic.

## Verification
```bash
true
```

## Forbidden changes
- No unrelated work.
"""


def invoke(worktree: Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(TOOL), *args],
        cwd=worktree,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


class ClaimFixture:
    def __init__(self, root: Path):
        self.main = root / "main"
        self.other = root / "other"
        self.main.mkdir()
        subprocess.run(["git", "init", "-q"], cwd=self.main, check=True)
        subprocess.run(
            ["git", "config", "user.email", "fixture@example.invalid"],
            cwd=self.main,
            check=True,
        )
        subprocess.run(
            ["git", "config", "user.name", "Fixture"],
            cwd=self.main,
            check=True,
        )
        tasks = self.main / "tasks/active"
        tasks.mkdir(parents=True)
        for task_id in ("TEST-001", "TEST-002", "TEST-003"):
            (tasks / f"{task_id}-fixture.md").write_text(
                task_text(task_id), encoding="utf-8"
            )
        (self.main / "src/a").mkdir(parents=True)
        (self.main / "src/a/file.txt").write_text("a\n", encoding="utf-8")
        (self.main / "docs/b").mkdir(parents=True)
        (self.main / "docs/b/file.txt").write_text("b\n", encoding="utf-8")
        subprocess.run(["git", "add", "."], cwd=self.main, check=True)
        subprocess.run(["git", "commit", "-qm", "base"], cwd=self.main, check=True)
        subprocess.run(
            ["git", "worktree", "add", "-q", "-b", "agent-two", str(self.other)],
            cwd=self.main,
            check=True,
        )

    def acquire(
        self,
        worktree: Path,
        task_id: str,
        owner: str,
        *paths: str,
        lease: int = 3600,
    ) -> subprocess.CompletedProcess[str]:
        command = [
            "acquire",
            "--root",
            str(worktree),
            "--task-id",
            task_id,
            "--owner",
            owner,
            "--lease-seconds",
            str(lease),
        ]
        for path in paths:
            command.extend(["--path", path])
        return invoke(worktree, *command)


class TaskClaimTests(unittest.TestCase):
    def test_atomic_task_claim_has_one_winner(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ClaimFixture(Path(tmp))
            commands = []
            for worktree, owner in (
                (fixture.main, "driver-a"),
                (fixture.other, "driver-b"),
            ):
                commands.append(
                    subprocess.Popen(
                        [
                            sys.executable,
                            str(TOOL),
                            "acquire",
                            "--root",
                            str(worktree),
                            "--task-id",
                            "TEST-001",
                            "--owner",
                            owner,
                        ],
                        cwd=worktree,
                        text=True,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT,
                    )
                )
            results = [process.communicate()[0] for process in commands]
            codes = [process.returncode for process in commands]
        self.assertEqual(sorted(codes), [0, 2], results)

    def test_shared_worktree_rejects_second_writer(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ClaimFixture(Path(tmp))
            first = fixture.acquire(fixture.main, "TEST-001", "driver-a")
            second = fixture.acquire(fixture.main, "TEST-002", "driver-b")
        self.assertEqual(first.returncode, 0, first.stdout)
        self.assertEqual(second.returncode, 2, second.stdout)
        self.assertIn("one-writer policy", second.stdout)

    def test_cross_worktree_overlapping_path_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ClaimFixture(Path(tmp))
            first = fixture.acquire(fixture.main, "TEST-001", "driver-a", "src/a")
            second = fixture.acquire(
                fixture.other, "TEST-002", "driver-b", "src/a/file.txt"
            )
        self.assertEqual(first.returncode, 0, first.stdout)
        self.assertEqual(second.returncode, 2, second.stdout)
        self.assertIn("path overlap", second.stdout)

    def test_cross_worktree_non_overlapping_paths_are_allowed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ClaimFixture(Path(tmp))
            first = fixture.acquire(fixture.main, "TEST-001", "driver-a", "src/a")
            second = fixture.acquire(fixture.other, "TEST-002", "driver-b", "docs/b")
        self.assertEqual(first.returncode, 0, first.stdout)
        self.assertEqual(second.returncode, 0, second.stdout)

    def test_wrong_owner_cannot_release(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ClaimFixture(Path(tmp))
            fixture.acquire(fixture.main, "TEST-001", "driver-a")
            result = invoke(
                fixture.main,
                "release",
                "--root",
                str(fixture.main),
                "--task-id",
                "TEST-001",
                "--owner",
                "driver-b",
                "--reason",
                "wrong owner test",
            )
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("wrong owner", result.stdout)

    def test_owner_release_retains_history_and_allows_reclaim(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ClaimFixture(Path(tmp))
            fixture.acquire(fixture.main, "TEST-001", "driver-a")
            released = invoke(
                fixture.main,
                "release",
                "--root",
                str(fixture.main),
                "--task-id",
                "TEST-001",
                "--owner",
                "driver-a",
                "--reason",
                "slice complete",
            )
            reclaimed = fixture.acquire(fixture.main, "TEST-001", "driver-a")
            common = subprocess.run(
                [
                    "git",
                    "rev-parse",
                    "--path-format=absolute",
                    "--git-common-dir",
                ],
                cwd=fixture.main,
                text=True,
                stdout=subprocess.PIPE,
                check=True,
            ).stdout.strip()
            history = list(
                (Path(common) / "intrinsic-agent-claims/v1/history/TEST-001").glob(
                    "*.json"
                )
            )
        self.assertEqual(released.returncode, 0, released.stdout)
        self.assertEqual(reclaimed.returncode, 0, reclaimed.stdout)
        self.assertEqual(len(history), 1)

    def test_stale_claim_requires_explicit_recovery(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ClaimFixture(Path(tmp))
            acquired = fixture.acquire(fixture.main, "TEST-001", "driver-a", lease=1)
            self.assertEqual(acquired.returncode, 0, acquired.stdout)
            time.sleep(1.1)
            blocked = fixture.acquire(fixture.main, "TEST-001", "driver-b")
            recovered = invoke(
                fixture.main,
                "recover",
                "--root",
                str(fixture.main),
                "--task-id",
                "TEST-001",
                "--actor",
                "maintainer",
                "--reason",
                "lease expired",
            )
            reclaimed = fixture.acquire(fixture.main, "TEST-001", "driver-b")
        self.assertEqual(blocked.returncode, 2, blocked.stdout)
        self.assertIn("explicit recover", blocked.stdout)
        self.assertEqual(recovered.returncode, 0, recovered.stdout)
        self.assertEqual(reclaimed.returncode, 0, reclaimed.stdout)


if __name__ == "__main__":
    unittest.main()
