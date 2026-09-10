---
id: BUG-183
theme: none
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-overnight"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-09-10T00:40:45Z"
contract_schema: 1
contracts: []
contract_review: "The catalog has no JobService dependency ID; preserve its existing documented cancellation and terminal-publication contract."
---
# BUG-183 — Preserve failed dependency state through job reaping

## Goal
Prevent a cancelled/discarded predecessor from becoming a satisfied dependency after ReapCompleted removes its job record.

## Non-goals
No scheduler redesign, worker-budget change or new dependency API.

## Context
Independent RUNTIME-224 source review found that `Runtime.JobService.cpp` ReleaseSatisfiedDependencies treats missing/reaped dependencies as satisfied. Cancellation propagation visits one pending layer per drain; reaping can remove an unsuccessfully terminated predecessor before its downstream consumer observes the failure. RUNTIME-224 adds its own main-thread abandonment guard and actual Vulkan intermediate-cancellation test. This task owns the scheduler-level correction for other consumers. The review finding is source-backed; a deterministic standalone reproducer must establish the exact affected sequence before changing JobService.

## Risk review
Cancellation propagation and reclamation cross the worker/main-thread lifetime boundary. Use an independent fixed-surface review for this correction; no public API change is needed.

## Slice plan
One slice: deterministic multistage cancellation/reap regression, smallest retention/propagation correction, focused tests and documentation.

## Required changes
- [x] Preserve terminal failure information while an unresolved dependent can reference it, or propagate cancellation fully before retiring that information.
- [x] Keep published/reaped predecessor behavior and bounded metadata reclamation intact.

## Tests
- [x] Reproduce cancellation of an intermediate stage with reaping between drains.
- [x] Verify no downstream Work or Publish executes after the failed dependency.
- [x] Verify successful dependency chains, explicit cancellation and reclamation still work.

## Docs
- [x] Synchronize the JobService lifetime/dependency comments with the final rule.

## Acceptance criteria
- [x] Deterministic regression fails before and passes after the correction.
- [x] Existing RuntimeJobService tests and full CPU gate pass.
- [x] Completed chains release dependency metadata instead of leaking terminal records.

## Verification
```bash
cmake --preset ci
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R '^RuntimeJobService' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
Do not weaken cancellation tests or treat every absent token as successful without preserving the failed-dependency contract.

## Reproduction and correction
The controlled before-fix run executes and publishes the third job after its predecessor is cancelled and reaped: downstream Work=1, nonempty publications, one finalizer instead of two. The successful-chain control passes. Retaining dependency records referenced by the pending queue makes all 27 RuntimeJobService tests pass; completed chains still reclaim all three records.

An existing compiler warning also identifies missing waiting-state cases in the aggregate `InFlightJobs` diagnostic. BUG-184 tracks that separate accounting issue; the regression here additionally asserts the final snapshot is empty and every record was reaped.

## Completion — 2026-09-10
Commit: `924a45372`. The regression fails before the correction and all 27 scheduler cases pass after it. Full CPU selects 4,426 tests with zero failures; native follow-up yields 4,425 distinct passes and one expected unsanitized leak-control skip. The actual Vulkan bilateral cancellation case passes without a skip under the existing BUG-180 leak-check exclusion. No permanent dependency-outcome ledger or public API was added. Independent acceptance and sealed evidence are recorded in `tasks/evidence/BUG-183/report.yaml`.
