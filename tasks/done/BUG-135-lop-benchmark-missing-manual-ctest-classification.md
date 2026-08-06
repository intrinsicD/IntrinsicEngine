---
id: BUG-135
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "codex-bug135"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-06T18:15:42Z"
contract_schema: 1
contracts: []
contract_review: "Reviewed the full catalog; this repair classifies an existing standalone benchmark CTest producer in the test-routing reconciler and changes no engine, data-domain, publication, configuration, runtime, UI, or reusable task-workflow contract."
---
# BUG-135 — LOP benchmark lacks manual CTest classification

## Status

- Completed on 2026-08-06.
- Implementation commit: `0424ea43`.

## Goal

- Restore fail-closed live test-routing reconciliation by classifying the
  existing `IntrinsicLopFamilyGpuBenchmarkSmoke` executable as a manual CTest
  producer.

## Non-goals

- No benchmark implementation, registration, labels, aggregate membership,
  execution policy, or result-schema change.
- No weakening of the rule that every registered target must have GoogleTest
  source ownership or an explicit manual-producer classification.
- No unrelated test-routing baseline or target-ownership cleanup.

## Context

- Symptom: on 2026-08-06, both
  `--aggregate IntrinsicTests` and `--aggregate IntrinsicCpuTests` live
  reconciliation failed with
  `registered targets have no source ownership and are not classified as
  manual CTest producers: ['IntrinsicLopFamilyGpuBenchmarkSmoke']`.
- Expected behavior: the standalone LOP-family benchmark executable is
  classified alongside the existing benchmark runners that register CTest
  cases manually rather than through GoogleTest source discovery.
- Impact: the required CPU routing preflight fails on every otherwise valid
  aggregate, blocking RUNTIME-217 and the fresh REVIEW-003 audit.

## Required changes

- [x] Add only `IntrinsicLopFamilyGpuBenchmarkSmoke` to the reconciler's
      explicit manual CTest producer set.
- [x] Preserve every source-owner, aggregate, label, and case-baseline rule.

## Tests

- [x] The 19 hermetic routing regressions pass unchanged.
- [x] Live `IntrinsicCpuTests` and `IntrinsicTests` reconciliation pass against
      the freshly built `build/ci` registry.

## Docs

- [x] Record the cause, exact repair, and verification in this task and the
      bug index; no architecture documentation change is required.

## Acceptance criteria

- [x] The exact unclassified-target failure no longer reproduces.
- [x] Removing the classification would still make live reconciliation fail
      closed; no exemption or ignored-target path is introduced.
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

Executed on 2026-08-06 after BUG-136 reconciled the independently exposed case
baseline drift: all 19 hermetic regressions passed. Live reconciliation passed
for `IntrinsicCpuTests` (26 targets, 4,101 cases, 327 sources) and
`IntrinsicTests` (37 targets, 4,158 cases, 327 sources). Strict test-layout,
task-policy, task-format, task-state-link, documentation-link, and whitespace
checks passed. The production benchmark, CTest registration, labels, and
aggregate graph are unchanged.

## Forbidden changes

- Adding the target to an ignored or exception list that bypasses ownership
  validation.
- Relabeling, disabling, removing, or rebuilding the benchmark to make the
  reconciler stop seeing it.
- Mixing unrelated benchmark, CMake, or test-routing changes into the repair.
