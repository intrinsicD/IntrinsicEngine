---
id: BUG-115
theme: G
depends_on:
  - BUG-106
---
# BUG-115 — Test-gate routing case baseline drift

## Goal
- Restore the live `BUG-106` test-gate reconciler by synchronizing its exact
  affected-case baseline with the current registered GoogleTest inventory.

## Non-goals
- No production code, test-body, target, label, aggregate, or capability-route
  change.
- No weakening of exact case reconciliation or conversion of the live
  aggregate check to warning mode.
- No broad regeneration framework for a five-row deterministic correction.

## Context
- Symptom: on clean `main` at `3aa33618`,
  `Test.TestGateRouting.py --build-dir build/ci --aggregate IntrinsicTests`
  fails before accepting the configured test graph. The baseline still names
  four retired `CoreGraphInterfaces` cases, while the live registry names four
  replacement `TaskPlanGraph` cases and one added
  `RuntimeStreamingExecutor.RuntimeTaskKindTokensRemainStable` contract.
- Expected behavior: the checked-in affected-case baseline names every current
  case in the `BUG-106` reconciliation scope exactly once, so an actual
  missing, extra, duplicated, mislabeled, or misrouted case still fails
  closed.
- Impact: any change that legitimately touches the routing baseline cannot
  pass the canonical live aggregate reconciler even though the complete CPU
  CTest gate is green.
- Diagnosis: both the failing baseline and the live case names are
  byte-identical on `main` and the in-flight `RUNTIME-179` branch before that
  branch's scoped additions. This is a pre-existing harness-data defect, not
  an async-work regression.

## Status
- Completed and retired on 2026-07-19; owner: Codex; branch: `main`;
  implementation commit: `fdd466e4`.
- The live reconciler now accepts 4,154 cases across 36 targets and 338
  assertion sources.

## Required changes
- [x] Replace the four retired `CoreGraphInterfaces` case names with the four
      current `TaskPlanGraph` names.
- [x] Add the current stable task-kind-token case under its canonical runtime
      integration target.
- [x] Ratchet the matching per-target and total affected-case counts from
      `104`/`233` to `105`/`234`.
- [x] Preserve every source-owner, label, aggregate, duplicate-case, and
      sanitizer-environment reconciliation rule.

## Tests
- [x] Reproduce the exact live aggregate failure before the correction.
- [x] Pass the live `IntrinsicTests` aggregate reconciliation afterward.
- [x] Pass the routing tool's complete synthetic self-test and strict test
      layout check.

## Docs
- [x] Record diagnosis and verification in this task, the bug index, and the
      retirement log. No architecture documentation changes because production
      behavior and test routing do not change.

## Acceptance criteria
- [x] The configured live inventory and checked-in affected-case baseline
      reconcile exactly.
- [x] Synthetic missing/extra/duplicate/mislabel regressions remain fail
      closed.
- [x] The patch changes only harness baseline/count ratchets and task-state
      documentation.

## Evidence
- Before the correction, the live reconciler reported the four retired graph
  cases as missing and the four `TaskPlanGraph` replacements plus
  `RuntimeTaskKindTokensRemainStable` as extra. After the row correction it
  exposed the paired stale `104` per-target count, proving both baseline
  representations failed closed independently.
- After the correction,
  `Test.TestGateRouting.py --build-dir build/ci --aggregate IntrinsicTests`
  passed with `targets=36 cases=4154 sources=338`.
- All 19 synthetic routing regressions passed. Strict test layout, task policy,
  task-state links, and diff-whitespace checks passed.

## Verification
```bash
python3 tests/regression/tooling/Test.TestGateRouting.py \
  --build-dir build/ci --aggregate IntrinsicTests
python3 tests/regression/tooling/Test.TestGateRouting.py --self-test
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
git diff --check
```

## Forbidden changes
- Changing production code, test bodies, CMake ownership, labels, or aggregate
  membership to match stale baseline text.
- Deleting baseline rows, suppressing mismatch output, or broadening accepted
  cases without matching live inventory evidence.
