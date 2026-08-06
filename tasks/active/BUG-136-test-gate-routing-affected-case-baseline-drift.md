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
claimed_at: "2026-08-06T18:09:44Z"
contract_schema: 1
contracts: []
contract_review: "Reviewed the full catalog; this task reconciles a generated-by-audit test-routing case baseline with existing registered cases and changes no engine, geometry, method, publication, configuration, runtime, UI, or reusable task-workflow contract."
---
# BUG-136 — Test-gate routing affected-case baseline drift

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

- [ ] Audit the live `Test.RuntimeEngineLayering.cpp` case inventory against
      CTest discovery and the affected-source map.
- [ ] Replace the two retired case rows with the exact six live rows reported
      by reconciliation; preserve every unaffected row and exact-equality
      check.

## Tests

- [ ] The 19 hermetic routing regressions pass unchanged.
- [ ] Live `IntrinsicCpuTests` and `IntrinsicTests` reconciliation pass against
      the freshly built `build/ci` registry.

## Docs

- [ ] Record the audited mismatch, correction, and verification here and in
      the bug index; no architecture documentation change is required.

## Acceptance criteria

- [ ] The reported missing/extra set is empty without changing source
      ownership or checker logic.
- [ ] The baseline remains a sorted exact inventory of the affected cases.
- [ ] Strict task, test-layout, and whitespace gates pass.

## Verification

```bash
python3 tests/regression/tooling/Test.TestGateRouting.py --self-test
python3 tests/regression/tooling/Test.TestGateRouting.py --build-dir build/ci --aggregate IntrinsicCpuTests
python3 tests/regression/tooling/Test.TestGateRouting.py --build-dir build/ci --aggregate IntrinsicTests
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
git diff --check
```

## Forbidden changes

- Editing RuntimeEngineLayering test names or registrations to match stale
  baseline rows.
- Removing affected sources or cases from reconciliation.
- Adding an allowlist, ignored-case path, or non-exact comparison.
