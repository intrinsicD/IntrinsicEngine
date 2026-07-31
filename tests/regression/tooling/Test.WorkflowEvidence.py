#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parents[3]
TOOL = REPO_ROOT / "tools" / "agents" / "workflow_evidence.py"


def run(
    repo: Path, *args: str, expected: int | None = None
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [sys.executable, str(TOOL), *args],
        cwd=repo,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if expected is not None and result.returncode != expected:
        raise AssertionError(
            f"command returned {result.returncode}, expected {expected}\n{result.stdout}"
        )
    return result


def git(repo: Path, *args: str) -> None:
    subprocess.run(
        ["git", *args],
        cwd=repo,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )


def task_text(repo: Path, profile: str, checked: bool) -> str:
    mark = "x" if checked else " "
    return f"""---
id: TEST-001
theme: none
depends_on: []
workflow_schema: 1
workflow_profile: {profile}
evidence: required
owner: driver
branch: test/workflow
worktree: {repo}
claimed_at: "2026-07-29T12:00:00Z"
---
# TEST-001 — Workflow evidence fixture

## Goal
- Exercise workflow evidence.

## Non-goals
- No product behavior.

## Context
- Tooling fixture.

## Required changes
- [{mark}] Generate evidence.

## Tests
- [{mark}] Run the validator.

## Docs
- [{mark}] Keep the fixture self-describing.

## Acceptance criteria
- [{mark}] Evidence is complete.

## Verification
```bash
true
```

## Forbidden changes
- No unrelated work.
"""


class Fixture:
    def __init__(self, root: Path, profile: str = "standard", checked: bool = True):
        self.repo = root
        git(root, "init", "-q")
        git(root, "config", "user.email", "fixture@example.invalid")
        git(root, "config", "user.name", "Fixture")
        self.task = root / "tasks/active/TEST-001-fixture.md"
        self.task.parent.mkdir(parents=True)
        self.task.write_text(task_text(root, profile, False), encoding="utf-8")
        git(root, "add", ".")
        git(root, "commit", "-qm", "base")
        self.base = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=root,
            text=True,
            stdout=subprocess.PIPE,
            check=True,
        ).stdout.strip()
        self.task.write_text(task_text(root, profile, checked), encoding="utf-8")
        self.artifact = root / "artifact.txt"
        self.artifact.write_text("evidence\n", encoding="utf-8")
        self.profile = profile

    @property
    def report(self) -> Path:
        return self.repo / "tasks/evidence/TEST-001/report.yaml"

    def receipt(self, label: str = "ok", executable: str = "/bin/true") -> int:
        return run(
            self.repo,
            "record-command",
            "--root",
            str(self.repo),
            "--task-id",
            "TEST-001",
            "--label",
            label,
            "--",
            executable,
        ).returncode

    def generate(
        self,
        *,
        complete: bool = True,
        replace: bool = False,
        skip: str | None = None,
        artifact: str = "artifact.txt",
        expected: int | None = 0,
    ) -> subprocess.CompletedProcess[str]:
        command = [
            "generate-report",
            "--root",
            str(self.repo),
            "--task-id",
            "TEST-001",
            "--base-ref",
            self.base,
            "--extension-seam",
            "Plain schema-versioned files",
            "--future-change-plan",
            "Add fields only through schema migration",
            "--artifact",
            artifact,
            "--self-review-complete",
        ]
        if complete:
            command.append("--complete")
        if replace:
            command.append("--replace")
        if skip:
            command.extend(["--skip", skip])
        return run(self.repo, *command, expected=expected)

    def validate(self) -> subprocess.CompletedProcess[str]:
        return run(
            self.repo,
            "validate",
            "--root",
            str(self.repo),
            "--require-complete",
            "TEST-001",
        )

    def digest(self) -> str:
        return self.report_data()["source"]["content_digest"]

    def report_data(self) -> dict:
        return yaml.safe_load(self.report.read_text(encoding="utf-8"))

    def revision(self) -> str:
        source = self.report_data()["source"]
        if source["dirty"]:
            return f"worktree:{source['content_digest']}"
        return source["head_revision"]

    def handoff(
        self,
        digest: str | None = None,
        revision: str | None = None,
        expected: int | None = 0,
    ) -> subprocess.CompletedProcess[str]:
        return run(
            self.repo,
            "append-handoff",
            "--root",
            str(self.repo),
            "--task-id",
            "TEST-001",
            "--driver",
            "driver",
            "--revision",
            revision or self.revision(),
            "--content-digest",
            digest or self.digest(),
            "--summary",
            "Implemented the fixture",
            "--changed-surface",
            "workflow evidence",
            "--evidence",
            "artifact.txt",
            "--next-action",
            "Review",
            expected=expected,
        )

    def review(
        self,
        *,
        digest: str | None = None,
        verdict: str = "accepted",
        reviewer: str = "reviewer",
        self_review: bool = False,
        revision: str | None = None,
    ) -> subprocess.CompletedProcess[str]:
        command = [
            "append-review",
            "--root",
            str(self.repo),
            "--task-id",
            "TEST-001",
            "--driver",
            "driver",
            "--reviewer",
            reviewer,
            "--revision",
            revision or self.revision(),
            "--content-digest",
            digest or self.digest(),
            "--verdict",
            verdict,
        ]
        if self_review:
            command.append("--self-review")
        return run(self.repo, *command)


