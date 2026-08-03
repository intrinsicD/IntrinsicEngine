---
id: BUG-128
theme: H
depends_on: [PROC-030]
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "Codex-LocalMerge"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-03T12:07:20Z"
contract_schema: 1
contracts: [repo.task-contract-discovery]
---
# BUG-128 — Parallel pre-policy retired task baselines

## Status
- Implementation and regression coverage are complete on 2026-08-03. Strict
  task-policy and workflow-custody validation pass while the eleven imported
  retired task files remain byte-identical to their pre-policy feature
  revision.
- The task remains active because its `high-risk` profile still requires a
  durable handoff and an independent fixed-surface accepted review before
  retirement; no self-review is being represented as that acceptance.

## Goal
- Let the prospective task-contract validator preserve byte-identical retired
  task records that were completed and scientifically sealed on a parallel
  branch before `PROC-030` took effect, without treating later edits as
  grandfathered.

## Non-goals
- Do not rewrite sealed task hashes, experiment runs, bundles, or audits.
- Do not move the global policy effective date or grandfather new/open work.
- Do not accept an unverified path, digest, or symbolic Git revision.

## Context
- Symptom: merging `feature/lop-consolidation-e2e` with the post-`PROC-030`
  main line forces its pre-policy retired tasks to enroll, which changes four
  claim-grade task hashes after official run initialization.
- Expected behavior: exact retired bytes can cite the immutable parallel commit
  that contains them; promotion, replay in another lifecycle, or any later byte
  change still requires prospective enrollment.
- Impact: without a bounded parallel-history baseline, a correct branch merge
  must either invalidate scientific custody or weaken the contract gate.

## Required changes
- [x] Extend the contract legacy inventory with path-scoped retired snapshots,
  each binding one exact 40-hex source revision and SHA-256 digest.
- [x] Validate every supplemental record against Git and permit it only under
  `tasks/done/` or `tasks/archive/`.
- [x] Keep open-task consumption/replay behavior and the primary immutable
  baseline unchanged.

## Tests
- [x] Add regression coverage proving an exact parallel retired snapshot is
  accepted and a byte change still requires contract enrollment.
- [x] Run strict task, workflow-evidence, and documentation gates on the merged
  tree.

## Docs
- [x] Document the narrow parallel-history case in the canonical task-format
  policy and synchronize skill mirrors.

## Acceptance criteria
- [x] All pre-policy feature task records retain their sealed bytes.
- [x] Claim-grade custody validates without changing any official run or
  bundle provenance.
- [x] New, edited, promoted, or open task records remain prospective.

## Verification
```bash
python3 tests/regression/tooling/Test.ValidateTasks.py
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/workflow_evidence.py validate --root .
python3 tools/agents/sync_skills.py --check
```

## Forbidden changes
- Rewriting historical experiment custody to match a post-run task edit.
- Allowing a supplemental baseline without exact path, revision, and digest.
- Broadening the exception to active or backlog tasks.
