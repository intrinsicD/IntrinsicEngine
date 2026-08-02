#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from shutil import copyfile

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
    engine_integration: bool = False,
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
        integration = ""
        if engine_integration:
            integration = """## Engine integration
- Least-structured input: vertex positions.
- Compatible entity sources: mesh vertices, graph nodes, and point-cloud points.
- RuntimeModule: fixture binding.
- Config/agent: fixture config path.
- UI: fixture panel.
- Publication: same-cardinality vertex property.
- End-to-end tests: fixture contract test.

"""
        body = f"""# {task_id} — Fixture

## Goal
- Exercise prospective task validation.

## Non-goals
- No product behavior.

## Context
- Tooling fixture.

{integration}## Required changes
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
contract_schema: 1
contracts: []
contract_review: fixture has no subsystem contract
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
contract_schema: 1
contracts: [repo.task-contract-discovery]
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
contract_schema: 1
contracts: []
contract_review: mechanical fixture
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
contract_schema: 1
contracts: []
contract_review: fixture has no subsystem contract
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
contract_schema: 1
contracts: [repo.task-contract-discovery]
""",
            )
            result = run_validator(root)

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("duplicate key", result.stdout)

    def test_new_task_must_enroll_in_contract_schema(self) -> None:
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
branch: test/contracts
worktree: /tmp/contracts
claimed_at: "2026-08-02T12:00:00Z"
""",
            )
            result = run_validator(root)

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("must declare `contract_schema: 1`", result.stdout)

    def test_unknown_contract_id_is_rejected(self) -> None:
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
branch: test/contracts
worktree: /tmp/contracts
claimed_at: "2026-08-02T12:00:00Z"
contract_schema: 1
contracts: [missing.contract]
""",
            )
            result = run_validator(root)

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("is not a known ID", result.stdout)

    def test_empty_contracts_require_review_reason(self) -> None:
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
branch: test/contracts
worktree: /tmp/contracts
claimed_at: "2026-08-02T12:00:00Z"
contract_schema: 1
contracts: []
""",
            )
            result = run_validator(root)

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("empty `contracts` requires", result.stdout)

    def test_justified_empty_contracts_are_accepted(self) -> None:
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
branch: test/contracts
worktree: /tmp/contracts
claimed_at: "2026-08-02T12:00:00Z"
contract_schema: 1
contracts: []
contract_review: No catalog contract applies to this isolated tooling fixture.
""",
            )
            result = run_validator(root)

        self.assertEqual(result.returncode, 0, result.stdout)

    def test_duplicate_contract_ids_are_rejected(self) -> None:
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
branch: test/contracts
worktree: /tmp/contracts
claimed_at: "2026-08-02T12:00:00Z"
contract_schema: 1
contracts: [repo.task-contract-discovery, repo.task-contract-discovery]
""",
            )
            result = run_validator(root)

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("is duplicated", result.stdout)

    def test_method_contract_requires_engine_integration_matrix(self) -> None:
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
branch: test/contracts
worktree: /tmp/contracts
claimed_at: "2026-08-02T12:00:00Z"
contract_schema: 1
contracts: [method.engine-integration]
""",
            )
            result = run_validator(root)

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("requires an `## Engine integration` section", result.stdout)

    def test_complete_method_engine_integration_matrix_is_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_task(
                root,
                engine_integration=True,
                front_matter="""id: TEST-001
theme: none
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: fixture
branch: test/contracts
worktree: /tmp/contracts
claimed_at: "2026-08-02T12:00:00Z"
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
""",
            )
            result = run_validator(root)

        self.assertEqual(result.returncode, 0, result.stdout)

    def test_method_engine_integration_matrix_rejects_missing_field(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            target = write_task(
                root,
                engine_integration=True,
                front_matter="""id: TEST-001
theme: none
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: fixture
branch: test/contracts
worktree: /tmp/contracts
claimed_at: "2026-08-02T12:00:00Z"
contract_schema: 1
contracts: [method.engine-integration]
""",
            )
            target.write_text(
                target.read_text(encoding="utf-8").replace(
                    "- Publication: same-cardinality vertex property.\n", ""
                ),
                encoding="utf-8",
            )
            result = run_validator(root)

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("missing field(s): Publication", result.stdout)

    def test_byte_identical_legacy_task_is_grandfathered(self) -> None:
        source = (
            REPO_ROOT
            / "tasks/backlog/bugs/BUG-091-gtest-pretest-discovery-cold-timeout.md"
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "tasks"
            target = root / "backlog/bugs" / source.name
            target.parent.mkdir(parents=True, exist_ok=True)
            copyfile(source, target)

            result = run_validator(root)

        self.assertEqual(result.returncode, 0, result.stdout)


if __name__ == "__main__":
    unittest.main()