class WorkflowEvidenceTests(unittest.TestCase):
    def fixture(
        self, tmp: str, profile: str = "standard", checked: bool = True
    ) -> Fixture:
        return Fixture(Path(tmp), profile=profile, checked=checked)

    def test_generated_standard_report_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp)
            self.assertEqual(fixture.receipt(), 0)
            fixture.generate()
            result = fixture.validate()
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_clean_fixed_revision_survives_later_unrelated_commit(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp)
            fixture.receipt()
            fixture.generate()
            git(fixture.repo, "add", ".")
            git(fixture.repo, "commit", "-qm", "implementation and initial evidence")

            fixture.generate(replace=True)
            report = yaml.safe_load(fixture.report.read_text(encoding="utf-8"))
            fixed_revision = report["source"]["head_revision"]
            self.assertFalse(report["source"]["dirty"])
            git(fixture.repo, "add", str(fixture.report))
            git(fixture.repo, "commit", "-qm", "bind fixed revision evidence")

            unrelated = fixture.repo / "unrelated.txt"
            unrelated.write_text("later task\n", encoding="utf-8")
            git(fixture.repo, "add", str(unrelated))
            git(fixture.repo, "commit", "-qm", "land unrelated task")
            current_revision = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=fixture.repo,
                text=True,
                stdout=subprocess.PIPE,
                check=True,
            ).stdout.strip()
            result = fixture.validate()

        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertNotEqual(current_revision, fixed_revision)

    def test_clean_fixed_revision_hashes_symlink_as_git_blob(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp, profile="high-risk")
            fixture.receipt()
            link = fixture.repo / "linked-artifact.txt"
            link.symlink_to("artifact.txt")
            git(fixture.repo, "add", ".")
            git(fixture.repo, "commit", "-qm", "add symlink source surface")
            fixture.generate()
            fixture.handoff()
            fixture.review()
            result = fixture.validate()
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_clean_fixed_revision_allows_contained_symlink_artifact(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp, profile="high-risk")
            fixture.receipt()
            link = fixture.repo / "linked-artifact.txt"
            link.symlink_to("artifact.txt")
            git(fixture.repo, "add", ".")
            git(fixture.repo, "commit", "-qm", "add contained symlink artifact")
            fixture.generate(artifact="linked-artifact.txt")
            fixture.handoff()
            fixture.review()
            report = fixture.report_data()
            result = fixture.validate()
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(
            report["artifacts"][0]["sha256"],
            hashlib.sha256(b"artifact.txt").hexdigest(),
        )

    def test_clean_fixed_revision_rejects_unsafe_symlink_artifacts(self) -> None:
        for link_target, expected in (
            ("../outside.txt", "artifact path escapes repository"),
            ("missing.txt", "artifact is missing or not a file"),
            ("unsafe-artifact", "artifact symlink cycle"),
        ):
            with (
                self.subTest(link_target=link_target),
                tempfile.TemporaryDirectory() as tmp,
            ):
                root = Path(tmp)
                repo = root / "repo"
                repo.mkdir()
                fixture = self.fixture(str(repo), profile="high-risk")
                fixture.receipt()
                (root / "outside.txt").write_text("outside\n", encoding="utf-8")
                link = fixture.repo / "unsafe-artifact"
                link.symlink_to(link_target)
                git(fixture.repo, "add", ".")
                git(fixture.repo, "commit", "-qm", "add unsafe artifact link")
                generated = fixture.generate(artifact=link.name, expected=None)
                self.assertNotEqual(generated.returncode, 0, generated.stdout)
                self.assertIn(expected, generated.stdout)
                fixture.generate()
                report = fixture.report_data()
                report["artifacts"] = [
                    {
                        "path": link.name,
                        "sha256": hashlib.sha256(
                            os.readlink(os.fsencode(link))
                        ).hexdigest(),
                        "kind": "file",
                    }
                ]
                fixture.report.write_text(
                    yaml.safe_dump(report, sort_keys=False), encoding="utf-8"
                )
                fixture.handoff()
                fixture.review()
                result = fixture.validate()
            self.assertEqual(result.returncode, 1, result.stdout)
            self.assertIn(expected, result.stdout)

    def test_dirty_report_rejects_unsafe_symlink_artifacts(self) -> None:
        for link_target, expected in (
            ("../outside.txt", "path escapes repository"),
            ("missing.txt", "artifact is not a file"),
            ("unsafe-artifact", "artifact symlink cycle"),
        ):
            with (
                self.subTest(link_target=link_target),
                tempfile.TemporaryDirectory() as tmp,
            ):
                root = Path(tmp)
                repo = root / "repo"
                repo.mkdir()
                fixture = self.fixture(str(repo), profile="high-risk")
                fixture.receipt()
                (root / "outside.txt").write_text("outside\n", encoding="utf-8")
                link = fixture.repo / "unsafe-artifact"
                link.symlink_to(link_target)
                generated = fixture.generate(artifact=link.name, expected=None)
                self.assertNotEqual(generated.returncode, 0, generated.stdout)
                self.assertIn(expected, generated.stdout)
                fixture.generate()
                report = fixture.report_data()
                self.assertTrue(report["source"]["dirty"])
                report["artifacts"] = [
                    {
                        "path": link.name,
                        "sha256": hashlib.sha256(
                            os.readlink(os.fsencode(link))
                        ).hexdigest(),
                        "kind": "file",
                    }
                ]
                fixture.report.write_text(
                    yaml.safe_dump(report, sort_keys=False), encoding="utf-8"
                )
                fixture.handoff()
                fixture.review()
                result = fixture.validate()
            self.assertEqual(result.returncode, 1, result.stdout)
            self.assertIn(expected, result.stdout)

    def test_clean_and_dirty_artifact_resolution_preserve_component_order(
        self,
    ) -> None:
        for clean in (False, True):
            with self.subTest(clean=clean), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                repo = root / "repo"
                repo.mkdir()
                external = root / "external"
                (external / "pivot").mkdir(parents=True)
                (external / "outside.txt").write_text("external\n", encoding="utf-8")
                fixture = self.fixture(str(repo), profile="high-risk")
                fixture.receipt()
                (repo / "outside.txt").write_text("decoy\n", encoding="utf-8")
                (repo / "pivot").symlink_to(external / "pivot")
                artifact = repo / "ordered-artifact"
                artifact.symlink_to("pivot/../outside.txt")
                if clean:
                    git(repo, "add", ".")
                    git(repo, "commit", "-qm", "freeze ordered artifact links")

                generated = fixture.generate(artifact=artifact.name, expected=None)
                self.assertNotEqual(generated.returncode, 0, generated.stdout)
                self.assertIn("artifact path escapes repository", generated.stdout)

                fixture.generate()
                report = fixture.report_data()
                self.assertEqual(report["source"]["dirty"], not clean)
                report["artifacts"] = [
                    {
                        "path": artifact.name,
                        "sha256": hashlib.sha256(
                            os.readlink(os.fsencode(artifact))
                        ).hexdigest(),
                        "kind": "file",
                    }
                ]
                fixture.report.write_text(
                    yaml.safe_dump(report, sort_keys=False), encoding="utf-8"
                )
                fixture.handoff()
                fixture.review()
                result = fixture.validate()
            self.assertEqual(result.returncode, 1, result.stdout)
            self.assertIn("artifact path escapes repository", result.stdout)

    def test_clean_fixed_revision_rejects_recorded_blob_hash_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp)
            fixture.receipt()
            fixture.generate()
            git(fixture.repo, "add", ".")
            git(fixture.repo, "commit", "-qm", "implementation and initial evidence")
            fixture.generate(replace=True)

            report = yaml.safe_load(fixture.report.read_text(encoding="utf-8"))
            report["source"]["surface"][0]["sha256"] = "0" * 64
            fixture.report.write_text(
                yaml.safe_dump(report, sort_keys=False), encoding="utf-8"
            )
            result = fixture.validate()

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("content hash mismatch", result.stdout)

    def test_clean_fixed_revision_artifact_survives_later_change(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp)
            fixture.receipt()
            fixture.generate()
            git(fixture.repo, "add", ".")
            git(fixture.repo, "commit", "-qm", "implementation and initial evidence")
            fixture.generate(replace=True)
            git(fixture.repo, "add", str(fixture.report))
            git(fixture.repo, "commit", "-qm", "bind fixed revision evidence")

            fixture.artifact.write_text("later task content\n", encoding="utf-8")
            git(fixture.repo, "add", str(fixture.artifact))
            git(fixture.repo, "commit", "-qm", "update shared artifact later")
            result = fixture.validate()

        self.assertEqual(result.returncode, 0, result.stdout)

    def test_clean_fixed_revision_rejects_recorded_artifact_hash_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp)
            fixture.receipt()
            fixture.generate()
            git(fixture.repo, "add", ".")
            git(fixture.repo, "commit", "-qm", "implementation and initial evidence")
            fixture.generate(replace=True)

            report = yaml.safe_load(fixture.report.read_text(encoding="utf-8"))
            report["artifacts"][0]["sha256"] = "0" * 64
            fixture.report.write_text(
                yaml.safe_dump(report, sort_keys=False), encoding="utf-8"
            )
            result = fixture.validate()

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("artifact hash mismatch", result.stdout)

    def test_dirty_report_remains_bound_to_current_worktree(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp)
            fixture.receipt()
            fixture.generate()
            unrelated = fixture.repo / "unrelated.txt"
            unrelated.write_text("later worktree change\n", encoding="utf-8")
            result = fixture.validate()

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("current worktree diff", result.stdout)

    def test_duplicate_yaml_key_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp)
            fixture.receipt()
            fixture.generate()
            fixture.report.write_text(
                fixture.report.read_text(encoding="utf-8") + "task_id: OTHER-001\n",
                encoding="utf-8",
            )
            result = fixture.validate()
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("duplicate key", result.stdout)

    def test_task_id_mismatch_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp)
            fixture.receipt()
            fixture.generate()
            report = yaml.safe_load(fixture.report.read_text(encoding="utf-8"))
            report["task_id"] = "OTHER-001"
            fixture.report.write_text(
                yaml.safe_dump(report, sort_keys=False), encoding="utf-8"
            )
            result = fixture.validate()
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("task_id does not match task", result.stdout)

    def test_unaddressed_acceptance_criterion_blocks_completion(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp, checked=False)
            fixture.receipt()
            fixture.generate()
            result = fixture.validate()
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("leaves criterion unresolved", result.stdout)

    def test_missing_required_receipt_blocks_completion(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp)
            fixture.generate()
            result = fixture.validate()
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("needs a required command receipt", result.stdout)

    def test_failed_required_receipt_blocks_completion(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp)
            self.assertEqual(fixture.receipt("failed", "/bin/false"), 1)
            fixture.generate()
            result = fixture.validate()
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("required command failed", result.stdout)

    def test_artifact_hash_mismatch_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp)
            fixture.receipt()
            fixture.generate()
            fixture.artifact.write_text("tampered\n", encoding="utf-8")
            result = fixture.validate()
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("artifact hash mismatch", result.stdout)

    def test_artifact_kind_must_be_file(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp)
            fixture.receipt()
            fixture.generate()
            report = fixture.report_data()
            report["artifacts"][0]["kind"] = "symlink"
            fixture.report.write_text(
                yaml.safe_dump(report, sort_keys=False), encoding="utf-8"
            )
            result = fixture.validate()
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("artifact kind must equal file", result.stdout)

    def test_unjustified_skip_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp)
            fixture.receipt()
            fixture.generate(skip="optional-gpu=host has no GPU")
            report = yaml.safe_load(fixture.report.read_text(encoding="utf-8"))
            report["skipped_checks"][0]["reason"] = ""
            fixture.report.write_text(
                yaml.safe_dump(report, sort_keys=False), encoding="utf-8"
            )
            result = fixture.validate()
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("requires check and reason", result.stdout)

    def test_high_risk_requires_handoff_and_terminal_review(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp, profile="high-risk")
            fixture.receipt()
            fixture.generate()
            result = fixture.validate()
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("needs handoff", result.stdout)
        self.assertIn("needs review", result.stdout)

    def test_empty_surface_digest_is_still_recomputed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp, profile="high-risk")
            fixture.receipt()
            git(fixture.repo, "add", ".")
            git(fixture.repo, "commit", "-qm", "freeze empty review surface")
            fixture.base = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=fixture.repo,
                text=True,
                stdout=subprocess.PIPE,
                check=True,
            ).stdout.strip()
            fixture.generate()
            report = fixture.report_data()
            self.assertEqual(report["source"]["surface"], [])
            forged_digest = "f" * 64
            report["source"]["content_digest"] = forged_digest
            fixture.report.write_text(
                yaml.safe_dump(report, sort_keys=False), encoding="utf-8"
            )
            fixture.handoff(digest=forged_digest)
            fixture.review(digest=forged_digest)
            result = fixture.validate()
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("surface digest mismatch", result.stdout)

    def test_stale_review_digest_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp, profile="high-risk")
            fixture.receipt()
            fixture.generate()
            fixture.handoff()
            result = fixture.review(digest="0" * 64)
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("record content digest does not match", result.stdout)

    def test_append_rejects_revision_that_differs_from_report(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp, profile="high-risk")
            fixture.receipt()
            fixture.generate()
            handoff = fixture.handoff(revision="0" * 40, expected=None)
            fixture.handoff()
            review = fixture.review(revision="0" * 40)
        self.assertEqual(handoff.returncode, 2, handoff.stdout)
        self.assertEqual(review.returncode, 2, review.stdout)
        self.assertIn("does not match report source revision", handoff.stdout)
        self.assertIn("does not match report source revision", review.stdout)

    def test_validation_rejects_tampered_latest_record_revisions(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp, profile="high-risk")
            fixture.receipt()
            fixture.generate()
            fixture.handoff()
            fixture.review()
            handoff_path = fixture.report.parent / "handoff.jsonl"
            handoff = json.loads(handoff_path.read_text(encoding="utf-8"))
            handoff["revision"] = "0" * 40
            handoff_path.write_text(json.dumps(handoff) + "\n", encoding="utf-8")
            review_path = fixture.report.parent / "reviews.jsonl"
            review = json.loads(review_path.read_text(encoding="utf-8"))
            review["revision"] = "0" * 40
            review_path.write_text(json.dumps(review) + "\n", encoding="utf-8")
            result = fixture.validate()
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("handoff revision differs from report source", result.stdout)
        self.assertIn("review revision differs from report source", result.stdout)

    def test_claim_grade_and_protected_completion_require_custody(self) -> None:
        for profile in ("claim-grade", "protected"):
            with self.subTest(profile=profile), tempfile.TemporaryDirectory() as tmp:
                fixture = self.fixture(tmp, profile=profile)
                fixture.receipt()
                fixture.generate()
                fixture.handoff()
                fixture.review()
                result = fixture.validate()
            self.assertEqual(result.returncode, 1, result.stdout)
            self.assertIn(
                "claim-grade completion requires experiment custody records",
                result.stdout,
            )

    def test_self_review_cannot_be_presented_as_acceptance(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp, profile="high-risk")
            fixture.receipt()
            fixture.generate()
            fixture.handoff()
            result = fixture.review(
                reviewer="driver", self_review=True, verdict="accepted"
            )
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("self-review is provisional", result.stdout)

    def test_revision_requested_is_not_terminal_acceptance(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp, profile="high-risk")
            fixture.receipt()
            fixture.generate()
            fixture.handoff()
            fixture.review(verdict="revision-requested")
            result = fixture.validate()
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("latest terminal review must be accepted", result.stdout)

    def test_valid_independent_high_risk_review_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp, profile="high-risk")
            fixture.receipt()
            fixture.generate()
            fixture.handoff()
            fixture.review()
            result = fixture.validate()
        self.assertEqual(result.returncode, 0, result.stdout)


if __name__ == "__main__":
    unittest.main()
