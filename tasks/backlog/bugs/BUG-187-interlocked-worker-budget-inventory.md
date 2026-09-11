---
id: BUG-187
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Bounded verification repair discovered during RORG-134; evidence is the diff and workflow regression tests.
contract_schema: 1
contracts: []
contract_review: The catalog has no CTest worker-budget inventory contract; retain AGENTS.md testing reservations and exact source-to-CMake reconciliation.
---
# BUG-187 — Reconcile interlocked single-worker test reservations

## Goal
Count the paused worker and controlling test thread in the three JobService
completion-interlock tests, and keep CTest reservations and the source audit exact.

## Context
RORG-134 reproduced a pre-existing failure in `Test.WorkflowConcurrency.py`
at HEAD `90909d7507bf6da1014b6b16ce0a88f05cd0db0c`: the audit ignored
`SchedulerScope{1}`, while CMake already reserved two processors for
`CancelBeforeStartFinalizesOnMainThreadInsteadOfPublishing`. The expected
budget distribution was also stale (one three-processor case now needs two).
Source inspection found two other tests using the same paused-worker interlock without that reservation.
The runtime tests and scheduler behavior are unchanged by this repair.

## Acceptance criteria
- [x] The source audit recognizes all three explicit paused-worker interlocks.
- [x] CMake reserves two processors for each and the exact inventory agrees.
- [x] Workflow regressions and the relevant JobService tests pass.

## Verification
```bash
python3 tests/regression/tooling/Test.WorkflowConcurrency.py
ctest --test-dir build/ci --output-on-failure -R '^RuntimeJobService\.' --no-tests=error --timeout 60
```

## Verification outcome
Fixed in the uncommitted RORG-134 cleanup. All 20 workflow-concurrency
regressions pass, and the final CPU gate selects 4,508 tests with zero failures
and one unrelated unsanitized leak-control skip. The three JobService tests
pass. Retirement awaits integration of the reviewed cleanup.
