#!/usr/bin/env python3
from __future__ import annotations

import contextlib
import errno
import hashlib
import importlib.util
import io
import json
import os
import resource
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import yaml

REPO_ROOT = Path(__file__).resolve().parents[3]
TOOL = REPO_ROOT / "tools" / "agents" / "workflow_evidence.py"
CHECK = "test -f fixed.marker"
GOOD_REASON = "Missing module import fixed; the identical rerun passes."
sys.path.insert(0, str(TOOL.parent))  # raw ledger appends reuse _append_jsonl


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


class EvidenceRetentionTests(unittest.TestCase):
    def test_retired_missing_report_is_allowed_but_explicit_completion_is_not(self):
        with tempfile.TemporaryDirectory() as tmp:
            fixture = Fixture(Path(tmp))
            destination = fixture.repo / "tasks/done" / fixture.task.name
            destination.parent.mkdir()
            fixture.task.rename(destination)
            result = run(fixture.repo, "validate", "--root", str(fixture.repo))
            self.assertEqual(result.returncode, 0, result.stdout)
            required = fixture.validate()
            self.assertEqual(required.returncode, 1, required.stdout)
            self.assertIn("required completion report is missing", required.stdout)

    def test_clean_source_ignored_artifact_is_hash_checked_locally(self):
        with tempfile.TemporaryDirectory() as tmp:
            fixture = Fixture(Path(tmp))
            (fixture.repo / ".gitignore").write_text("/tasks/evidence/\n")
            git(fixture.repo, "add", ".")
            git(fixture.repo, "commit", "-qm", "source ready")
            self.assertEqual(fixture.receipt(), 0)
            artifact = fixture.repo / "tasks/evidence/TEST-001/result.txt"
            artifact.write_text("temporary result\n")
            fixture.generate(artifact=str(artifact.relative_to(fixture.repo)))
            self.assertFalse(fixture.report_data()["source"]["dirty"])
            result = fixture.validate()
            self.assertEqual(result.returncode, 0, result.stdout)
            artifact.write_text("changed result\n")
            result = fixture.validate()
            self.assertEqual(result.returncode, 1, result.stdout)
            self.assertIn("artifact hash mismatch", result.stdout)

    def test_clean_source_cannot_use_ignored_artifacts_outside_own_evidence(self):
        for name in ("build/result.txt", "tasks/evidence/OTHER-002/result.txt"):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as tmp:
                fixture = Fixture(Path(tmp))
                (fixture.repo / ".gitignore").write_text("/tasks/evidence/\n/build/\n")
                git(fixture.repo, "add", ".")
                git(fixture.repo, "commit", "-qm", "source ready")
                self.assertEqual(fixture.receipt(), 0)
                artifact = fixture.repo / name
                artifact.parent.mkdir(parents=True, exist_ok=True)
                artifact.write_text("not part of source or task evidence\n")
                result = fixture.generate(artifact=name, expected=None)
                self.assertNotEqual(result.returncode, 0, result.stdout)

    def test_existing_retired_draft_report_still_fails_completion(self):
        with tempfile.TemporaryDirectory() as tmp:
            fixture = Fixture(Path(tmp))
            fixture.receipt()
            fixture.generate(complete=False)
            destination = fixture.repo / "tasks/done" / fixture.task.name
            destination.parent.mkdir()
            fixture.task.rename(destination)
            result = run(fixture.repo, "validate", "--root", str(fixture.repo))
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn("complete", result.stdout)

    def test_later_ignore_rule_does_not_rebind_a_committed_artifact(self):
        with tempfile.TemporaryDirectory() as tmp:
            fixture = Fixture(Path(tmp))
            name = "tasks/evidence/TEST-001/legacy.txt"
            artifact = fixture.repo / name
            artifact.parent.mkdir(parents=True)
            artifact.write_text("original\n")
            (fixture.repo / ".gitignore").write_text(
                "/tasks/evidence/TEST-001/report.yaml\n/tasks/evidence/TEST-001/commands/\n"
            )
            git(fixture.repo, "add", ".")
            git(fixture.repo, "commit", "-qm", "source and historical artifact")
            fixture.receipt()
            fixture.generate(artifact=name)
            git(fixture.repo, "rm", name)
            (fixture.repo / ".gitignore").write_text("/tasks/evidence/\n")
            git(fixture.repo, "add", ".gitignore")
            git(fixture.repo, "commit", "-qm", "later evidence cleanup")
            artifact.write_text("new ignored bytes\n")
            result = fixture.validate()
            self.assertEqual(result.returncode, 0, result.stdout)

    def test_clean_source_rejects_ignored_symlink_artifact(self):
        with tempfile.TemporaryDirectory() as tmp:
            fixture = Fixture(Path(tmp))
            (fixture.repo / ".gitignore").write_text("/tasks/evidence/\n/build/\n")
            git(fixture.repo, "add", ".")
            git(fixture.repo, "commit", "-qm", "source ready")
            fixture.receipt()
            target = fixture.repo / "build/result.txt"
            target.parent.mkdir()
            target.write_text("unbound target bytes\n")
            artifact = fixture.repo / "tasks/evidence/TEST-001/result.txt"
            artifact.symlink_to("../../../build/result.txt")
            result = fixture.generate(artifact=str(artifact.relative_to(fixture.repo)), expected=None)
            self.assertNotEqual(result.returncode, 0, result.stdout)


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

    @property
    def commands(self) -> Path:
        return self.repo / "tasks/evidence/TEST-001/commands"

    @property
    def ledger(self) -> Path:
        return self.repo / "tasks/evidence/TEST-001/supersessions.jsonl"

    def receipt(
        self,
        label: str = "ok",
        executable: str = "/bin/true",
        *,
        shell: str | None = None,
        cwd: str | None = None,
        optional: bool = False,
    ) -> int:
        command = ["record-command", "--root", str(self.repo)]
        command += ["--task-id", "TEST-001", "--label", label, "--min-free-gib", "0"]
        if cwd:
            command += ["--cwd", str(self.repo / cwd)]
        if optional:
            command.append("--optional")
        command += ["--shell", shell] if shell else ["--", executable]
        return run(self.repo, *command).returncode

    def check(
        self, label: str, passing: bool, *, cwd: str | None = None, optional: bool = False
    ) -> int:
        """Record the same argv; the marker decides whether it passes."""
        marker = self.repo / (cwd or ".") / "fixed.marker"
        marker.parent.mkdir(exist_ok=True)
        if passing:
            marker.touch()
        else:
            marker.unlink(missing_ok=True)
        return self.receipt(label, shell=CHECK, cwd=cwd, optional=optional)

    def edit_receipt(self, label: str, **fields: object) -> None:
        path = self.commands / f"{label}.json"
        data = json.loads(path.read_text(encoding="utf-8"))
        data.update(fields)
        path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")

    def backdate(self, label: str) -> None:
        """Order receipts deterministically without sleeping across a second."""
        self.edit_receipt(
            label,
            started_at="2000-01-01T00:00:00Z",
            finished_at="2000-01-01T00:00:01Z",
        )

    def supersede(
        self, failed: str, replacement: str, reason: str = GOOD_REASON
    ) -> subprocess.CompletedProcess[str]:
        return run(
            self.repo,
            "supersede-command",
            "--root",
            str(self.repo),
            "--task-id",
            "TEST-001",
            "--failed",
            failed,
            "--replacement",
            replacement,
            "--reason",
            reason,
        )

    def raw_supersession(
        self, failed: str, replacement: str, reason: str = GOOD_REASON
    ) -> None:
        """Append without the CLI guard so validation is tested on its own."""
        record = {
            "schema_version": 1,
            "recorded_at": "2026-01-01T00:00:00Z",
            "failed": failed,
            "replacement": replacement,
            "reason": reason,
        }
        for role, label in (("failed", failed), ("replacement", replacement)):
            path = self.commands / f"{label}.json"
            record[f"{role}_sha256"] = (
                hashlib.sha256(path.read_bytes()).hexdigest()
                if path.is_file()
                else "0" * 64
            )
        import workflow_evidence

        workflow_evidence._append_jsonl(self.ledger, record)

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

    def seal(
        self, revision: str = "HEAD", expected: int | None = 0
    ) -> subprocess.CompletedProcess[str]:
        return run(
            self.repo,
            "seal-report",
            "--root",
            str(self.repo),
            "--task-id",
            "TEST-001",
            "--revision",
            revision,
            "--reason",
            "Freeze the completed fixture at its reviewed commit.",
            expected=expected,
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

    def test_retired_dirty_report_uses_post_commit_historical_seal(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp)
            fixture.receipt()
            done = fixture.repo / "tasks/done/TEST-001-fixture.md"
            done.parent.mkdir(parents=True)
            fixture.task.rename(done)
            fixture.task = done
            fixture.generate()
            git(fixture.repo, "add", ".")
            git(fixture.repo, "commit", "-qm", "retire dirty report fixture")

            missing_seal = fixture.validate()
            sealed = fixture.seal(expected=None)
            self.assertEqual(sealed.returncode, 0, sealed.stdout)
            seal_data = yaml.safe_load(
                (fixture.report.parent / "seal.yaml").read_text(encoding="utf-8")
            )
            report_digest = fixture.digest()
            git(fixture.repo, "add", ".")
            git(fixture.repo, "commit", "-qm", "seal retired report")

            fixture.artifact.write_text("later shared content\n", encoding="utf-8")
            git(fixture.repo, "add", str(fixture.artifact))
            git(fixture.repo, "commit", "-qm", "later task updates shared artifact")
            result = fixture.validate()

        self.assertEqual(missing_seal.returncode, 1, missing_seal.stdout)
        self.assertIn("historical seal", missing_seal.stdout)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertRegex(seal_data["sealed_revision"], r"^[0-9a-f]{40}$")
        self.assertEqual(seal_data["task_id"], "TEST-001")
        self.assertEqual(
            seal_data["source_content_digest"], report_digest
        )

    def test_historical_seal_survives_a_later_task_move(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp)
            fixture.receipt()
            fixture.generate()
            git(fixture.repo, "add", ".")
            git(fixture.repo, "commit", "-qm", "commit active dirty report")
            fixture.seal()
            git(fixture.repo, "add", ".")
            git(fixture.repo, "commit", "-qm", "seal active report")

            done = fixture.repo / "tasks/done/TEST-001-fixture.md"
            done.parent.mkdir(parents=True)
            fixture.task.rename(done)
            fixture.task = done
            git(fixture.repo, "add", ".")
            git(fixture.repo, "commit", "-qm", "retire task after sealing")
            result = fixture.validate()

        self.assertEqual(result.returncode, 0, result.stdout)

    def test_historical_seal_rejects_report_and_review_tampering(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = self.fixture(tmp, profile="high-risk")
            fixture.receipt()
            done = fixture.repo / "tasks/done/TEST-001-fixture.md"
            done.parent.mkdir(parents=True)
            fixture.task.rename(done)
            fixture.task = done
            fixture.generate()
            fixture.handoff()
            fixture.review()
            git(fixture.repo, "add", ".")
            git(fixture.repo, "commit", "-qm", "commit reviewed dirty report")
            fixture.seal()
            git(fixture.repo, "add", ".")
            git(fixture.repo, "commit", "-qm", "seal reviewed report")

            report_bytes = fixture.report.read_bytes()
            fixture.report.write_bytes(report_bytes + b"\n")
            report_result = fixture.validate()
            fixture.report.write_bytes(report_bytes)

            review_path = fixture.report.parent / "reviews.jsonl"
            review_path.write_text(
                review_path.read_text(encoding="utf-8") + "\n", encoding="utf-8"
            )
            review_result = fixture.validate()

        self.assertEqual(report_result.returncode, 1, report_result.stdout)
        self.assertIn("report.yaml differs from historical seal", report_result.stdout)
        self.assertEqual(review_result.returncode, 1, review_result.stdout)
        self.assertIn("current record differs", review_result.stdout)

    def test_historical_seal_rejects_unresolvable_or_reportless_revision(
        self,
    ) -> None:
        for revision, expected in (
            ("0" * 40, "git cat-file"),
            ("base", "does not contain report.yaml"),
        ):
            with self.subTest(revision=revision), tempfile.TemporaryDirectory() as tmp:
                fixture = self.fixture(tmp)
                fixture.receipt()
                done = fixture.repo / "tasks/done/TEST-001-fixture.md"
                done.parent.mkdir(parents=True)
                fixture.task.rename(done)
                fixture.task = done
                fixture.generate()
                git(fixture.repo, "add", ".")
                git(fixture.repo, "commit", "-qm", "commit dirty report")
                fixture.seal()
                seal_path = fixture.report.parent / "seal.yaml"
                seal = yaml.safe_load(seal_path.read_text(encoding="utf-8"))
                seal["sealed_revision"] = (
                    fixture.base if revision == "base" else revision
                )
                seal_path.write_text(
                    yaml.safe_dump(seal, sort_keys=False), encoding="utf-8"
                )
                result = fixture.validate()

            self.assertEqual(result.returncode, 1, result.stdout)
            self.assertIn(expected, result.stdout)

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


def failed_then_passed(fixture: Fixture) -> None:
    fixture.check("check-01", passing=False)
    fixture.backdate("check-01")
    fixture.check("check-02", passing=True)


class SupersessionTests(unittest.TestCase):
    """BUG-171: append-only reconciliation of a failed required receipt."""

    def receipt_bytes(self, fixture: Fixture) -> dict[str, bytes]:
        return {path.name: path.read_bytes() for path in fixture.commands.iterdir()}

    def test_identical_passing_rerun_supersedes_failure_and_keeps_history(self):
        with tempfile.TemporaryDirectory() as tmp:
            fixture = Fixture(Path(tmp))
            failed_then_passed(fixture)
            before = self.receipt_bytes(fixture)
            superseded = fixture.supersede("check-01", "check-02")
            fixture.generate()
            result = fixture.validate()
            after = self.receipt_bytes(fixture)
            entries = {
                Path(entry["receipt"]).stem: entry
                for entry in fixture.report_data()["commands"]
            }
        self.assertEqual(superseded.returncode, 0, superseded.stdout)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("superseded required failure", result.stdout)
        self.assertEqual(before, after)
        self.assertEqual(set(entries), {"check-01", "check-02"})
        self.assertEqual(
            entries["check-01"]["superseded_by"],
            {
                "receipt": "tasks/evidence/TEST-001/commands/check-02.json",
                "reason": GOOD_REASON,
            },
        )
        self.assertNotIn("superseded_by", entries["check-02"])

    def test_unsuperseded_failure_still_blocks_beside_a_valid_supersession(self):
        with tempfile.TemporaryDirectory() as tmp:
            fixture = Fixture(Path(tmp))
            for label in ("check-01", "check-02"):
                fixture.check(label, passing=False)
                fixture.backdate(label)
            fixture.check("check-03", passing=True)
            superseded = fixture.supersede("check-01", "check-03")
            fixture.generate()
            result = fixture.validate()
        self.assertEqual(superseded.returncode, 0, superseded.stdout)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("[WARNING] commands[0]: superseded required failure", result.stdout)
        self.assertIn("[ERROR] commands[1]: required command failed", result.stdout)
        self.assertNotIn("[ERROR] commands[0]", result.stdout)

    def test_cli_guard_and_validation_reject_invalid_supersessions(self):
        def unrelated(fixture: Fixture) -> None:
            fixture.check("check-01", passing=False)
            fixture.backdate("check-01")
            fixture.receipt("check-02", "/bin/true")

        def other_cwd(fixture: Fixture) -> None:
            fixture.check("check-01", passing=False)
            fixture.backdate("check-01")
            fixture.check("check-02", passing=True, cwd="sub")

        def reversed_order(fixture: Fixture) -> None:
            fixture.check("check-02", passing=True)
            fixture.check("check-01", passing=False)

        def failed_twice(fixture: Fixture) -> None:
            fixture.check("check-01", passing=False)
            fixture.backdate("check-01")
            fixture.check("check-02", passing=False)

        def optional_pass(fixture: Fixture) -> None:
            fixture.check("check-01", passing=False)
            fixture.backdate("check-01")
            fixture.check("check-02", passing=True, optional=True)

        def passed_twice(fixture: Fixture) -> None:
            fixture.check("check-01", passing=True)
            fixture.backdate("check-01")
            fixture.check("check-02", passing=True)

        def other_task(fixture: Fixture) -> None:
            failed_then_passed(fixture)
            fixture.edit_receipt("check-02", task_id="OTHER-002")

        def malformed_time(fixture: Fixture) -> None:
            failed_then_passed(fixture)
            fixture.edit_receipt("check-02", started_at="yesterday")

        def accepted_first(fixture: Fixture) -> None:
            failed_then_passed(fixture)
            fixture.check("check-03", passing=True)
            first = fixture.supersede("check-01", "check-02")
            self.assertEqual(first.returncode, 0, first.stdout)

        replacement_fails = "replacement receipt must be a passing required command"
        cases = {
            "missing receipt": (
                failed_then_passed, "check-01", "absent", GOOD_REASON, 1,
                "missing replacement receipt: absent",
            ),
            "unrelated check": (
                unrelated, "check-01", "check-02", GOOD_REASON, 1,
                "replacement runs a different command",
            ),
            "different cwd": (
                other_cwd, "check-01", "check-02", GOOD_REASON, 1,
                "replacement runs in a different working directory",
            ),
            "reversed chronology": (
                reversed_order, "check-01", "check-02", GOOD_REASON, 1,
                "replacement must start after the failed receipt finished",
            ),
            "failed replacement": (
                failed_twice, "check-01", "check-02", GOOD_REASON, 1,
                replacement_fails,
            ),
            "optional replacement": (
                optional_pass, "check-01", "check-02", GOOD_REASON, 1,
                replacement_fails,
            ),
            "passing receipt superseded": (
                passed_twice, "check-01", "check-02", GOOD_REASON, 1,
                "superseded receipt must be a failed required command",
            ),
            "self supersession": (
                failed_then_passed, "check-01", "check-01", GOOD_REASON, 1,
                replacement_fails,
            ),
            "other task receipt": (
                other_task, "check-01", "check-02", GOOD_REASON, 1,
                "replacement receipt is invalid: task_id does not match report",
            ),
            "malformed timestamp": (
                malformed_time, "check-01", "check-02", GOOD_REASON, 1,
                "replacement receipt has malformed started_at",
            ),
            "path outside commands": (
                failed_then_passed, "../../OTHER-002/commands/check-01", "check-02",
                GOOD_REASON, 1, "failed must be a receipt label",
            ),
            "short reason": (
                failed_then_passed, "check-01", "check-02", "fixed it now", 1,
                "reason must contain at least 20 non-blank characters",
            ),
            "blank reason": (
                failed_then_passed, "check-01", "check-02", " " * 30, 1,
                "reason must contain at least 20 non-blank characters",
            ),
            "cycle": (
                accepted_first, "check-02", "check-01", GOOD_REASON, 2,
                "superseded receipt must be a failed required command",
            ),
            "contradictory": (
                accepted_first, "check-01", "check-03", GOOD_REASON, 2,
                "contradictory records supersede check-01 more than once",
            ),
        }
        for name, (build, failed, replacement, reason, line, message) in cases.items():
            for mode in ("cli", "raw"):
                with self.subTest(case=name, mode=mode), tempfile.TemporaryDirectory() as tmp:
                    fixture = Fixture(Path(tmp))
                    build(fixture)
                    if mode == "cli":
                        result = fixture.supersede(failed, replacement, reason)
                        self.assertEqual(result.returncode, 2, result.stdout)
                        self.assertIn(message, result.stdout)
                        continue
                    fixture.raw_supersession(failed, replacement, reason)
                    fixture.generate()
                    result = fixture.validate()
                    self.assertEqual(result.returncode, 1, result.stdout)
                    self.assertIn(f"supersessions.jsonl:{line}: {message}", result.stdout)

    def test_validation_rejects_evidence_tampered_after_supersession(self):
        def delete_replacement(fixture: Fixture) -> None:
            (fixture.commands / "check-02.json").unlink()

        def edit_failed(fixture: Fixture) -> None:
            with (fixture.commands / "check-01.json").open("a") as handle:
                handle.write("\n")

        def edit_ledger(fixture: Fixture) -> None:
            lines = fixture.ledger.read_text(encoding="utf-8").splitlines()
            lines[0] = lines[0].replace("identical rerun", "unrelated rerun")
            fixture.ledger.write_text("\n".join(lines) + "\n", encoding="utf-8")

        cases = {
            "replacement deleted": (
                delete_replacement,
                "supersessions.jsonl:1: missing replacement receipt: check-02",
            ),
            "failed receipt edited": (
                edit_failed, "supersessions.jsonl:1: failed receipt hash mismatch"
            ),
            "non-final ledger record edited": (
                edit_ledger, "previous-entry hash chain is broken"
            ),
        }
        for name, (tamper, message) in cases.items():
            with self.subTest(case=name), tempfile.TemporaryDirectory() as tmp:
                fixture = Fixture(Path(tmp))
                failed_then_passed(fixture)
                fixture.check("check-03", passing=False)
                fixture.backdate("check-03")
                for failed in ("check-01", "check-03"):
                    accepted = fixture.supersede(failed, "check-02")
                    self.assertEqual(accepted.returncode, 0, accepted.stdout)
                tamper(fixture)
                fixture.generate()
                result = fixture.validate()
                self.assertEqual(result.returncode, 1, result.stdout)
                self.assertIn(message, result.stdout)
                self.assertIn("required command failed", result.stdout)

    def test_report_cannot_omit_a_recorded_receipt(self):
        with tempfile.TemporaryDirectory() as tmp:
            fixture = Fixture(Path(tmp))
            failed_then_passed(fixture)
            fixture.generate()
            report = fixture.report_data()
            report["commands"] = [
                entry for entry in report["commands"]
                if not entry["receipt"].endswith("check-01.json")
            ]
            fixture.report.write_text(yaml.safe_dump(report), encoding="utf-8")
            result = fixture.validate()
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn(
            "report omits recorded receipt(s): "
            "tasks/evidence/TEST-001/commands/check-01.json",
            result.stdout,
        )

    def test_report_supersession_must_match_the_ledger(self):
        def stale(fixture: Fixture) -> None:
            fixture.generate()
            accepted = fixture.supersede("check-01", "check-02")
            self.assertEqual(accepted.returncode, 0, accepted.stdout)

        def forged(fixture: Fixture) -> None:
            fixture.generate()
            report = fixture.report_data()
            report["commands"][0]["superseded_by"] = {
                "receipt": "tasks/evidence/TEST-001/commands/check-02.json",
                "reason": GOOD_REASON,
            }
            fixture.report.write_text(yaml.safe_dump(report), encoding="utf-8")

        for name, edit in (("stale", stale), ("forged", forged)):
            with self.subTest(case=name), tempfile.TemporaryDirectory() as tmp:
                fixture = Fixture(Path(tmp))
                failed_then_passed(fixture)
                edit(fixture)
                result = fixture.validate()
                self.assertEqual(result.returncode, 1, result.stdout)
                self.assertIn(
                    "report supersession does not match supersessions.jsonl",
                    result.stdout,
                )


def load_tool():
    spec = importlib.util.spec_from_file_location("workflow_evidence_under_test", TOOL)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def record_with_write_limit(
    repo: Path, label: str, *command: str
) -> subprocess.CompletedProcess[str]:
    """Writes past 256 bytes fail with EFBIG; no volume is filled, no mount needed."""

    def limit() -> None:
        resource.setrlimit(resource.RLIMIT_FSIZE, (256, 256))

    return subprocess.run(
        [
            sys.executable,
            str(TOOL),
            "record-command",
            "--root",
            str(repo),
            "--task-id",
            "TEST-001",
            "--label",
            label,
            "--min-free-gib",
            "0",
            "--",
            *command,
        ],
        cwd=repo,
        text=True,
        capture_output=True,
        preexec_fn=limit,
        check=False,
    )


def listing(path: Path) -> list[str]:
    return sorted(entry.name for entry in path.iterdir()) if path.exists() else []


class RecordCommandResourceTests(unittest.TestCase):
    def test_record_command_refuses_low_disk_headroom_before_running(self) -> None:
        tool = load_tool()
        floor = tool.DEFAULT_MIN_FREE_GIB * 1024**3
        with tempfile.TemporaryDirectory() as tmp:
            repo = Fixture(Path(tmp)).repo
            commands = repo / "tasks/evidence/TEST-001/commands"

            def record(label: str, free: int, *extra: str) -> int:
                args = tool.build_parser().parse_args(
                    ["record-command", "--root", tmp, "--task-id", "TEST-001"]
                    + ["--label", label, *extra, "--", "touch", f"{label}.ran"]
                )
                usage = SimpleNamespace(free=free)
                with mock.patch.object(tool.shutil, "disk_usage", return_value=usage):
                    with contextlib.redirect_stdout(io.StringIO()):
                        return tool.record_command(args)

            with self.assertRaisesRegex(
                ValueError, r"(?s)insufficient disk headroom.*--min-free-gib"
            ):
                record("low", floor - 1)
            self.assertFalse((repo / "low.ran").exists())
            self.assertFalse(commands.exists())

            self.assertEqual(record("at-floor", floor), 0)
            self.assertEqual(record("disabled", 0, "--min-free-gib", "0"), 0)
            self.assertTrue((repo / "at-floor.ran").exists())
            self.assertTrue((repo / "disabled.ran").exists())

    def test_unwritable_receipt_is_reported_and_completed_logs_are_kept(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = Fixture(Path(tmp)).repo
            commands = repo / "tasks/evidence/TEST-001/commands"
            result = record_with_write_limit(repo, "full", "echo", "ok")
            self.assertEqual(result.returncode, 2, result.stderr)
            self.assertIn("incomplete command evidence", result.stderr)
            self.assertIn("exited 0", result.stderr)
            self.assertEqual((commands / "full.stdout.log").read_bytes(), b"ok\n")
            self.assertEqual(listing(commands), ["full.stderr.log", "full.stdout.log"])

    def test_unwritable_log_output_is_emitted_not_lost(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = Fixture(Path(tmp)).repo
            commands = repo / "tasks/evidence/TEST-001/commands"
            result = record_with_write_limit(
                repo, "big", sys.executable, "-c", "print('x' * 4000)"
            )
            self.assertEqual(result.returncode, 2, result.stderr)
            self.assertIn("x" * 4000 + "\n", result.stdout)
            self.assertIn("incomplete command evidence", result.stderr)
            self.assertIn("kept nothing", result.stderr)
            self.assertEqual(listing(commands), [])

    def test_unprintable_stdout_does_not_drop_captured_stderr(self) -> None:
        tool = load_tool()
        enospc = OSError(errno.ENOSPC, "No space left on device")
        full = mock.Mock()
        full.buffer.write.side_effect = enospc
        err = SimpleNamespace(flush=lambda: None, buffer=io.BytesIO())
        child = "import sys; print('out'); print('CHILD DIAGNOSTIC', file=sys.stderr)"
        with tempfile.TemporaryDirectory() as tmp:
            Fixture(Path(tmp))
            args = tool.build_parser().parse_args(
                ["record-command", "--root", tmp, "--task-id", "TEST-001"]
                + ["--label", "lost", "--min-free-gib", "0", "--"]
                + [sys.executable, "-c", child]
            )
            with (
                mock.patch.object(tool, "_atomic_write", side_effect=enospc),
                mock.patch.object(tool.sys, "stdout", full),
                mock.patch.object(tool.sys, "stderr", err),
                self.assertRaises(ValueError) as raised,
            ):
                tool.record_command(args)
        self.assertEqual(err.buffer.getvalue(), b"CHILD DIAGNOSTIC\n")
        message = str(raised.exception)
        self.assertIn("exited 0", message)
        self.assertIn("could not print tasks/evidence/TEST-001/commands/lost.stdout.log", message)
        self.assertNotIn("printed above", message)


if __name__ == "__main__":
    unittest.main()
