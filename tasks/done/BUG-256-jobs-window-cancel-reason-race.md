---
id: BUG-256
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests, and CI
contract_schema: 1
contracts: []
contract_review: A test-timing defect in one integration test; no catalog contract changes.
---
# BUG-256 — Jobs window test reads the cancel reason after the job may have ended

## Goal
- `SandboxJobsWindow.ListsALongJobAndCancelsItThroughTheSurface` failed once in the CPU set on the shared dev
  build (2026-11-01, during ARCH-022 slice 3 step 1b): `tests/integration/runtime/Test.SandboxJobsWindow.cpp:198`
  expected the disabled Cancel to say "already requested", and the reason was something else. It passed 5 of 5
  reruns and the run before.
- Likely cause: the test reads the cancel readiness two frames after the Cancel press; a cancelled job that already
  ended no longer reports "already requested". Confirm, then make the test hold the job until it has checked the
  reason.

## Acceptance criteria
- [x] The cause is confirmed (for example by holding the job's end) and the test no longer depends on timing.

## Log
- 2026-11-01: Confirmed: with a 100 ms pause and one more frame before the readiness check, the test failed every
  time; the cancelled job had ended and the reason was "The job already ended." Fix: the test's long job keeps
  running after the cancel request until the test releases it (`LongJob(..., stopOnCancel = false)`), and the test
  releases it before draining. With the pause the fixed test passes; without it, the 9 `SandboxJobsWindow` tests
  pass 30 repetitions (`--repeat until-fail:30`).
- 2026-11-01: Review (Codex, fresh session `01a1973c-80b4-7881-8c35-e6573a58ff0e`) of the fix block with ARCH-022's
  open points, digest `f47ccb8c…cc415d84`: REVISE for this record only (no review attribution); no correctness
  finding. Its non-blocking note, applied: an early assertion failure left the held job running and `release` too
  short-lived; `release` is now declared before the harness and a scope guard releases the job on every exit (an
  injected early failure ends the test at once; 10 repetitions pass). The delta's decision is recorded in ARCH-022's
  log. Completed 2026-11-01. Commit: the one whose message names BUG-256 (`git log --grep BUG-256`).

## Verification
```bash
ctest --test-dir build/dev -R 'SandboxJobsWindow\.' --repeat until-fail:50 --timeout 120
```
