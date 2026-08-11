#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import os
import socket
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[3]
TOOL = REPOSITORY / "tools/agents/agent_work_graph.py"
CLAIM_TOOL = REPOSITORY / "tools/agents/task_claim.py"
DEFAULT_RECIPE = REPOSITORY / "tools/agents/work_graphs/review-diamond.v1.json"


def task_text(task_id: str, profile: str) -> str:
    return f"""---
id: {task_id}
theme: H
depends_on: []
workflow_schema: 1
workflow_profile: {profile}
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: Fixture has no reusable engine contract.
---
# {task_id} — Work graph fixture

## Goal
- Exercise a repository-native work graph.

## Non-goals
- No product behavior.

## Context
- Isolated tooling fixture.

## Slice plan
- Slice 1 establishes the fixture state.
- Slice 2 exercises an explicitly bounded continuation.

## Required changes
- [ ] Run the graph.

## Tests
- [ ] Exercise transitions.

## Docs
- [ ] Keep the fixture self-describing.

## Acceptance criteria
- [ ] State is deterministic.

## Verification
```bash
true
```

## Forbidden changes
- No unrelated work.
"""


def invoke(root: Path, tool: Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(tool), *args],
        cwd=root,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


class WorkGraphFixture:
    def __init__(self, root: Path, profile: str = "standard"):
        self.root = root
        self.task_id = "PROC-999"
        self.owner = "driver"
        subprocess.run(["git", "init", "-q", "-b", "main"], cwd=root, check=True)
        subprocess.run(
            ["git", "config", "user.email", "fixture@example.invalid"],
            cwd=root,
            check=True,
        )
        subprocess.run(["git", "config", "user.name", "Fixture"], cwd=root, check=True)
        task_dir = root / "tasks/active"
        task_dir.mkdir(parents=True)
        (task_dir / "PROC-999-fixture.md").write_text(
            task_text(self.task_id, profile), encoding="utf-8"
        )
        recipe_dir = root / "tools/agents/work_graphs"
        recipe_dir.mkdir(parents=True)
        self.recipe = recipe_dir / DEFAULT_RECIPE.name
        shutil.copyfile(DEFAULT_RECIPE, self.recipe)
        (root / "README.md").write_text("fixture\n", encoding="utf-8")
        subprocess.run(["git", "add", "."], cwd=root, check=True)
        subprocess.run(["git", "commit", "-qm", "base"], cwd=root, check=True)

    def claim(self, owner: str | None = None) -> subprocess.CompletedProcess[str]:
        return invoke(
            self.root,
            CLAIM_TOOL,
            "acquire",
            "--root",
            str(self.root),
            "--task-id",
            self.task_id,
            "--owner",
            owner or self.owner,
        )

    def release(self) -> subprocess.CompletedProcess[str]:
        return invoke(
            self.root,
            CLAIM_TOOL,
            "release",
            "--root",
            str(self.root),
            "--task-id",
            self.task_id,
            "--owner",
            self.owner,
            "--reason",
            "fixture complete",
        )

    def work(self, *args: str) -> subprocess.CompletedProcess[str]:
        return invoke(self.root, TOOL, *args, "--root", str(self.root))

    def start(self) -> subprocess.CompletedProcess[str]:
        return self.work(
            "start",
            "--task-id",
            self.task_id,
            "--owner",
            self.owner,
            "--recipe",
            str(self.recipe),
            "--base-ref",
            "HEAD",
        )

    def resume(self, owner: str) -> subprocess.CompletedProcess[str]:
        return self.work(
            "resume",
            "--task-id",
            self.task_id,
            "--owner",
            owner,
            "--reason",
            "fixture handoff",
        )

    def begin(self, node: str, actor: str | None = None):
        return self.work(
            "begin",
            "--task-id",
            self.task_id,
            "--node",
            node,
            "--actor",
            actor or self.owner,
        )

    def finish(
        self,
        node: str,
        outcome: str = "succeeded",
        actor: str | None = None,
    ):
        return self.work(
            "finish",
            "--task-id",
            self.task_id,
            "--node",
            node,
            "--actor",
            actor or self.owner,
            "--outcome",
            outcome,
        )

    def complete(self, node: str, actor: str | None = None) -> None:
        started = self.begin(node, actor)
        self._assert_success(started)
        finished = self.finish(node, actor=actor)
        self._assert_success(finished)

    def show(self) -> tuple[subprocess.CompletedProcess[str], dict]:
        result = self.work("show", "--task-id", self.task_id, "--json")
        return result, json.loads(result.stdout)

    def progress_to_fanout(self) -> None:
        for node in ("context", "plan", "implement", "freeze_diff"):
            self.complete(node)

    def progress_to_join(self, join_outcome: str = "succeeded") -> None:
        self.progress_to_fanout()
        for node in (
            "architecture_review",
            "verification",
            "docs_evidence_review",
        ):
            self.complete(node)
        self._assert_success(self.begin("findings_join"))
        self._assert_success(self.finish("findings_join", join_outcome))

    @staticmethod
    def _assert_success(result: subprocess.CompletedProcess[str]) -> None:
        if result.returncode != 0:
            raise AssertionError(result.stdout)


