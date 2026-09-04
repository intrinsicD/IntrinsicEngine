---
id: BUG-165
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "backlog test-harness bug; execution evidence is deferred until promotion"
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: "The observed defect is nondeterministic test orchestration around the existing asset-import cancellation contract. No production contract change is proposed; promotion must revisit the catalog if diagnosis instead finds a runtime behavior defect."
---
# BUG-165 — Dropped-geometry cancellation test races worker completion

## Goal
- Make `SandboxEditorUi.DroppedGeometryQueueCancellationPreventsMainThreadApply`
  deterministically exercise cancellation while the queued geometry worker is
  still active, instead of assuming a tiny OBJ cannot finish before the first
  queue snapshot.

## Acceptance criteria
- [ ] Use the existing queued-geometry pre-decode test hook to hold the worker
      at a defined barrier, observe the cancellable operation, request
      cancellation, release the worker, and retain the no-main-thread-apply
      assertion.
- [ ] The isolated test passes 200 consecutive unsanitized repetitions and the
      containing runtime contract selector passes without retry, quarantine,
      sleeps used as synchronization, or weakened assertions.

## Verification
```bash
ctest --test-dir build/ci-fast --output-on-failure --parallel 1 \
  -R '^SandboxEditorUi\.DroppedGeometryQueueCancellationPreventsMainThreadApply$' \
  --repeat until-fail:200
ctest --test-dir build/ci-fast --output-on-failure \
  -R '^SandboxEditorUi\.' --timeout 60 --parallel 4
```

## Context
- Hosted `pr-fast` run `33867833363` on 2026-09-04 completed the BUG-164
  module-invalidation probe and all 2,039 build edges, then failed only this
  test. The first snapshot reported `CanCancel == false`; the import completed
  and materialized one mesh before cancellation was attempted.
- The same binary passed 200/200 isolated local repetitions, confirming that
  the current test is scheduling-sensitive rather than a deterministic
  BUG-164 cache failure. Nearby runtime import tests already use
  `SetQueuedGeometryImportBeforeDecodeHookForTest(...)` as a synchronization
  barrier.
