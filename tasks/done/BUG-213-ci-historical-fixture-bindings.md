---
id: BUG-213
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive CI fixture repair; preserved failure logs and strict validation results provide evidence
contract_schema: 1
contracts: []
contract_review: Frozen fixture bindings and regression inputs only; no engine API, numerical method, validator policy or benchmark result changes.
---
# BUG-213 — Preserve historical CI fixture bindings

## Goal

Resolve two historical-fixture failures exposed by the next clean-checkout run
of [PR #1044](https://github.com/intrinsicD/IntrinsicEngine/pull/1044):

- METHOD-047 added an Angle parameter to a manifest already bound by an old
  immutable scaling result. Restore the exact original manifest; Angle remains
  fixed explicitly in the runner. Do not rewrite the baseline, hash or validator.
- The legacy task enrollment regressions copied the current BUG-091 task, which
  the parent branch has already enrolled. Select an unconsumed entry from the authoritative inventory and use
  the existing pinned baseline-byte helper for unchanged, edited and promoted legacy fixtures.

## Acceptance criteria

- [x] Preserve the historical baseline and restore its exact manifest hash.
- [x] Exercise the pinned, unconsumed legacy task bytes in all three legacy cases,
  with the existing strict acceptance/rejection assertions unchanged.
- [x] Run the complete selected structural route and docs workflow regressions,
  retaining both original failures and the passing results.

## Verification

```bash
python3 tools/benchmark/validate_benchmark_results.py --root benchmarks/baselines --strict
python3 tests/regression/tooling/Test.CiTiming.py
python3 tests/regression/tooling/Test.ValidateTasks.py
python3 tools/ci/touched_scope.py --root . --action plan --base-ref codex/proc-034-token-efficiency --head-ref HEAD --output /tmp/intrinsic-method047-collaboration/final-route.json
python3 tools/ci/touched_scope.py --root . --action structural --plan /tmp/intrinsic-method047-collaboration/final-route.json
```

The original failures are retained under `tasks/evidence/BUG-213/`.
This repair changes neither scientific results nor claim eligibility.

## Completion

Retired 2026-09-23 at the CI-maintenance endpoint.
PR/commit: [PR #1044](https://github.com/intrinsicD/IntrinsicEngine/pull/1044),
the enclosing historical-fixture repair commit.

All seven baseline results and 110 manifests validate. The timing regression
passes 20 cases and the task validator passes 25. The full selected structural
route passes; the docs workflow's remaining evidence/custody/claim/work-graph
suites pass 32, 40, 9 and 29 cases respectively. Actual Opus 5.5 reviewed the
manifest restoration and final inventory-selected frozen fixtures; its
[final review](../evidence/BUG-213/claude-final-review.md) passes.
[Binding proof](../evidence/BUG-213/frozen-binding.json) records the unchanged
baseline and restored manifest hash. No C++ source, validator, recorded
scientific result or acceptance threshold changes. Remote CI reruns after push.
