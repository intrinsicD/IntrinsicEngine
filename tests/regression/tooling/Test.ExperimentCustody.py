#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parents[3]
TOOL = REPO_ROOT / "tools/agents/experiment_custody.py"
PROTECTED_FIXTURE = REPO_ROOT / "tools/agents/fixtures/protected-synthetic"


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def seal_protocol(protocol: dict) -> dict:
    protocol["state"] = "frozen"
    protocol["frozen_at"] = "2026-07-31T00:00:00Z"
    canonical = {
        key: value
        for key, value in protocol.items()
        if key not in {"protocol_digest", "frozen_at"}
    }
    protocol["protocol_digest"] = hashlib.sha256(
        json.dumps(
            canonical,
            ensure_ascii=False,
            separators=(",", ":"),
            sort_keys=True,
            default=str,
        ).encode("utf-8")
    ).hexdigest()
    return protocol


def invoke(repo: Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(TOOL), *args],
        cwd=repo,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


def git(repo: Path, *args: str) -> str:
    return subprocess.run(
        ["git", *args],
        cwd=repo,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    ).stdout.strip()


class ExperimentFixture:
    def __init__(
        self,
        root: Path,
        *,
        profile: str = "claim-grade",
        claim_eligible: bool = False,
    ):
        self.repo = root
        git(root, "init", "-q")
        git(root, "config", "user.email", "fixture@example.invalid")
        git(root, "config", "user.name", "Fixture")
        self.task = root / "tasks/active/TEST-001-fixture.md"
        self.task.parent.mkdir(parents=True)
        self.task.write_text(
            f"""---
id: TEST-001
theme: none
depends_on: []
workflow_schema: 1
workflow_profile: {profile}
evidence: required
owner: driver
branch: test/experiment
worktree: {root}
claimed_at: "2026-07-29T12:00:00Z"
---
# TEST-001 — Experiment fixture

## Goal
- Exercise experiment custody.

## Non-goals
- No product behavior.

## Context
- Tooling fixture.

## Required changes
- [x] Freeze the protocol.

## Tests
- [x] Run the custody tests.

## Docs
- [x] Keep the fixture self-describing.

## Acceptance criteria
- [x] Custody is deterministic.

## Verification
```bash
true
```

## Forbidden changes
- No unrelated work.
""",
            encoding="utf-8",
        )
        self.data_screen = root / "data/screen.json"
        self.data_confirm = root / "data/confirm.json"
        self.config = root / "config/run.yaml"
        self.environment = root / "config/environment.json"
        self.implementation = root / "src/implementation.py"
        for path, payload in (
            (self.data_screen, '{"rows":[1,2]}\n'),
            (self.data_confirm, '{"rows":[3,4]}\n'),
            (self.config, "iterations: 2\n"),
            (self.environment, '{"python":"3.12"}\n'),
            (self.implementation, "def run(): return 1\n"),
        ):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(payload, encoding="utf-8")
        git(root, "add", ".")
        git(root, "commit", "-qm", "source")
        self.source_revision = git(root, "rev-parse", "HEAD")
        self.experiment = root / "tasks/evidence/TEST-001/experiment"
        self.protocol = self.experiment / "protocol.yaml"
        self.protocol.parent.mkdir(parents=True)
        protocol = {
            "schema_version": 2,
            "task_id": "TEST-001",
            "protocol_id": "synthetic-v1",
            "profile": profile,
            "state": "ready",
            "question": "Does the synthetic score remain bounded?",
            "hypothesis": "Mean score is at most two.",
            "claim_boundary": "Synthetic fixture only.",
            "evidence_phase": "confirmation",
            "claim_eligible": claim_eligible,
            "datasets": [
                {
                    "id": "synthetic-screen",
                    "path": "data/screen.json",
                    "sha256": sha(self.data_screen),
                    "split": "screen",
                },
                {
                    "id": "synthetic-confirm",
                    "path": "data/confirm.json",
                    "sha256": sha(self.data_confirm),
                    "split": "confirm",
                },
            ],
            "disjoint_splits": [
                {
                    "left": "screen",
                    "right": "confirm",
                    "method": "checked-in disjoint synthetic files",
                }
            ],
            "seeds": [7, 11],
            "input_policy": "Use only declared synthetic rows.",
            "comparators": [{"id": "constant-baseline", "budget": 2}],
            "primary_metrics": [
                {
                    "name": "score",
                    "direction": "minimize",
                    "unit": "synthetic-points",
                    "statistical_unit": "row",
                }
            ],
            "statistical_tests": [{"name": "descriptive-mean", "alpha": None}],
            "summaries": [{"name": "score", "column": "score", "statistic": "mean"}],
            "killing_gates": [{"metric": "score", "operator": "<=", "threshold": 2.0}],
            "screening_confirmation_policy": "No retuning after confirmation begins.",
            "resources": {"cpu_threads": 1, "gpu": False},
            "command": ["python3", "run_synthetic.py"],
            "expected_artifacts": ["raw_rows.jsonl", "bundle.yaml"],
            "blockers": [],
            "source": {
                "revision": self.source_revision,
                "clean": claim_eligible,
            },
            "config": {"path": "config/run.yaml", "sha256": sha(self.config)},
            "environment": {
                "path": "config/environment.json",
                "sha256": sha(self.environment),
            },
            "implementation": [
                {
                    "path": "src/implementation.py",
                    "sha256": sha(self.implementation),
                }
            ],
        }
        if profile == "protected":
            source_files = {
                self.data_screen: "screen.json",
                self.data_confirm: "confirm.json",
                self.config: "run.yaml",
                self.environment: "environment.json",
                self.implementation: "implementation.py",
            }
            for destination, name in source_files.items():
                destination.write_bytes((PROTECTED_FIXTURE / name).read_bytes())
            replacements = {
                "__TASK_ID__": "TEST-001",
                "__SOURCE_REVISION__": self.source_revision,
                "__SCREEN_PATH__": self.data_screen.relative_to(root).as_posix(),
                "__SCREEN_SHA256__": sha(self.data_screen),
                "__CONFIRM_PATH__": self.data_confirm.relative_to(root).as_posix(),
                "__CONFIRM_SHA256__": sha(self.data_confirm),
                "__CONFIG_PATH__": self.config.relative_to(root).as_posix(),
                "__CONFIG_SHA256__": sha(self.config),
                "__ENVIRONMENT_PATH__": self.environment.relative_to(root).as_posix(),
                "__ENVIRONMENT_SHA256__": sha(self.environment),
                "__IMPLEMENTATION_PATH__": self.implementation.relative_to(
                    root
                ).as_posix(),
                "__IMPLEMENTATION_SHA256__": sha(self.implementation),
            }
            template = (PROTECTED_FIXTURE / "protocol.template.yaml").read_text(
                encoding="utf-8"
            )
            for marker, value in replacements.items():
                template = template.replace(marker, value)
            protocol = yaml.safe_load(template)
        self.protocol.write_text(
            yaml.safe_dump(protocol, sort_keys=False), encoding="utf-8"
        )
        self.profile = profile
        self.claim_eligible = claim_eligible
        self.run_root = self.experiment / "runs/run-001"

    def freeze(self, *, commit: bool = False) -> subprocess.CompletedProcess[str]:
        result = invoke(
            self.repo,
            "freeze-protocol",
            "--root",
            str(self.repo),
            "--protocol",
            self.protocol.relative_to(self.repo).as_posix(),
        )
        if result.returncode == 0 and commit:
            git(self.repo, "add", ".")
            git(self.repo, "commit", "-qm", "freeze protocol")
        return result

    def init(self) -> subprocess.CompletedProcess[str]:
        return invoke(
            self.repo,
            "init-run",
            "--root",
            str(self.repo),
            "--protocol",
            self.protocol.relative_to(self.repo).as_posix(),
            "--run-parent",
            (self.experiment / "runs").relative_to(self.repo).as_posix(),
            "--run-id",
            "run-001",
            "--attempt-id",
            "attempt-001",
        )

    def cell(self, state: str, **kwargs: str) -> subprocess.CompletedProcess[str]:
        command = [
            "append-cell",
            "--root",
            str(self.repo),
            "--run-root",
            self.run_root.relative_to(self.repo).as_posix(),
            "--cell-key",
            "primary",
            "--state",
            state,
        ]
        for key, value in kwargs.items():
            command.extend([f"--{key.replace('_', '-')}", value])
        return invoke(self.repo, *command)

    def raw_and_receipt(self) -> tuple[Path, Path]:
        raw = self.run_root / "raw_rows.jsonl"
        raw.write_text('{"score":1.0}\n{"score":2.0}\n', encoding="utf-8")
        receipt = self.run_root / "smoke.json"
        receipt.write_text('{"exit_code":0}\n', encoding="utf-8")
        return raw, receipt

    def bundle(
        self,
        *,
        visual: bool = False,
        preview: str | None = None,
        smoke: bool = True,
        link: str | list[str] | None = None,
    ) -> subprocess.CompletedProcess[str]:
        raw, receipt = self.raw_and_receipt()
        command = [
            "create-bundle",
            "--root",
            str(self.repo),
            "--run-root",
            self.run_root.relative_to(self.repo).as_posix(),
            "--raw-rows",
            raw.relative_to(self.repo).as_posix(),
            "--summary",
            "score:score:mean",
            "--replay-command",
            "python3",
            "--replay-command",
            "run_synthetic.py",
            "--view-command",
            "python3",
            "--view-command",
            "view_synthetic.py",
        ]
        if smoke:
            command.extend(
                ["--smoke-receipt", receipt.relative_to(self.repo).as_posix()]
            )
        if visual:
            command.append("--visual")
        if preview:
            command.extend(["--preview", preview])
        links = [link] if isinstance(link, str) else (link or [])
        for value in links:
            command.extend(["--link", value])
        return invoke(self.repo, *command)

    def audit(self) -> subprocess.CompletedProcess[str]:
        return invoke(
            self.repo,
            "audit-bundle",
            "--root",
            str(self.repo),
            "--run-root",
            self.run_root.relative_to(self.repo).as_posix(),
            "--driver",
            "driver",
            "--auditor",
            "auditor",
            "--revision",
            self.source_revision,
        )

    def completion(self) -> subprocess.CompletedProcess[str]:
        return invoke(
            self.repo,
            "validate-completion",
            "--root",
            str(self.repo),
            "--task-id",
            "TEST-001",
            "--profile",
            self.profile,
            "--experiment-root",
            self.experiment.relative_to(self.repo).as_posix(),
        )

    def review(
        self,
        *,
        kind: str = "independent",
        reviewer: str = "reviewer",
        interactions: int = 0,
        draws: int = 0,
    ) -> subprocess.CompletedProcess[str]:
        return invoke(
            self.repo,
            "prospective-review",
            "--root",
            str(self.repo),
            "--protocol",
            self.protocol.relative_to(self.repo).as_posix(),
            "--driver",
            "driver",
            "--reviewer",
            reviewer,
            "--review-kind",
            kind,
            "--review-method",
            "source inspection",
            "--review-boundary",
            "implementation and frozen protocol only",
            "--source-coverage",
            "src/implementation.py",
            "--protected-interactions",
            str(interactions),
            "--private-draws",
            str(draws),
            "--disposition",
            "no-blocker",
        )

    def authorize(
        self, authorizer: str = "authorizer"
    ) -> subprocess.CompletedProcess[str]:
        return invoke(
            self.repo,
            "authorize-protected",
            "--root",
            str(self.repo),
            "--protocol",
            self.protocol.relative_to(self.repo).as_posix(),
            "--authorizer",
            authorizer,
            "--authorization-boundary",
            "one synthetic attempt",
        )

    def attempt(self, state: str) -> subprocess.CompletedProcess[str]:
        return invoke(
            self.repo,
            "consume-attempt",
            "--root",
            str(self.repo),
            "--run-root",
            self.run_root.relative_to(self.repo).as_posix(),
            "--attempt-id",
            "attempt-001",
            "--state",
            state,
            "--reason",
            "fixture transition",
        )


