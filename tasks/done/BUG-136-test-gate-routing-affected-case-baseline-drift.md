---
id: BUG-136
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "codex-bug136"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-06T18:10:59Z"
contract_schema: 1
contracts: []
contract_review: "Reviewed the full catalog; this task reconciles a generated-by-audit test-routing case baseline with existing registered cases and changes no engine, geometry, method, publication, configuration, runtime, UI, or reusable task-workflow contract."
---
# BUG-136 — Test-gate routing affected-case baseline drift

## Status

- Completed on 2026-08-06.
- Implementation commit: `89c4e9ed`.

## Goal

- Restore the BUG-106 affected-case parity check by making its baseline name
  the exact current cases owned by the audited Runtime Engine layering source.

## Non-goals

- No test implementation, label, target, source ownership, aggregate
  membership, or production behavior change.
- No reduction of the affected source set or weakening of exact baseline
  equality.
- No unrelated baseline cleanup.

## Context

- Symptom: after BUG-135 restored manual-producer classification, live
  reconciliation reports two missing retired RuntimeEngineLayering case names
  and six extra current names.
- Expected behavior: every registered case from the BUG-106 affected sources
  appears exactly once in the checked-in baseline, so additions, deletions, and
  renames remain fail-closed.
- Impact: both required `IntrinsicCpuTests` and `IntrinsicTests` routing
  preflights fail, blocking BUG-135, RUNTIME-217, and REVIEW-003.

## Required changes

- [x] Audit the live `Test.RuntimeEngineLayering.cpp` case inventory against
      CTest discovery and the affected-source map.
- [x] Replace the two retired case rows with the exact six live rows reported
      by reconciliation, then synchronize the coupled Runtime-target and total
      count ratchets from `35`/`222` to `39`/`226`; preserve every unaffected
      row and exact-equality check.

## Tests

- [x] The 19 hermetic routing regressions pass unchanged.
- [x] Live `IntrinsicCpuTests` and `IntrinsicTests` reconciliation pass against
      the freshly built `build/ci` registry.

## Docs

- [x] Record the audited mismatch, correction, and verification here and in
      the bug index; no architecture documentation change is required.

## Acceptance criteria

- [x] The reported missing/extra set is empty without changing source
      ownership or checker logic.
- [x] The baseline remains an exact inventory of the affected cases with
      explicit per-target and total count ratchets.
- [x] Strict task, test-layout, and whitespace gates pass.

## Verification

```bash
python3 tests/regression/tooling/Test.TestGateRouting.py --self-test
python3 tests/regression/tooling/Test.TestGateRouting.py --build-dir build/ci --aggregate IntrinsicCpuTests
python3 tests/regression/tooling/Test.TestGateRouting.py --build-dir build/ci --aggregate IntrinsicTests
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
git diff --check
```

Executed on 2026-08-06: all 19 hermetic regressions passed. Live
reconciliation passed for `IntrinsicCpuTests` (26 targets, 4,101 cases, 327
sources) and `IntrinsicTests` (37 targets, 4,158 cases, 327 sources). Strict
test-layout, task-policy, task-format, task-state-link, documentation-link, and
whitespace checks passed. The repair changed only the exact case inventory and
its coupled Runtime-target/total count ratchets.

## Forbidden changes

- Editing RuntimeEngineLayering test names or registrations to match stale
  baseline rows.
- Removing affected sources or cases from reconciliation.
- Adding an allowlist, ignored-case path, or non-exact comparison.
