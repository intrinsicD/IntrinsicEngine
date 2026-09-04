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
# BUG-165 — Dropped-file queue tests race worker completion

## Goal
- Make the dropped-file queue snapshot and cancellation contracts
  deterministically observe the queued geometry worker at a defined barrier,
  instead of assuming a tiny OBJ cannot finish before the first snapshot.

## Acceptance criteria
- [ ] Use the existing queued-geometry pre-decode test hook to hold the worker
      at a defined barrier in both
      `DroppedFileQueuePreservesOrderDiagnosticsAndClearCompleted` and
      `DroppedGeometryQueueCancellationPreventsMainThreadApply`; observe the
      intended active/cancellable state before releasing or cancelling it.
- [ ] Retain the final order, complete/failed diagnostics, clear-completed,
      cancellation, and no-main-thread-apply assertions.
- [ ] Both isolated tests pass 200 consecutive unsanitized repetitions and the
      containing runtime contract selector passes without retry, quarantine,
      sleeps used as synchronization, or weakened assertions.

## Verification
```bash
ctest --test-dir build/ci-fast --output-on-failure --parallel 1 \
  -R '^SandboxEditorUi\.(DroppedFileQueuePreservesOrderDiagnosticsAndClearCompleted|DroppedGeometryQueueCancellationPreventsMainThreadApply)$' \
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
- Fresh hosted seed `33869585685` reproduced the cancellation race and exposed
  the same assumption in
  `DroppedFileQueuePreservesOrderDiagnosticsAndClearCompleted`: both saw
  `CanCancel == false` after the tiny OBJ worker completed before the first
  snapshot. The module probe and all 2,039 build edges passed, but the failed
  test phase correctly prevented the cold ccache store from being saved, so
  BUG-164's warm cohort cannot begin until this harness race is repaired.
