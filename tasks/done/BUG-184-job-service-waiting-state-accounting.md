---
id: BUG-184
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive diagnostic-accounting repair with deterministic regression tests; no performance or backend claim.
contract_schema: 1
contracts: []
contract_review: "No JobService aggregate-accounting contract ID exists in the catalog; preserve the documented job states and completion semantics."
---
# BUG-184 — Count waiting jobs in aggregate in-flight diagnostics

## Goal
Make JobService::Stats report pending dependencies and parked apply-gate jobs as in flight until they become terminal.

## Context
Building BUG-183 exposes the pre-existing exhaustive-switch warning in Runtime.JobService.cpp: Stats omits AwaitingDependencies, AwaitingApply and StaleDiscarded. The first two are nonterminal but currently add nothing to InFlightJobs. Source inspection identifies the omission; a deterministic parked-job/dependency test must reproduce the accounting error before correction.

## Non-goals
No scheduler redesign or change to cancellation/publication behavior.

## Slice plan
One slice: controlled waiting-state accounting test, complete the state accounting, verify terminal/reaped counts and existing runtime tests.

## Required changes
- [x] Count every nonterminal job state without double-counting maintained per-state counters.
- [x] Handle StaleDiscarded as terminal explicitly.

## Tests
- [x] A parked ancestor and dependent jobs remain counted as in flight.
- [x] Publication/cancellation/reaping return aggregate counts to zero.

## Docs
- [x] Keep the aggregate-counter source contract current.

## Acceptance criteria
- [x] The accounting regression fails before and passes after the correction.
- [x] Existing RuntimeJobService tests and full CPU gate pass.

## Verification
```bash
cmake --preset ci
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R '^RuntimeJobService' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
Do not treat waiting states as terminal merely to make aggregate counts consistent.

## Plan and reuse decision — 2026-09-14
Operator requests continued simplification/open-point work with Claude. This
reproducible job-diagnostics omission affects queue monitoring and is selected
as a bounded reliability slice. `JobService::Stats` duplicates an incomplete
state classification while the existing private `IsTerminal` already serves
completion, cancellation, dependency dispatch and reaping. Reuse that predicate
for aggregate counting, explicitly excluding Invalid. Compute the three direct
state buckets from the same acquire-loaded value; retain the maintained waiting
counters without adding them twice. No helper, file, module or interface change.

Root owns the checkout and documentation. Claude implements only two temporary
source/test copies, followed by a fixed-source review. Backtest new assertions
on original production before applying the correction. Existing single-worker
fixtures provide parked/dependency barriers; terminal states are the completion
oracle, not the aggregate count under test. Keep finalizer, cancellation and
publication semantics unchanged. A separate state classifier would only be
justified by a genuinely different lifecycle contract.

## Review and implementation — 2026-09-14
`Stats` now uses the existing private `IsTerminal` predicate, with an explicit
Invalid exclusion, instead of its separate incomplete switch. Waiting gauges
remain maintained once; unpublished finalizers retain their separate counter.
The parked-ancestor/dependent test observes actual job states independently of
the aggregate under test, then checks publication and reaping. Existing tests
also cover parked results, cancellation and stale-discarded terminal records.

The backtest failed in both waiting-state cases before the production fix; its
cancellation and stale-state controls passed. All 29 focused native JobService
and engine-wiring tests pass after the fix. Claude implemented temporary source
copies and reviewed the fixed diff. Root fixed the new fixture's failure-path
lifetime by declaring its scheduler scope after captured objects; Claude's
follow-up confirmed the join now precedes their destruction. Runtime docs state
the existing off-thread cross-field snapshot limitation. No public API, module,
state transition, or scheduler change was needed.

## Final verification — 2026-09-14
Verified the combined BUG-184/BUG-181 source before separate implementation
commits. Supported Clang 23 preset builds passed with ccache disabled: native
`IntrinsicTests`, isolated ASan/UBSan `IntrinsicRuntimeContractTests`, and the
fresh Null/headless shader target. Full native CPU selected 4,612 cases:
4,611 passed, zero failed, one expected unsanitized GLFW/LSan-only skip.
Focused isolated ASan and UBSan each passed 29 JobService/engine-wiring cases
and all 66 editor mesh-method cases (95 per sanitizer). The original filename
regex did not select the editor suite; the additional runs used its actual
66 source-declared test names. No GPU execution or performance result claimed.

All 37 planner regressions and strict layering, allowlist, task policy,
document links, test layout, root hygiene and skill-mirror checks passed.
No source module interface changed. Four-point review: one intent per source
commit, no new layer edge, behavior covered by failing-before/passing-after
tests, owning docs and task records synchronized.

Local full logs, exact commands, fixed diffs, source hashes and Claude reviews:
`build/analysis/bug184-job-accounting-2026-09-14/` and
`build/analysis/bug181-shader-routing-2026-09-14/`. These are interactive local
verification records, not benchmark or research-claim evidence.

## Completion
Completed 2026-09-14. Commit: `8c671ae4dc548ea0119e3cfc2906a0d85344dba6`.
Maturity: CPUContracted, the intended endpoint for this diagnostic/tooling
repair; no GPU backend or later capability gate belongs to its scope.
BUILD-007 retains matched compile-time measurement, and the broader engine
convergence and GPU work remain open under their existing owners. This closure
adds no performance or backend-capability claim. Committed locally; not pushed.
