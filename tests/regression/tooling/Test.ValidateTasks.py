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


def write_task(
    root: Path,
    *,
    front_matter: str,
    task_id: str = "TEST-001",
    micro: bool = False,
) -> Path:
    target = root / "active" / f"{task_id}-fixture.md"
    target.parent.mkdir(parents=True, exist_ok=True)
    if micro:
        body = f"""# {task_id} — Fixture

## Goal
- Exercise prospective task validation.

## Acceptance criteria
- [ ] The fixture is valid.

## Verification
```bash
true
```
"""
    else:
        body = f"""# {task_id} — Fixture

## Goal
- Exercise prospective task validation.

## Non-goals
- No product behavior.

## Context
- Tooling fixture.

## Required changes
- [ ] Add the fixture.

## Tests
- [ ] Run the validator.

## Docs
- [ ] Keep the fixture self-describing.

## Acceptance criteria
- [ ] The fixture is valid.

## Verification
```bash
true
```

## Forbidden changes
- No unrelated work.
"""
    target.write_text(f"---\n{front_matter}---\n{body}", encoding="utf-8")
    return target


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

    def test_new_task_must_enroll_in_prospective_workflow(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_task(
                root,
                front_matter="""id: TEST-001
theme: none
depends_on: []
""",
            )
            result = run_validator(root)

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("must declare `workflow_schema: 1`", result.stdout)

    def test_standard_workflow_metadata_is_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_task(
                root,
                front_matter="""id: TEST-001
theme: none
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: fixture
branch: test/workflow
worktree: /tmp/workflow
claimed_at: "2026-07-29T12:00:00Z"
""",
            )
            result = run_validator(root)

        self.assertEqual(result.returncode, 0, result.stdout)

    def test_micro_workflow_requires_explicit_evidence_skip_reason(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_task(
                root,
                micro=True,
                front_matter="""id: TEST-001
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
owner: fixture
branch: test/workflow
worktree: /tmp/workflow
claimed_at: "2026-07-29T12:00:00Z"
""",
            )
            result = run_validator(root)

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("`evidence_skip_reason`", result.stdout)

    def test_micro_profile_cannot_be_applied_to_full_task(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_task(
                root,
                front_matter="""id: TEST-001
theme: none
depends_on: []
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: fixture
owner: fixture
branch: test/workflow
worktree: /tmp/workflow
claimed_at: "2026-07-29T12:00:00Z"
""",
            )
            result = run_validator(root)

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("requires `template: micro`", result.stdout)

    def test_duplicate_front_matter_key_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_task(
                root,
                front_matter="""id: TEST-001
id: TEST-001
theme: none
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: fixture
branch: test/workflow
worktree: /tmp/workflow
claimed_at: "2026-07-29T12:00:00Z"
""",
            )
            result = run_validator(root)

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("duplicate key", result.stdout)


if __name__ == "__main__":
    unittest.main()