class ExperimentCustodyTests(unittest.TestCase):
    def test_repository_protected_fixture_contains_no_real_protected_inputs(
        self,
    ) -> None:
        template = (PROTECTED_FIXTURE / "protocol.template.yaml").read_text(
            encoding="utf-8"
        )
        self.assertIn("Synthetic fixture only", template)
        self.assertIn("claim_eligible: false", template)
        self.assertNotIn("http://", template)
        self.assertNotIn("https://", template)

    def test_protocol_schema_one_requires_explicit_summary_migration(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(Path(tmp))
            protocol = yaml.safe_load(fixture.protocol.read_text(encoding="utf-8"))
            protocol["schema_version"] = 1
            fixture.protocol.write_text(
                yaml.safe_dump(protocol, sort_keys=False), encoding="utf-8"
            )
            result = fixture.freeze()
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("protocol schema_version must equal 2", result.stdout)

    def test_claim_grade_end_to_end_bundle_and_audit(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(
                Path(tmp), profile="claim-grade", claim_eligible=True
            )
            self.assertEqual(fixture.freeze(commit=True).returncode, 0)
            self.assertEqual(fixture.init().returncode, 0)
            self.assertEqual(fixture.cell("started").returncode, 0)
            self.assertEqual(fixture.cell("completed").returncode, 0)
            self.assertEqual(fixture.bundle().returncode, 0)
            audit = fixture.audit()
            validation = invoke(fixture.repo, "validate", "--root", str(fixture.repo))
            completion = fixture.completion()
        self.assertEqual(audit.returncode, 0, audit.stdout)
        self.assertEqual(validation.returncode, 0, validation.stdout)
        self.assertEqual(completion.returncode, 0, completion.stdout)

    def test_completion_requires_protocol_run_bundle_and_audit(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(Path(tmp), profile="claim-grade")
            result = fixture.completion()
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("protocol must be frozen", result.stdout)
        self.assertIn("at least one initialized run", result.stdout)

    def test_existing_benchmark_smoke_routes_through_bundle_and_audit(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(
                Path(tmp), profile="claim-grade", claim_eligible=False
            )
            manifest_source = (
                REPO_ROOT / "benchmarks/datasets/manifests/geometry_example_small.yaml"
            )
            result_source = (
                REPO_ROOT / "benchmarks/reports/examples/example_smoke_result.json"
            )
            manifest = (
                fixture.repo
                / "benchmarks/datasets/manifests/geometry_example_small.yaml"
            )
            result = fixture.repo / "benchmarks/results/example_smoke_result.json"
            manifest.parent.mkdir(parents=True)
            result.parent.mkdir(parents=True)
            manifest.write_bytes(manifest_source.read_bytes())
            result.write_bytes(result_source.read_bytes())

            protocol = yaml.safe_load(fixture.protocol.read_text(encoding="utf-8"))
            protocol["question"] = "Does the existing example smoke stay bounded?"
            protocol["hypothesis"] = "Runtime remains within its smoke guardrail."
            protocol["claim_boundary"] = "Workflow migration rehearsal only."
            protocol["primary_metrics"] = [
                {
                    "name": "runtime_ms",
                    "direction": "minimize",
                    "unit": "milliseconds",
                    "statistical_unit": "benchmark attempt",
                }
            ]
            protocol["summaries"] = [
                {
                    "name": "runtime_ms",
                    "column": "runtime_ms",
                    "statistic": "mean",
                }
            ]
            protocol["killing_gates"] = [
                {"metric": "runtime_ms", "operator": "<=", "threshold": 200.0}
            ]
            fixture.protocol.write_text(
                yaml.safe_dump(protocol, sort_keys=False), encoding="utf-8"
            )

            self.assertEqual(fixture.freeze().returncode, 0)
            self.assertEqual(fixture.init().returncode, 0)
            self.assertEqual(fixture.cell("started").returncode, 0)
            self.assertEqual(fixture.cell("completed").returncode, 0)
            _, receipt = fixture.raw_and_receipt()
            bundle = invoke(
                fixture.repo,
                "create-bundle",
                "--root",
                str(fixture.repo),
                "--run-root",
                fixture.run_root.relative_to(fixture.repo).as_posix(),
                "--benchmark-result",
                result.relative_to(fixture.repo).as_posix(),
                "--benchmark-manifests-root",
                "benchmarks",
                "--summary",
                "runtime_ms:runtime_ms:mean",
                "--replay-command",
                "python3",
                "--replay-command",
                "tools/benchmark/run_and_seal.py",
                "--view-command",
                "python3",
                "--view-command",
                "tools/benchmark/validate_benchmark_results.py",
                "--smoke-receipt",
                receipt.relative_to(fixture.repo).as_posix(),
            )
            audit = fixture.audit()
            bundle_data = yaml.safe_load(
                (fixture.run_root / "bundle.yaml").read_text(encoding="utf-8")
            )
        self.assertEqual(bundle.returncode, 0, bundle.stdout)
        self.assertEqual(audit.returncode, 0, audit.stdout)
        self.assertEqual(
            bundle_data["benchmark_result"]["benchmark_id"],
            "geometry.example.small",
        )
        self.assertFalse(bundle_data["benchmark_result"]["claim_eligible"])

    def test_dirty_official_source_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(Path(tmp), claim_eligible=True)
            fixture.freeze(commit=True)
            (fixture.repo / "dirty.txt").write_text("dirty\n", encoding="utf-8")
            result = fixture.init()
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("clean worktree", result.stdout)

    def test_claim_eligible_source_must_contain_sealed_implementation(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(Path(tmp), claim_eligible=True)
            fixture.implementation.write_text("def run(): return 2\n", encoding="utf-8")
            protocol = yaml.safe_load(fixture.protocol.read_text(encoding="utf-8"))
            protocol["implementation"][0]["sha256"] = sha(fixture.implementation)
            seal_protocol(protocol)
            fixture.protocol.write_text(
                yaml.safe_dump(protocol, sort_keys=False), encoding="utf-8"
            )
            git(fixture.repo, "add", ".")
            git(fixture.repo, "commit", "-qm", "freeze mismatched implementation")
            result = fixture.init()
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("hash differs from claim-eligible source revision", result.stdout)

    def test_claim_source_path_cannot_follow_current_worktree_symlink(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(Path(tmp), claim_eligible=True)
            alias = fixture.repo / "src/declared_alias.py"
            alias.symlink_to("implementation.py")
            protocol = yaml.safe_load(fixture.protocol.read_text(encoding="utf-8"))
            protocol["implementation"] = [
                {
                    "path": alias.relative_to(fixture.repo).as_posix(),
                    "sha256": sha(alias),
                }
            ]
            seal_protocol(protocol)
            fixture.protocol.write_text(
                yaml.safe_dump(protocol, sort_keys=False), encoding="utf-8"
            )
            git(fixture.repo, "add", ".")
            git(fixture.repo, "commit", "-qm", "freeze symlinked implementation")
            result = fixture.init()
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("is missing from claim-eligible source revision", result.stdout)

    def test_run_root_cannot_be_overwritten(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(Path(tmp))
            fixture.freeze()
            self.assertEqual(fixture.init().returncode, 0)
            result = fixture.init()
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("already exists", result.stdout)

    def test_changed_task_after_initialization_is_detected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(Path(tmp))
            fixture.freeze()
            fixture.init()
            fixture.task.write_text(
                fixture.task.read_text(encoding="utf-8") + "\nchanged\n",
                encoding="utf-8",
            )
            result = invoke(fixture.repo, "validate", "--root", str(fixture.repo))
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("task changed", result.stdout)

    def test_changed_protocol_and_retuned_gate_are_detected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(Path(tmp))
            fixture.freeze()
            fixture.init()
            protocol = yaml.safe_load(fixture.protocol.read_text(encoding="utf-8"))
            protocol["killing_gates"][0]["threshold"] = 99.0
            fixture.protocol.write_text(
                yaml.safe_dump(protocol, sort_keys=False), encoding="utf-8"
            )
            result = invoke(fixture.repo, "validate", "--root", str(fixture.repo))
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("protocol_digest", result.stdout)

    def test_changed_seal_is_detected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(Path(tmp))
            fixture.freeze()
            fixture.init()
            fixture.config.write_text("iterations: 999\n", encoding="utf-8")
            result = invoke(fixture.repo, "validate", "--root", str(fixture.repo))
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("hash mismatch", result.stdout)

    def test_clean_claim_run_keeps_historical_source_seals_after_later_commit(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(Path(tmp), claim_eligible=True)
            self.assertEqual(fixture.freeze(commit=True).returncode, 0)
            self.assertEqual(fixture.init().returncode, 0)
            self.assertEqual(fixture.cell("started").returncode, 0)
            self.assertEqual(fixture.cell("completed").returncode, 0)
            source_link = fixture.implementation.relative_to(fixture.repo).as_posix()
            evidence_link = (
                fixture.run_root / "smoke.json"
            ).relative_to(fixture.repo).as_posix()
            self.assertEqual(
                fixture.bundle(link=[source_link, evidence_link]).returncode,
                0,
            )
            self.assertEqual(fixture.audit().returncode, 0)
            git(fixture.repo, "add", ".")
            git(fixture.repo, "commit", "-qm", "seal official evidence")

            for path, payload in (
                (fixture.data_screen, '{"rows":[10,20]}\n'),
                (fixture.data_confirm, '{"rows":[30,40]}\n'),
                (fixture.config, "iterations: 999\n"),
                (fixture.environment, '{"python":"3.13"}\n'),
                (fixture.implementation, "def run(): return 2\n"),
            ):
                path.write_text(payload, encoding="utf-8")
            git(fixture.repo, "add", ".")
            git(fixture.repo, "commit", "-qm", "evolve source inputs")

            validation = invoke(
                fixture.repo, "validate", "--root", str(fixture.repo)
            )
            completion = fixture.completion()
            (fixture.run_root / "smoke.json").write_text(
                '{"exit_code":0,"tampered":true}\n', encoding="utf-8"
            )
            tampered = invoke(
                fixture.repo, "validate", "--root", str(fixture.repo)
            )
        self.assertEqual(validation.returncode, 0, validation.stdout)
        self.assertEqual(completion.returncode, 0, completion.stdout)
        self.assertEqual(tampered.returncode, 1, tampered.stdout)
        self.assertIn("hash mismatch", tampered.stdout)

    def test_run_bindings_must_equal_frozen_protocol(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(Path(tmp))
            fixture.freeze()
            fixture.init()
            run_path = fixture.run_root / "run.yaml"
            run_data = yaml.safe_load(run_path.read_text(encoding="utf-8"))
            run_data["config"] = dict(run_data["environment"])
            run_path.write_text(
                yaml.safe_dump(run_data, sort_keys=False), encoding="utf-8"
            )
            result = invoke(fixture.repo, "validate", "--root", str(fixture.repo))
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("run config differs from frozen protocol", result.stdout)

    def test_cell_keys_are_append_only_and_errors_remain_visible(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(Path(tmp))
            fixture.freeze()
            fixture.init()
            self.assertEqual(fixture.cell("started").returncode, 0)
            self.assertEqual(
                fixture.cell("failed", error="synthetic failure").returncode, 0
            )
            reused = fixture.cell("started")
            journal = (fixture.run_root / "cells.jsonl").read_text(encoding="utf-8")
        self.assertEqual(reused.returncode, 2, reused.stdout)
        self.assertIn("already terminal", reused.stdout)
        self.assertIn("synthetic failure", journal)

    def test_missing_cell_requires_visible_reason(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(Path(tmp))
            fixture.freeze()
            fixture.init()
            result = fixture.cell("missing")
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("requires --reason", result.stdout)

    def test_bundle_rejects_missing_smoke_and_visual_preview(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(Path(tmp))
            fixture.freeze()
            fixture.init()
            missing_smoke = fixture.bundle(smoke=False)
        self.assertEqual(missing_smoke.returncode, 2, missing_smoke.stdout)
        self.assertIn("smoke receipt", missing_smoke.stdout)

        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(Path(tmp))
            fixture.freeze()
            fixture.init()
            missing_preview = fixture.bundle(visual=True)
        self.assertEqual(missing_preview.returncode, 2, missing_preview.stdout)
        self.assertIn("preview/readback", missing_preview.stdout)

    def test_bundle_rejects_nonportable_or_missing_link(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(Path(tmp))
            fixture.freeze()
            fixture.init()
            result = fixture.bundle(link="../outside")
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("repository-relative", result.stdout)

    def test_audit_recomputes_and_rejects_inconsistent_summary(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(Path(tmp))
            fixture.freeze()
            fixture.init()
            fixture.bundle()
            bundle_path = fixture.run_root / "bundle.yaml"
            bundle = yaml.safe_load(bundle_path.read_text(encoding="utf-8"))
            bundle["summaries"][0]["value"] = 999.0
            bundle_path.write_text(
                yaml.safe_dump(bundle, sort_keys=False), encoding="utf-8"
            )
            result = fixture.audit()
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("Audit rejected", result.stdout)

    def test_audit_rejects_bundle_provenance_substitution(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(Path(tmp))
            fixture.freeze()
            fixture.init()
            fixture.bundle()
            bundle_path = fixture.run_root / "bundle.yaml"
            bundle = yaml.safe_load(bundle_path.read_text(encoding="utf-8"))
            bundle["provenance"]["source"] = {"revision": "substituted"}
            bundle["resolved_config"] = dict(bundle["environment"])
            bundle["environment"] = {"path": "substituted"}
            bundle["claim_authorized"] = True
            bundle_path.write_text(
                yaml.safe_dump(bundle, sort_keys=False), encoding="utf-8"
            )
            audit = fixture.audit()
            audit_errors = json.loads(
                (fixture.run_root / "audit.json").read_text(encoding="utf-8")
            )["errors"]
        self.assertEqual(audit.returncode, 1, audit.stdout)
        self.assertIn("bundle provenance differs from frozen run", audit_errors)
        self.assertIn("bundle resolved config differs from frozen run", audit_errors)
        self.assertIn("bundle environment differs from frozen run", audit_errors)
        self.assertIn("bundle must not claim authorization", audit_errors)

    def test_audit_rejects_gate_retuned_after_bundle_creation(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(Path(tmp))
            protocol = yaml.safe_load(fixture.protocol.read_text(encoding="utf-8"))
            protocol["killing_gates"][0]["threshold"] = 1.0
            fixture.protocol.write_text(
                yaml.safe_dump(protocol, sort_keys=False), encoding="utf-8"
            )
            fixture.freeze()
            fixture.init()
            fixture.cell("started")
            fixture.cell("completed")
            fixture.bundle()
            bundle_path = fixture.run_root / "bundle.yaml"
            bundle = yaml.safe_load(bundle_path.read_text(encoding="utf-8"))
            self.assertFalse(bundle["gates"][0]["passed"])
            bundle["gates"][0]["threshold"] = 2.0
            bundle["gates"][0]["passed"] = True
            bundle_path.write_text(
                yaml.safe_dump(bundle, sort_keys=False), encoding="utf-8"
            )
            audit = fixture.audit()
            audit_errors = json.loads(
                (fixture.run_root / "audit.json").read_text(encoding="utf-8")
            )["errors"]
            completion = fixture.completion()
        self.assertEqual(audit.returncode, 1, audit.stdout)
        self.assertIn("gates[0] declaration differs from frozen protocol", audit_errors)
        self.assertEqual(completion.returncode, 1, completion.stdout)
        self.assertIn("declaration differs from frozen protocol", completion.stdout)

    def test_audit_rejects_summary_retuned_after_bundle_creation(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(Path(tmp))
            protocol = yaml.safe_load(fixture.protocol.read_text(encoding="utf-8"))
            protocol["killing_gates"][0]["threshold"] = 1.0
            fixture.protocol.write_text(
                yaml.safe_dump(protocol, sort_keys=False), encoding="utf-8"
            )
            fixture.freeze()
            fixture.init()
            fixture.cell("started")
            fixture.cell("completed")
            fixture.bundle()
            bundle_path = fixture.run_root / "bundle.yaml"
            bundle = yaml.safe_load(bundle_path.read_text(encoding="utf-8"))
            self.assertEqual(bundle["summaries"][0]["value"], 1.5)
            self.assertFalse(bundle["gates"][0]["passed"])
            bundle["summaries"][0]["statistic"] = "min"
            bundle["summaries"][0]["value"] = 1.0
            bundle["gates"][0]["observed"] = 1.0
            bundle["gates"][0]["passed"] = True
            bundle_path.write_text(
                yaml.safe_dump(bundle, sort_keys=False), encoding="utf-8"
            )
            audit = fixture.audit()
            audit_errors = json.loads(
                (fixture.run_root / "audit.json").read_text(encoding="utf-8")
            )["errors"]
            completion = fixture.completion()
        self.assertEqual(audit.returncode, 1, audit.stdout)
        self.assertIn(
            "summaries[0] declaration differs from frozen protocol", audit_errors
        )
        self.assertEqual(completion.returncode, 1, completion.stdout)
        self.assertIn("declaration differs from frozen protocol", completion.stdout)

    def test_protected_review_must_be_result_free(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(Path(tmp), profile="protected")
            fixture.freeze()
            result = fixture.review(interactions=1)
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("zero protected access", result.stdout)

    def test_machine_rehearsal_cannot_authorize(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(Path(tmp), profile="protected")
            fixture.freeze()
            self.assertEqual(fixture.review(kind="machine_rehearsal").returncode, 0)
            result = fixture.authorize()
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("machine rehearsal", result.stdout)

    def test_reviewer_cannot_self_authorize(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(Path(tmp), profile="protected")
            fixture.freeze()
            fixture.review()
            result = fixture.authorize(authorizer="reviewer")
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("self-authorize", result.stdout)

    def test_stale_implementation_digest_invalidates_authorization(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(Path(tmp), profile="protected")
            fixture.freeze()
            fixture.review()
            fixture.implementation.write_text("def run(): return 2\n", encoding="utf-8")
            result = fixture.authorize()
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("hash mismatch", result.stdout)

    def test_one_shot_identity_cannot_retry_after_any_consumed_state(self) -> None:
        for terminal in ("started", "failed", "completed"):
            with self.subTest(terminal=terminal), tempfile.TemporaryDirectory() as tmp:
                fixture = ExperimentFixture(Path(tmp), profile="protected")
                fixture.freeze()
                fixture.review()
                fixture.authorize()
                fixture.init()
                first = fixture.attempt("started")
                self.assertEqual(first.returncode, 0, first.stdout)
                if terminal != "started":
                    transitioned = fixture.attempt(terminal)
                    self.assertEqual(transitioned.returncode, 0, transitioned.stdout)
                retry = fixture.attempt("started")
                self.assertEqual(retry.returncode, 2, retry.stdout)
                self.assertIn("retry is forbidden", retry.stdout)

    def test_protected_completion_requires_authority_and_terminal_attempt(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = ExperimentFixture(Path(tmp), profile="protected")
            fixture.freeze()
            fixture.review()
            fixture.authorize()
            fixture.init()
            fixture.cell("started")
            fixture.cell("completed")
            fixture.bundle()
            fixture.audit()
            before_attempt = fixture.completion()
            fixture.attempt("started")
            started = fixture.completion()
            fixture.attempt("completed")
            completed = fixture.completion()
        self.assertEqual(before_attempt.returncode, 1, before_attempt.stdout)
        self.assertIn("attempt consumption is missing", before_attempt.stdout)
        self.assertEqual(started.returncode, 1, started.stdout)
        self.assertIn("terminal attempt", started.stdout)
        self.assertEqual(completed.returncode, 0, completed.stdout)


if __name__ == "__main__":
    unittest.main()