class AgentWorkGraphTests(unittest.TestCase):
    def test_checked_in_recipe_is_valid(self) -> None:
        result = invoke(
            REPOSITORY,
            TOOL,
            "validate-recipe",
            "--recipe",
            str(DEFAULT_RECIPE),
        )
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("10 nodes", result.stdout)

    def test_validator_rejects_cycles_and_parallel_writers(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            recipe = json.loads(DEFAULT_RECIPE.read_text(encoding="utf-8"))
            recipe["nodes"][0]["requires"] = ["finalize"]
            cyclic = root / "cycle.json"
            cyclic.write_text(json.dumps(recipe), encoding="utf-8")
            cycle_result = invoke(
                REPOSITORY,
                TOOL,
                "validate-recipe",
                "--recipe",
                str(cyclic),
            )

            recipe = json.loads(DEFAULT_RECIPE.read_text(encoding="utf-8"))
            for node in recipe["nodes"]:
                if node["id"] in {"architecture_review", "docs_evidence_review"}:
                    node["permission"] = "write"
            parallel = root / "parallel.json"
            parallel.write_text(json.dumps(recipe), encoding="utf-8")
            parallel_result = invoke(
                REPOSITORY,
                TOOL,
                "validate-recipe",
                "--recipe",
                str(parallel),
            )
        self.assertEqual(cycle_result.returncode, 2, cycle_result.stdout)
        self.assertIn("acyclic", cycle_result.stdout)
        self.assertEqual(parallel_result.returncode, 2, parallel_result.stdout)
        self.assertIn("not parallel", parallel_result.stdout)

    def test_validator_rejects_profile_pruned_writer_and_final_binder(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            writer_recipe = json.loads(DEFAULT_RECIPE.read_text(encoding="utf-8"))
            for node in writer_recipe["nodes"]:
                if node["permission"] == "write":
                    node["minimum_profile"] = "high-risk"
            writer_path = root / "pruned-writer.json"
            writer_path.write_text(json.dumps(writer_recipe), encoding="utf-8")
            writer_result = invoke(
                REPOSITORY,
                TOOL,
                "validate-recipe",
                "--recipe",
                str(writer_path),
            )

            final_recipe = json.loads(DEFAULT_RECIPE.read_text(encoding="utf-8"))
            for node in final_recipe["nodes"]:
                if node["binds_surface"]:
                    node["minimum_profile"] = "high-risk"
            final_path = root / "pruned-final.json"
            final_path.write_text(json.dumps(final_recipe), encoding="utf-8")
            final_result = invoke(
                REPOSITORY,
                TOOL,
                "validate-recipe",
                "--recipe",
                str(final_path),
            )
        self.assertEqual(writer_result.returncode, 2, writer_result.stdout)
        self.assertIn("write-capable node active", writer_result.stdout)
        self.assertEqual(final_result.returncode, 2, final_result.stdout)
        self.assertIn("final surface-binding node must be active", final_result.stdout)

    def test_cli_rejects_invalid_task_id_before_common_dir_path_resolution(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = WorkGraphFixture(Path(tmp))
            result = fixture.work(
                "show",
                "--task-id",
                "../../PROC-999",
                "--json",
            )
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("invalid task ID", result.stdout)

    def test_start_requires_matching_live_claim_and_prunes_profile(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = WorkGraphFixture(Path(tmp))
            missing = fixture.start()
            self.assertEqual(missing.returncode, 2, missing.stdout)
            self.assertIn("no live task claim", missing.stdout)
            fixture._assert_success(fixture.claim())
            fixture._assert_success(fixture.start())
            result, state = fixture.show()
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(state["derived"]["ready_nodes"], ["context"])
        self.assertEqual(
            state["nodes"]["independent_review"]["status"], "profile_skipped"
        )

    def test_single_writer_and_parallel_read_fanout(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = WorkGraphFixture(Path(tmp))
            fixture._assert_success(fixture.claim())
            fixture._assert_success(fixture.start())
            fixture.complete("context")
            fixture.complete("plan")
            rejected = fixture.begin("implement", "reviewer")
            self.assertEqual(rejected.returncode, 2, rejected.stdout)
            self.assertIn("belongs to claimed owner", rejected.stdout)
            fixture.complete("implement")
            fixture.complete("freeze_diff")
            result, state = fixture.show()
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(
            state["derived"]["ready_nodes"],
            ["architecture_review", "docs_evidence_review", "verification"],
        )

    def test_resume_rebinds_recovered_claim_and_invalidates_running_work(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = WorkGraphFixture(Path(tmp))
            fixture._assert_success(fixture.claim())
            fixture._assert_success(fixture.start())
            fixture.complete("context")
            fixture._assert_success(fixture.begin("plan"))
            fixture._assert_success(fixture.release())
            fixture._assert_success(fixture.claim("replacement"))
            stale_result, stale_state = fixture.show()
            resumed = fixture.resume("replacement")
            fixture._assert_success(resumed)
            result, state = fixture.show()
            fixture.complete("plan", "replacement")
            prior_writer = fixture.begin("implement", fixture.owner)
            replacement_writer = fixture.begin("implement", "replacement")
        self.assertEqual(stale_result.returncode, 1, stale_result.stdout)
        self.assertIn(
            "task claim owner changed", stale_state["derived"]["stale_reasons"]
        )
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(state["owner"], "replacement")
        self.assertEqual(state["nodes"]["context"]["status"], "succeeded")
        self.assertEqual(state["nodes"]["plan"]["status"], "ready")
        self.assertEqual(state["nodes"]["plan"]["attempts"], 1)
        self.assertEqual(prior_writer.returncode, 2, prior_writer.stdout)
        self.assertIn("belongs to claimed owner replacement", prior_writer.stdout)
        self.assertEqual(replacement_writer.returncode, 0, replacement_writer.stdout)

    def test_same_label_reacquire_requires_explicit_resume(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = WorkGraphFixture(Path(tmp))
            fixture._assert_success(fixture.claim())
            fixture._assert_success(fixture.start())
            fixture._assert_success(fixture.release())
            fixture._assert_success(fixture.claim())
            stale_result, stale_state = fixture.show()
            rejected = fixture.begin("context")
            abort_rejected = fixture.work(
                "abort",
                "--task-id",
                fixture.task_id,
                "--actor",
                fixture.owner,
                "--reason",
                "same-label replacement must resume",
            )
            resumed = fixture.resume(fixture.owner)
            fixture._assert_success(resumed)
            result, state = fixture.show()
        self.assertEqual(stale_result.returncode, 1, stale_result.stdout)
        self.assertIn(
            "task claim generation changed", stale_state["derived"]["stale_reasons"]
        )
        self.assertEqual(rejected.returncode, 2, rejected.stdout)
        self.assertIn("work graph is stale", rejected.stdout)
        self.assertEqual(abort_rejected.returncode, 2, abort_rejected.stdout)
        self.assertIn("task claim generation changed", abort_rejected.stdout)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(state["owner"], fixture.owner)

    def test_same_label_sibling_worktree_handoff_requires_resume(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "primary"
            root.mkdir()
            fixture = WorkGraphFixture(root)
            fixture._assert_success(fixture.claim())
            fixture._assert_success(fixture.start())
            fixture._assert_success(fixture.release())
            sibling = Path(tmp) / "sibling"
            subprocess.run(
                ["git", "worktree", "add", "-q", "-b", "handoff", str(sibling), "HEAD"],
                cwd=fixture.root,
                check=True,
            )
            acquired = invoke(
                sibling,
                CLAIM_TOOL,
                "acquire",
                "--root",
                str(sibling),
                "--task-id",
                fixture.task_id,
                "--owner",
                fixture.owner,
            )
            fixture._assert_success(acquired)
            stale_result, stale_state = fixture.show()
            rejected = fixture.begin("context")
            resumed = invoke(
                sibling,
                TOOL,
                "resume",
                "--root",
                str(sibling),
                "--task-id",
                fixture.task_id,
                "--owner",
                fixture.owner,
                "--reason",
                "same-label sibling handoff",
            )
            fixture._assert_success(resumed)
            shown = invoke(
                sibling,
                TOOL,
                "show",
                "--root",
                str(sibling),
                "--task-id",
                fixture.task_id,
                "--json",
            )
            state = json.loads(shown.stdout)
        self.assertEqual(stale_result.returncode, 1, stale_result.stdout)
        self.assertTrue(
            any("another worktree" in reason for reason in stale_state["derived"]["stale_reasons"]),
            stale_state,
        )
        self.assertEqual(rejected.returncode, 2, rejected.stdout)
        self.assertIn("work graph is stale", rejected.stdout)
        self.assertEqual(shown.returncode, 0, shown.stdout)
        self.assertEqual(state["worktree"], str(sibling))

    def test_recipe_drift_makes_an_active_run_stale(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = WorkGraphFixture(Path(tmp))
            fixture._assert_success(fixture.claim())
            fixture._assert_success(fixture.start())
            recipe = json.loads(fixture.recipe.read_text(encoding="utf-8"))
            recipe["description"] += " drift"
            fixture.recipe.write_text(
                json.dumps(recipe, indent=2) + "\n", encoding="utf-8"
            )
            result, state = fixture.show()
            transition = fixture.begin("context")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn(
            "checked-in recipe digest changed", state["derived"]["stale_reasons"]
        )
        self.assertEqual(transition.returncode, 2, transition.stdout)
        self.assertIn("work graph is stale", transition.stdout)

    def test_resume_rebinds_exhausted_running_work_as_failed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = WorkGraphFixture(Path(tmp))
            recipe = json.loads(fixture.recipe.read_text(encoding="utf-8"))
            for node in recipe["nodes"]:
                if node["id"] == "context":
                    node["max_attempts"] = 1
            fixture.recipe.write_text(
                json.dumps(recipe, indent=2) + "\n", encoding="utf-8"
            )
            subprocess.run(["git", "add", "."], cwd=fixture.root, check=True)
            subprocess.run(
                ["git", "commit", "-qm", "limit context attempts"],
                cwd=fixture.root,
                check=True,
            )
            fixture._assert_success(fixture.claim())
            fixture._assert_success(fixture.start())
            fixture._assert_success(fixture.begin("context"))
            fixture._assert_success(fixture.release())
            fixture._assert_success(fixture.claim("replacement"))
            fixture._assert_success(fixture.resume("replacement"))
            result, state = fixture.show()
            aborted = fixture.work(
                "abort",
                "--task-id",
                fixture.task_id,
                "--actor",
                "replacement",
                "--reason",
                "exhausted handoff",
            )
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(state["derived"]["status"], "blocked")
        self.assertEqual(state["nodes"]["context"]["status"], "failed")
        self.assertIn(
            "attempt budget exhausted", state["nodes"]["context"]["outcome_note"]
        )
        self.assertEqual(aborted.returncode, 0, aborted.stdout)

    def test_high_risk_gate_rejects_the_writer(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = WorkGraphFixture(Path(tmp), profile="high-risk")
            fixture._assert_success(fixture.claim())
            fixture._assert_success(fixture.start())
            fixture.progress_to_join()
            rejected = fixture.begin("independent_review", fixture.owner)
            self.assertEqual(rejected.returncode, 2, rejected.stdout)
            self.assertIn("cannot be performed by task owner", rejected.stdout)
            fixture.complete("independent_review", "reviewer")
            result, state = fixture.show()
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(state["derived"]["ready_nodes"], ["finalize"])

    def test_blocked_join_reopens_writer_and_preserves_event_chain(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = WorkGraphFixture(Path(tmp))
            fixture._assert_success(fixture.claim())
            fixture._assert_success(fixture.start())
            fixture.progress_to_join("blocked")
            note = fixture.work(
                "note",
                "--task-id",
                fixture.task_id,
                "--node",
                "implement",
                "--actor",
                "operator",
                "--kind",
                "constraint",
                "--text",
                "Repair the blocking review finding before rerunning checks.",
            )
            fixture._assert_success(note)
            reopened = fixture.work(
                "reopen",
                "--task-id",
                fixture.task_id,
                "--node",
                "implement",
                "--actor",
                fixture.owner,
                "--reason",
                "blocking review finding",
            )
            fixture._assert_success(reopened)
            result, state = fixture.show()

            common = subprocess.run(
                ["git", "rev-parse", "--path-format=absolute", "--git-common-dir"],
                cwd=fixture.root,
                text=True,
                stdout=subprocess.PIPE,
                check=True,
            ).stdout.strip()
            event_path = (
                Path(common) / "intrinsic-agent-work-graphs/v1/PROC-999.events.jsonl"
            )
            lines = event_path.read_text(encoding="utf-8").splitlines()
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(state["nodes"]["implement"]["status"], "ready")
        self.assertEqual(state["nodes"]["freeze_diff"]["status"], "pending")
        self.assertEqual(state["nodes"]["implement"]["notes_count"], 1)
        for index, line in enumerate(lines):
            record = json.loads(line)
            self.assertEqual(record["sequence"], index + 1)
            expected = (
                None
                if index == 0
                else hashlib.sha256(lines[index - 1].encode("utf-8")).hexdigest()
            )
            self.assertEqual(record["previous_entry_sha256"], expected)

    def test_reopen_rejects_exhausted_attempt_budget(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = WorkGraphFixture(Path(tmp))
            recipe = json.loads(fixture.recipe.read_text(encoding="utf-8"))
            for node in recipe["nodes"]:
                if node["id"] == "implement":
                    node["max_attempts"] = 1
            fixture.recipe.write_text(
                json.dumps(recipe, indent=2) + "\n", encoding="utf-8"
            )
            subprocess.run(["git", "add", "."], cwd=fixture.root, check=True)
            subprocess.run(
                ["git", "commit", "-qm", "limit attempts"],
                cwd=fixture.root,
                check=True,
            )
            fixture._assert_success(fixture.claim())
            fixture._assert_success(fixture.start())
            fixture.progress_to_join("blocked")
            rejected = fixture.work(
                "reopen",
                "--task-id",
                fixture.task_id,
                "--node",
                "implement",
                "--actor",
                fixture.owner,
                "--reason",
                "retry beyond budget",
            )
        self.assertEqual(rejected.returncode, 2, rejected.stdout)
        self.assertIn("exhausted its attempt budget", rejected.stdout)

    def test_advance_slice_recovers_clean_exhausted_pre_review_checkpoint(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = WorkGraphFixture(Path(tmp), profile="claim-grade")
            recipe = json.loads(fixture.recipe.read_text(encoding="utf-8"))
            for node in recipe["nodes"]:
                if node["id"] == "implement":
                    node["max_attempts"] = 1
            fixture.recipe.write_text(
                json.dumps(recipe, indent=2) + "\n", encoding="utf-8"
            )
            subprocess.run(["git", "add", "."], cwd=fixture.root, check=True)
            subprocess.run(
                ["git", "commit", "-qm", "limit writer attempts"],
                cwd=fixture.root,
                check=True,
            )
            fixture._assert_success(fixture.claim())
            fixture._assert_success(fixture.start())
            fixture.complete("context")
            fixture.complete("plan")
            fixture._assert_success(
                fixture.work(
                    "note",
                    "--task-id",
                    fixture.task_id,
                    "--node",
                    "plan",
                    "--actor",
                    fixture.owner,
                    "--kind",
                    "decision",
                    "--text",
                    "Carry this decision into the next declared slice.",
                )
            )
            fixture.complete("implement")
            before_result, before = fixture.show()
            fixture._assert_success(before_result)

            (fixture.root / "README.md").write_text(
                "committed post-writer epilogue\n", encoding="utf-8"
            )
            subprocess.run(["git", "add", "."], cwd=fixture.root, check=True)
            subprocess.run(
                ["git", "commit", "-qm", "post-writer epilogue"],
                cwd=fixture.root,
                check=True,
            )
            head = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=fixture.root,
                text=True,
                stdout=subprocess.PIPE,
                check=True,
            ).stdout.strip()
            common = subprocess.run(
                ["git", "rev-parse", "--path-format=absolute", "--git-common-dir"],
                cwd=fixture.root,
                text=True,
                stdout=subprocess.PIPE,
                check=True,
            ).stdout.strip()
            state_path = Path(common) / "intrinsic-agent-work-graphs/v1/PROC-999.json"
            legacy_state = json.loads(state_path.read_text(encoding="utf-8"))
            legacy_state["source"].pop("slice_index")
            state_path.write_text(
                json.dumps(legacy_state, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            legacy_result, legacy_status = fixture.show()

            missing_ack = fixture.work(
                "advance-slice",
                "--task-id",
                fixture.task_id,
                "--owner",
                fixture.owner,
                "--from-node",
                "plan",
                "--reason",
                "begin the next declared slice",
            )
            missing_checkpoint_ack = fixture.work(
                "advance-slice",
                "--task-id",
                fixture.task_id,
                "--owner",
                fixture.owner,
                "--from-node",
                "plan",
                "--reason",
                "begin the next declared slice",
                "--accept-stale-source",
            )
            advanced = fixture.work(
                "advance-slice",
                "--task-id",
                fixture.task_id,
                "--owner",
                fixture.owner,
                "--from-node",
                "plan",
                "--reason",
                "begin the next declared slice",
                "--accept-pre-review-checkpoint",
                "--accept-stale-source",
            )
            fixture._assert_success(advanced)
            result, state = fixture.show()
            fixture._assert_success(result)
            restarted = fixture.begin("plan")

            event_path = (
                Path(common) / "intrinsic-agent-work-graphs/v1/PROC-999.events.jsonl"
            )
            event_lines = event_path.read_text(encoding="utf-8").splitlines()
            events = [json.loads(line) for line in event_lines]

        self.assertEqual(missing_ack.returncode, 2, missing_ack.stdout)
        self.assertIn("--accept-stale-source", missing_ack.stdout)
        self.assertEqual(legacy_result.returncode, 1, legacy_result.stdout)
        self.assertEqual(legacy_status["source"]["slice_index"], 1)
        self.assertEqual(
            missing_checkpoint_ack.returncode, 2, missing_checkpoint_ack.stdout
        )
        self.assertIn(
            "--accept-pre-review-checkpoint", missing_checkpoint_ack.stdout
        )
        self.assertEqual(state["run_id"], before["run_id"])
        self.assertEqual(state["source"]["slice_index"], 2)
        self.assertEqual(state["source"]["base_revision"], head)
        self.assertEqual(state["source"]["head_revision_at_start"], head)
        self.assertEqual(state["source"]["surface_at_start"], [])
        self.assertEqual(
            state["source"]["content_digest_at_start"],
            hashlib.sha256(b"[]").hexdigest(),
        )
        self.assertIsNone(state["source"]["review_content_digest"])
        self.assertIsNone(state["source"]["final_content_digest"])
        self.assertEqual(state["nodes"]["context"]["status"], "succeeded")
        self.assertEqual(state["nodes"]["plan"]["status"], "ready")
        self.assertEqual(state["nodes"]["plan"]["attempts"], 0)
        self.assertEqual(state["nodes"]["plan"]["notes_count"], 1)
        self.assertEqual(state["nodes"]["implement"]["status"], "pending")
        self.assertEqual(state["nodes"]["implement"]["attempts"], 0)
        self.assertEqual(restarted.returncode, 0, restarted.stdout)
        self.assertIn("attempt 1", restarted.stdout)
        event = events[-2]
        self.assertEqual(event["event"], "slice_advanced")
        self.assertEqual(event["slice_index"], 2)
        self.assertEqual(event["prior_slice_index"], 1)
        self.assertEqual(event["prior_disposition"], "rolled-forward-before-review")
        self.assertTrue(event["accepted_pre_review_checkpoint"])
        self.assertEqual(
            event["accepted_stale_reasons"],
            ["review source binding is stale"],
        )
        self.assertEqual(event["prior_nodes"]["implement"]["attempts"], 1)
        self.assertEqual(event["prior_nodes"]["implement"]["status"], "succeeded")
        self.assertEqual(
            event["prior_source"]["review_content_digest"],
            before["source"]["review_content_digest"],
        )
        for index, line in enumerate(event_lines):
            record = json.loads(line)
            self.assertEqual(record["sequence"], index + 1)
            expected = (
                None
                if index == 0
                else hashlib.sha256(event_lines[index - 1].encode("utf-8")).hexdigest()
            )
            self.assertEqual(record["previous_entry_sha256"], expected)

    def test_advance_slice_restarts_a_fully_reviewed_cycle(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = WorkGraphFixture(Path(tmp))
            fixture._assert_success(fixture.claim())
            fixture._assert_success(fixture.start())
            subprocess.run(["git", "add", "."], cwd=fixture.root, check=True)
            subprocess.run(
                ["git", "commit", "-qm", "bind task claim"],
                cwd=fixture.root,
                check=True,
            )
            fixture.progress_to_join()
            fixture.complete("finalize")
            before_result, before = fixture.show()
            fixture._assert_success(before_result)
            human_before = fixture.work("show", "--task-id", fixture.task_id)

            advanced = fixture.work(
                "advance-slice",
                "--task-id",
                fixture.task_id,
                "--owner",
                fixture.owner,
                "--from-node",
                "plan",
                "--reason",
                "begin the second reviewed slice",
            )
            fixture._assert_success(advanced)
            result, state = fixture.show()
            fixture._assert_success(result)
            human_after = fixture.work("show", "--task-id", fixture.task_id)

            common = subprocess.run(
                ["git", "rev-parse", "--path-format=absolute", "--git-common-dir"],
                cwd=fixture.root,
                text=True,
                stdout=subprocess.PIPE,
                check=True,
            ).stdout.strip()
            event_path = (
                Path(common) / "intrinsic-agent-work-graphs/v1/PROC-999.events.jsonl"
            )
            event = json.loads(
                event_path.read_text(encoding="utf-8").splitlines()[-1]
            )

        self.assertEqual(before["derived"]["status"], "succeeded")
        self.assertIn("slice=1", human_before.stdout)
        self.assertIn("slice=2", human_after.stdout)
        self.assertEqual(state["source"]["slice_index"], 2)
        self.assertEqual(state["nodes"]["context"]["attempts"], 1)
        self.assertEqual(state["nodes"]["context"]["status"], "succeeded")
        self.assertEqual(state["nodes"]["plan"]["attempts"], 0)
        self.assertEqual(state["nodes"]["plan"]["status"], "ready")
        self.assertEqual(event["event"], "slice_advanced")
        self.assertEqual(event["prior_disposition"], "reviewed")
        self.assertFalse(event["accepted_pre_review_checkpoint"])
        self.assertEqual(event["accepted_stale_reasons"], [])
        self.assertEqual(event["prior_nodes"]["finalize"]["status"], "succeeded")

    def test_advance_slice_records_surplus_recovery_flags_as_unused(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = WorkGraphFixture(Path(tmp))
            fixture._assert_success(fixture.claim())
            fixture._assert_success(fixture.start())
            subprocess.run(["git", "add", "."], cwd=fixture.root, check=True)
            subprocess.run(
                ["git", "commit", "-qm", "bind task claim"],
                cwd=fixture.root,
                check=True,
            )
            fixture.progress_to_join()
            fixture.complete("finalize")

            advanced = fixture.work(
                "advance-slice",
                "--task-id",
                fixture.task_id,
                "--owner",
                fixture.owner,
                "--from-node",
                "plan",
                "--reason",
                "surplus recovery flags must not falsify the event",
                "--accept-pre-review-checkpoint",
                "--accept-stale-source",
            )
            fixture._assert_success(advanced)

            common = subprocess.run(
                ["git", "rev-parse", "--path-format=absolute", "--git-common-dir"],
                cwd=fixture.root,
                text=True,
                stdout=subprocess.PIPE,
                check=True,
            ).stdout.strip()
            event_path = (
                Path(common) / "intrinsic-agent-work-graphs/v1/PROC-999.events.jsonl"
            )
            event = json.loads(
                event_path.read_text(encoding="utf-8").splitlines()[-1]
            )

        self.assertEqual(event["prior_disposition"], "reviewed")
        self.assertFalse(event["accepted_pre_review_checkpoint"])
        self.assertEqual(event["accepted_stale_reasons"], [])

    def test_advance_slice_rejects_unsafe_cycle_states(self) -> None:
        def prepare(root: Path) -> WorkGraphFixture:
            root.mkdir()
            fixture = WorkGraphFixture(root)
            fixture._assert_success(fixture.claim())
            fixture._assert_success(fixture.start())
            subprocess.run(["git", "add", "."], cwd=root, check=True)
            subprocess.run(
                ["git", "commit", "-qm", "bind task claim"], cwd=root, check=True
            )
            fixture.complete("context")
            fixture.complete("plan")
            return fixture

        def advance(fixture: WorkGraphFixture, from_node: str = "plan"):
            return fixture.work(
                "advance-slice",
                "--task-id",
                fixture.task_id,
                "--owner",
                fixture.owner,
                "--from-node",
                from_node,
                "--reason",
                "unsafe fixture must fail closed",
                "--accept-pre-review-checkpoint",
                "--accept-stale-source",
            )

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)

            running = prepare(root / "running")
            running._assert_success(running.begin("implement"))
            running_result = advance(running)

            blocked = prepare(root / "blocked")
            blocked._assert_success(blocked.begin("implement"))
            blocked._assert_success(blocked.finish("implement", "blocked"))
            blocked_result = advance(blocked)

            failed = prepare(root / "failed")
            failed._assert_success(failed.begin("implement"))
            failed._assert_success(failed.finish("implement", "failed"))
            failed_result = advance(failed)

            partial = prepare(root / "partial")
            partial.complete("implement")
            partial.complete("freeze_diff")
            partial_result = advance(partial)

            undersized = prepare(root / "undersized")
            undersized.complete("implement")
            undersized_result = advance(undersized, "freeze_diff")

            dirty = prepare(root / "dirty")
            dirty.complete("implement")
            (dirty.root / "README.md").write_text(
                "uncommitted slice bytes\n", encoding="utf-8"
            )
            dirty_result = advance(dirty)

        self.assertEqual(running_result.returncode, 2, running_result.stdout)
        self.assertIn("nodes are running", running_result.stdout)
        self.assertEqual(blocked_result.returncode, 2, blocked_result.stdout)
        self.assertIn("failed or blocked cycle", blocked_result.stdout)
        self.assertEqual(failed_result.returncode, 2, failed_result.stdout)
        self.assertIn("failed or blocked cycle", failed_result.stdout)
        self.assertEqual(partial_result.returncode, 2, partial_result.stdout)
        self.assertIn("started review descendants", partial_result.stdout)
        self.assertEqual(undersized_result.returncode, 2, undersized_result.stdout)
        self.assertIn("every active writer", undersized_result.stdout)
        self.assertEqual(dirty_result.returncode, 2, dirty_result.stdout)
        self.assertIn("clean worktree", dirty_result.stdout)

    def test_advance_slice_rejects_task_claim_and_recipe_drift(self) -> None:
        def prepare(root: Path) -> WorkGraphFixture:
            root.mkdir()
            fixture = WorkGraphFixture(root)
            fixture._assert_success(fixture.claim())
            fixture._assert_success(fixture.start())
            subprocess.run(["git", "add", "."], cwd=root, check=True)
            subprocess.run(
                ["git", "commit", "-qm", "bind task claim"], cwd=root, check=True
            )
            fixture.complete("context")
            fixture.complete("plan")
            fixture.complete("implement")
            return fixture

        def advance(fixture: WorkGraphFixture, owner: str | None = None):
            return fixture.work(
                "advance-slice",
                "--task-id",
                fixture.task_id,
                "--owner",
                owner or fixture.owner,
                "--from-node",
                "plan",
                "--reason",
                "invalid custody must fail closed",
                "--accept-pre-review-checkpoint",
                "--accept-stale-source",
            )

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)

            wrong_owner = prepare(root / "wrong-owner")
            wrong_owner_result = advance(wrong_owner, "impostor")

            claim_drift = prepare(root / "claim-drift")
            claim_drift._assert_success(claim_drift.release())
            claim_drift._assert_success(claim_drift.claim())
            claim_drift_result = advance(claim_drift)

            recipe_drift = prepare(root / "recipe-drift")
            recipe = json.loads(recipe_drift.recipe.read_text(encoding="utf-8"))
            recipe["description"] += " drift"
            recipe_drift.recipe.write_text(
                json.dumps(recipe, indent=2) + "\n", encoding="utf-8"
            )
            subprocess.run(
                ["git", "add", "."], cwd=recipe_drift.root, check=True
            )
            subprocess.run(
                ["git", "commit", "-qm", "drift recipe"],
                cwd=recipe_drift.root,
                check=True,
            )
            recipe_drift_result = advance(recipe_drift)

            missing_plan = prepare(root / "missing-plan")
            task_path = missing_plan.root / "tasks/active/PROC-999-fixture.md"
            task_path.write_text(
                task_path.read_text(encoding="utf-8").replace(
                    "## Slice plan\n"
                    "- Slice 1 establishes the fixture state.\n"
                    "- Slice 2 exercises an explicitly bounded continuation.\n\n",
                    "",
                ),
                encoding="utf-8",
            )
            subprocess.run(["git", "add", "."], cwd=missing_plan.root, check=True)
            subprocess.run(
                ["git", "commit", "-qm", "remove slice plan"],
                cwd=missing_plan.root,
                check=True,
            )
            missing_plan_result = advance(missing_plan)

            inactive = prepare(root / "inactive")
            backlog = inactive.root / "tasks/backlog/general"
            backlog.mkdir(parents=True)
            (inactive.root / "tasks/active/PROC-999-fixture.md").rename(
                backlog / "PROC-999-fixture.md"
            )
            subprocess.run(["git", "add", "."], cwd=inactive.root, check=True)
            subprocess.run(
                ["git", "commit", "-qm", "move task to backlog"],
                cwd=inactive.root,
                check=True,
            )
            inactive_result = advance(inactive)

            done = prepare(root / "done")
            done_dir = done.root / "tasks/done"
            done_dir.mkdir(parents=True)
            (done.root / "tasks/active/PROC-999-fixture.md").rename(
                done_dir / "PROC-999-fixture.md"
            )
            subprocess.run(["git", "add", "."], cwd=done.root, check=True)
            subprocess.run(
                ["git", "commit", "-qm", "move task to done"],
                cwd=done.root,
                check=True,
            )
            done_result = advance(done)

            profile_drift = prepare(root / "profile-drift")
            profile_task = (
                profile_drift.root / "tasks/active/PROC-999-fixture.md"
            )
            profile_task.write_text(
                profile_task.read_text(encoding="utf-8").replace(
                    "workflow_profile: standard",
                    "workflow_profile: claim-grade",
                ),
                encoding="utf-8",
            )
            subprocess.run(
                ["git", "add", "."], cwd=profile_drift.root, check=True
            )
            subprocess.run(
                ["git", "commit", "-qm", "drift workflow profile"],
                cwd=profile_drift.root,
                check=True,
            )
            profile_drift_result = advance(profile_drift)

        self.assertEqual(wrong_owner_result.returncode, 2, wrong_owner_result.stdout)
        self.assertIn("does not match", wrong_owner_result.stdout)
        self.assertEqual(claim_drift_result.returncode, 2, claim_drift_result.stdout)
        self.assertIn("claim binding changed", claim_drift_result.stdout)
        self.assertEqual(recipe_drift_result.returncode, 2, recipe_drift_result.stdout)
        self.assertIn("recipe changed", recipe_drift_result.stdout)
        self.assertEqual(missing_plan_result.returncode, 2, missing_plan_result.stdout)
        self.assertIn("## Slice plan", missing_plan_result.stdout)
        self.assertEqual(inactive_result.returncode, 2, inactive_result.stdout)
        self.assertIn("tasks/active/", inactive_result.stdout)
        self.assertEqual(done_result.returncode, 2, done_result.stdout)
        self.assertIn("tasks/active/", done_result.stdout)
        self.assertEqual(
            profile_drift_result.returncode, 2, profile_drift_result.stdout
        )
        self.assertIn("workflow profile changed", profile_drift_result.stdout)

    def test_review_surface_change_blocks_finalize_until_writer_reopens(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = WorkGraphFixture(Path(tmp))
            fixture._assert_success(fixture.claim())
            fixture._assert_success(fixture.start())
            fixture.progress_to_join()
            (fixture.root / "README.md").write_text(
                "changed after review\n", encoding="utf-8"
            )
            stale_result, stale_state = fixture.show()
            rejected = fixture.begin("finalize")
            reopened = fixture.work(
                "reopen",
                "--task-id",
                fixture.task_id,
                "--node",
                "implement",
                "--actor",
                fixture.owner,
                "--reason",
                "source changed after review",
            )
            fixture._assert_success(reopened)
            result, state = fixture.show()
        self.assertEqual(stale_result.returncode, 1, stale_result.stdout)
        self.assertIn(
            "review source binding is stale",
            stale_state["derived"]["stale_reasons"],
        )
        self.assertEqual(rejected.returncode, 2, rejected.stdout)
        self.assertIn("review source binding is stale", rejected.stdout)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(state["nodes"]["implement"]["status"], "ready")
        self.assertIsNone(state["source"]["review_content_digest"])

    def test_terminal_surface_binding_becomes_stale_after_change(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = WorkGraphFixture(Path(tmp))
            fixture._assert_success(fixture.claim())
            fixture._assert_success(fixture.start())
            fixture.progress_to_join()
            fixture.complete("finalize")
            complete_result, complete_state = fixture.show()
            (fixture.root / "README.md").write_text(
                "changed after finalization\n", encoding="utf-8"
            )
            stale_result, stale_state = fixture.show()
        self.assertEqual(complete_result.returncode, 0, complete_result.stdout)
        self.assertEqual(complete_state["derived"]["status"], "succeeded")
        self.assertEqual(
            complete_state["source"]["final_content_digest"],
            complete_state["source"]["review_content_digest"],
        )
        self.assertEqual(stale_result.returncode, 1, stale_result.stdout)
        self.assertEqual(stale_state["derived"]["status"], "stale")
        self.assertIn(
            "final source binding is stale", stale_state["derived"]["stale_reasons"]
        )

    def test_show_and_list_wait_for_projection_lock(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = WorkGraphFixture(Path(tmp))
            fixture._assert_success(fixture.claim())
            fixture._assert_success(fixture.start())
            common = subprocess.run(
                ["git", "rev-parse", "--path-format=absolute", "--git-common-dir"],
                cwd=fixture.root,
                text=True,
                stdout=subprocess.PIPE,
                check=True,
            ).stdout.strip()
            graph_root = Path(common) / "intrinsic-agent-work-graphs/v1"
            lock = graph_root / ".lock"

            commands = [
                [
                    sys.executable,
                    str(TOOL),
                    "show",
                    "--root",
                    str(fixture.root),
                    "--task-id",
                    fixture.task_id,
                    "--json",
                ],
                [
                    sys.executable,
                    str(TOOL),
                    "list",
                    "--root",
                    str(fixture.root),
                    "--json",
                ],
            ]
            outputs: list[str] = []
            for command in commands:
                lock.mkdir()
                process = subprocess.Popen(
                    command,
                    cwd=fixture.root,
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                )
                time.sleep(0.25)
                self.assertIsNone(process.poll(), "reader bypassed projection lock")
                lock.rmdir()
                output, _ = process.communicate(timeout=5)
                self.assertEqual(process.returncode, 0, output)
                outputs.append(output)
        self.assertEqual(json.loads(outputs[0])["task_id"], fixture.task_id)
        self.assertEqual(json.loads(outputs[1])[0]["task_id"], fixture.task_id)

    def _graph_root(self, fixture: WorkGraphFixture) -> Path:
        common = subprocess.run(
            ["git", "rev-parse", "--path-format=absolute", "--git-common-dir"],
            cwd=fixture.root,
            text=True,
            stdout=subprocess.PIPE,
            check=True,
        ).stdout.strip()
        return Path(common) / "intrinsic-agent-work-graphs/v1"

    def test_waiter_does_not_break_lock_held_by_a_live_slow_holder(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = WorkGraphFixture(Path(tmp))
            fixture._assert_success(fixture.claim())
            fixture._assert_success(fixture.start())
            graph_root = self._graph_root(fixture)
            lock = graph_root / ".lock"
            lock.mkdir()
            # A holder that is alive but has been working far longer than any
            # elapsed-time threshold would tolerate.
            (lock / "owner.json").write_text(
                json.dumps(
                    {
                        "token": "live-holder",
                        "pid": os.getpid(),
                        "host": socket.gethostname(),
                        "acquired_at": time.time() - 3600.0,
                    },
                    sort_keys=True,
                ),
                encoding="utf-8",
            )
            # Backdate the directory mtime so an elapsed-time breaker would
            # fire. Only a liveness check can tell this lock is still held.
            stale = time.time() - 3600.0
            os.utime(lock, (stale, stale))

            result = invoke(
                fixture.root,
                TOOL,
                "show",
                "--root",
                str(fixture.root),
                "--task-id",
                fixture.task_id,
            )

            self.assertEqual(result.returncode, 2, result.stdout)
            self.assertIn("timed out waiting for the work-graph lock", result.stdout)
            self.assertTrue(lock.is_dir(), "waiter stole a live holder's lock")
            self.assertEqual(
                json.loads((lock / "owner.json").read_text(encoding="utf-8"))["token"],
                "live-holder",
            )

    def test_waiter_breaks_lock_whose_holder_is_provably_gone(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = WorkGraphFixture(Path(tmp))
            fixture._assert_success(fixture.claim())
            fixture._assert_success(fixture.start())
            graph_root = self._graph_root(fixture)
            lock = graph_root / ".lock"
            lock.mkdir()
            dead_pid = subprocess.run(
                [sys.executable, "-c", "import os; print(os.getpid())"],
                text=True,
                stdout=subprocess.PIPE,
                check=True,
            ).stdout.strip()
            (lock / "owner.json").write_text(
                json.dumps(
                    {
                        "token": "dead-holder",
                        "pid": int(dead_pid),
                        "host": socket.gethostname(),
                        "acquired_at": time.time(),
                    },
                    sort_keys=True,
                ),
                encoding="utf-8",
            )

            result = invoke(
                fixture.root,
                TOOL,
                "show",
                "--root",
                str(fixture.root),
                "--task-id",
                fixture.task_id,
            )

            self.assertEqual(result.returncode, 0, result.stdout)

    def test_broken_lock_victim_does_not_delete_the_successor_lock(self) -> None:
        sys.path.insert(0, str(REPOSITORY / "tools/agents"))
        import agent_work_graph  # noqa: PLC0415

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "graphs"
            lock = root / ".lock"
            owner = lock / "owner.json"

            with self.assertRaises(agent_work_graph.WorkGraphError) as caught:
                with agent_work_graph._state_lock(root):
                    # Simulate this holder being broken and a successor taking
                    # the lock while the critical section is still running.
                    owner.write_text(
                        json.dumps({"token": "successor"}, sort_keys=True),
                        encoding="utf-8",
                    )

            self.assertIn("taken over by another holder", str(caught.exception))
            self.assertTrue(lock.is_dir(), "victim deleted the successor's lock")
            self.assertEqual(
                json.loads(owner.read_text(encoding="utf-8"))["token"], "successor"
            )

    def test_lock_release_does_not_mask_an_error_from_the_critical_section(self) -> None:
        sys.path.insert(0, str(REPOSITORY / "tools/agents"))
        import agent_work_graph  # noqa: PLC0415

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "graphs"
            owner = root / ".lock" / "owner.json"

            with self.assertRaises(RuntimeError) as caught:
                with agent_work_graph._state_lock(root):
                    owner.write_text(
                        json.dumps({"token": "successor"}, sort_keys=True),
                        encoding="utf-8",
                    )
                    raise RuntimeError("original failure")

            self.assertEqual(str(caught.exception), "original failure")

    def test_success_is_immutable_and_inspectable_after_claim_release(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = WorkGraphFixture(Path(tmp))
            fixture._assert_success(fixture.claim())
            fixture._assert_success(fixture.start())
            fixture.progress_to_join()
            fixture.complete("finalize")
            note = fixture.work(
                "note",
                "--task-id",
                fixture.task_id,
                "--node",
                "finalize",
                "--actor",
                fixture.owner,
                "--kind",
                "finding",
                "--text",
                "late mutation",
            )
            reopened = fixture.work(
                "reopen",
                "--task-id",
                fixture.task_id,
                "--node",
                "finalize",
                "--actor",
                fixture.owner,
                "--reason",
                "late mutation",
            )
            aborted = fixture.work(
                "abort",
                "--task-id",
                fixture.task_id,
                "--actor",
                fixture.owner,
                "--reason",
                "late mutation",
            )
            fixture._assert_success(fixture.release())
            result, state = fixture.show()
        self.assertEqual(note.returncode, 2, note.stdout)
        self.assertIn("succeeded work graphs cannot receive notes", note.stdout)
        self.assertEqual(reopened.returncode, 2, reopened.stdout)
        self.assertIn("succeeded work graphs cannot be reopened", reopened.stdout)
        self.assertEqual(aborted.returncode, 2, aborted.stdout)
        self.assertIn("work graph is already succeeded", aborted.stdout)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(state["derived"]["status"], "succeeded")

    def test_only_claimed_owner_can_abort(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = WorkGraphFixture(Path(tmp))
            fixture._assert_success(fixture.claim())
            fixture._assert_success(fixture.start())
            rejected = fixture.work(
                "abort",
                "--task-id",
                fixture.task_id,
                "--actor",
                "reviewer",
                "--reason",
                "not authorized",
            )
            aborted = fixture.work(
                "abort",
                "--task-id",
                fixture.task_id,
                "--actor",
                fixture.owner,
                "--reason",
                "fixture complete",
            )
            result, state = fixture.show()
        self.assertEqual(rejected.returncode, 2, rejected.stdout)
        self.assertEqual(aborted.returncode, 0, aborted.stdout)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(state["derived"]["status"], "aborted")

    def test_prior_owner_cannot_abort_after_task_is_reclaimed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = WorkGraphFixture(Path(tmp))
            fixture._assert_success(fixture.claim())
            fixture._assert_success(fixture.start())
            fixture._assert_success(fixture.release())
            fixture._assert_success(fixture.claim("replacement"))
            rejected = fixture.work(
                "abort",
                "--task-id",
                fixture.task_id,
                "--actor",
                fixture.owner,
                "--reason",
                "prior owner attempt",
            )
        self.assertEqual(rejected.returncode, 2, rejected.stdout)
        self.assertIn("no longer holds the live task claim", rejected.stdout)


if __name__ == "__main__":
    unittest.main()
