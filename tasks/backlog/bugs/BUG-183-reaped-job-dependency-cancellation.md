---
id: BUG-183
theme: none
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
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

## Slice plan
One slice: deterministic multistage cancellation/reap regression, smallest retention/propagation correction, focused tests and documentation.

## Required changes
- [ ] Preserve terminal failure information while an unresolved dependent can reference it, or propagate cancellation fully before retiring that information.
- [ ] Keep published/reaped predecessor behavior and bounded metadata reclamation intact.

## Tests
- [ ] Reproduce cancellation of an intermediate stage with reaping between drains.
- [ ] Verify no downstream Work or Publish executes after the failed dependency.
- [ ] Verify successful dependency chains, explicit cancellation and reclamation still work.

## Docs
- [ ] Synchronize the JobService lifetime/dependency comments with the final rule.

## Acceptance criteria
- [ ] Deterministic regression fails before and passes after the correction.
- [ ] Existing RuntimeJobService tests and full CPU gate pass.
- [ ] Completed chains release dependency metadata instead of leaking terminal records.

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
