---
id: BUG-150
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "codex-bug150"
branch: "bug-150-historical-workflow-seals"
worktree: "/tmp/intrinsic-bug150-worktree"
claimed_at: "2026-08-10T23:33:26Z"
contract_schema: 1
contracts: [repo.task-contract-discovery]
contract_review: "This defect is in the reusable task/workflow-evidence contract: completed reports lose global validity after later tasks legitimately update shared ARA, task-index, or documentation files. Engine layering, geometry domains, property publication, method integration, and the agent work-graph lifecycle are unchanged."
---
# BUG-150 — Completed workflow reports lose their historical surface seal

## Status
- Completed on 2026-08-11.
- Implementation commit: `92fb669a`.

## Goal
- Keep a completed task's workflow-evidence report globally valid after later
  tasks legitimately modify shared files, while still verifying that report
  against the exact historical source surface and review digest it recorded.

## Non-goals
- No regeneration of old reports against a broadened current diff.
- No weakening of current active/dirty-report validation, artifact hashes,
  command receipts, independent-review binding, or task completion policy.
- No engine, method, benchmark, runtime, config, UI, or backend change.

## Context
- Symptom: during METHOD-038 checkpoint-2 verification on 2026-08-10,
  `python3 tools/agents/workflow_evidence.py validate --root .` reported 35
  errors. Completed GEOM-071 and METHOD-037 reports failed because later work
  changed shared `ara/*`, task indexes/session brief, benchmark docs, and method
  docs; the validator compared their recorded hashes and changed-path surfaces
  to the current worktree rather than an immutable historical seal.
- Expected behavior: a completed report remains verifiable against the exact
  committed tree/snapshot that contained its accepted source and review
  surface. Later unrelated tasks may update shared ledgers and indexes without
  invalidating history. A current uncommitted report must still bind the live
  worktree, and tampering or an unresolvable historical seal must fail closed.
- Impact: normal append-only ARA/task/doc updates make the repository-global
  workflow validator red after otherwise valid task completion. Regenerating
  a prior report would falsely broaden its scope and break its independent
  review identity, so agents currently cannot repair the gate honestly.

## Required changes
- [x] Define a deterministic historical-source seal for completed workflow
  reports, using a resolvable exact commit or approved snapshot/diff identity
  that contains every recorded surface byte and artifact.
- [x] Validate completed reports against that immutable seal while retaining
  live-worktree validation for active/uncommitted reports.
- [x] Fail closed when the seal is missing, ambiguous, does not contain the
  recorded task/report, or disagrees with the report's content digest,
  artifacts, receipts, handoff, or accepted review.
- [x] Diagnose existing stale completed reports explicitly; migrate them only
  through evidence that proves their original bytes, never by rebasing their
  surface onto current `HEAD`.

## Tests
- [x] Add a hermetic regression that commits a complete report and its shared
  ARA/task surface, makes a later unrelated commit changing those shared files,
  and proves global validation still checks the historical report successfully.
- [x] Cover tampered historical bytes, missing/unresolvable seals, task moves,
  and an active dirty report that must continue comparing against the live
  worktree.

## Docs
- [x] Update `docs/agent/workflow-evidence.md` and regenerate the workflow skill
  mirror if the completed-report source-seal format or validation rules change.

## Acceptance criteria
- [x] The METHOD-038-session repro no longer invalidates completed GEOM-071 or
  METHOD-037 reports merely because shared ledgers/indexes advanced.
- [x] Historical validation still rejects any altered report, source surface,
  artifact, receipt, handoff, or independent-review binding.
- [x] Workflow-evidence regressions and repository-global validation pass
  without regenerating a completed report against later unrelated changes.

## Verification
```bash
python3 tests/regression/tooling/Test.WorkflowEvidence.py
python3 tools/agents/workflow_evidence.py validate --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
```

Executed on 2026-08-11: all 32 workflow-evidence regressions passed; repository
validation completed with 0 errors and 68 pre-existing explicit-skip warnings;
strict task policy/format/state-link, skill-mirror, docs-sync, docs-link, and
root-hygiene gates passed. The unchanged `GEOM-071` and `METHOD-037` reports
seal to `2e36465b` and `23c1080a` respectively; neither report was regenerated.

## Forbidden changes
- Treating the current worktree as the historical source of a completed report.
- Silently regenerating or widening an accepted report/review surface.
- Skipping hash, artifact, receipt, task, handoff, or independent-review checks
  for historical evidence.
