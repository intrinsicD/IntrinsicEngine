---
id: BUG-184
theme: none
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
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
- [ ] Count every nonterminal job state without double-counting maintained per-state counters.
- [ ] Handle StaleDiscarded as terminal explicitly.

## Tests
- [ ] A parked ancestor and dependent jobs remain counted as in flight.
- [ ] Publication/cancellation/reaping return aggregate counts to zero.

## Docs
- [ ] Keep the aggregate-counter source contract current.

## Acceptance criteria
- [ ] The accounting regression fails before and passes after the correction.
- [ ] Existing RuntimeJobService tests and full CPU gate pass.

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
