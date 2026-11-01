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
- [ ] The cause is confirmed (for example by holding the job's end) and the test no longer depends on timing.

## Verification
```bash
ctest --test-dir build/dev -R 'SandboxJobsWindow\.' --repeat until-fail:50 --timeout 120
```
