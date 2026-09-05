---
id: BUG-165
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "interactive delegate session; evidence is the reviewed diff and CI"
owner: Codex
branch: codex/bug-165-deterministic-drop-barrier
worktree: /home/alex/Documents/IntrinsicEngine
claimed_at: "2026-09-04T21:01:29+02:00"
contract_schema: 1
contracts: []
contract_review: "The observed defect is nondeterministic test orchestration around the existing asynchronous asset-import queue and cancellation contracts. No production contract change is proposed; promotion must revisit the catalog if diagnosis instead finds a runtime behavior defect."
---
# BUG-165 — Asset-import queue tests race worker completion

## Goal
- Make the asset-import queue snapshot, cancellation, and non-blocking-frame
  contracts observe workers at defined barriers instead of assuming small
  inputs cannot advance before the first assertion.

## Acceptance criteria
- [x] Use the existing queued-geometry pre-decode test hook to hold the worker
      at a defined barrier in both
      `DroppedFileQueuePreservesOrderDiagnosticsAndClearCompleted` and
      `DroppedGeometryQueueCancellationPreventsMainThreadApply`; observe the
      intended active/cancellable state before releasing or cancelling it.
- [x] Retain the final order, complete/failed diagnostics, clear-completed,
      cancellation, and no-main-thread-apply assertions.
- [x] Both isolated tests pass 200 consecutive unsanitized repetitions and the
      containing runtime contract selector passes without retry, quarantine,
      sleeps used as synchronization, or weakened assertions.
- [x] Synchronize the sibling geometry-cancellation, dropped model/texture,
      and slow texture-read tests exposed by hosted full-CPU and exact warm
      `pr-fast`; retain their cancellation, queue-state, and frame-progress
      assertions without changing production behavior.

## Verification
```bash
ctest --test-dir build/ci-fast --output-on-failure --parallel 4 \
  -R '^(RuntimeAssetImportFormatCoverage\.(ExplicitCancelPublishesOneTerminalEvent|DroppedModelSceneAndTextureImportThroughStreamingQueue|SlowQueuedTextureReadDoesNotBlockRunFrame)|SandboxEditorUi\.(DroppedFileQueuePreservesOrderDiagnosticsAndClearCompleted|DroppedGeometryQueueCancellationPreventsMainThreadApply))$' \
  --repeat until-fail:200
ctest --test-dir build/ci-fast --output-on-failure \
  -R '^(RuntimeAssetImportFormatCoverage|SandboxEditorUi)\.' \
  --timeout 60 --parallel 4
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
- On 2026-09-04, the deterministic barrier passed both isolated tests for 200
  consecutive repetitions, the complete 184-test `SandboxEditorUi` selector,
  and the canonical 4,263-test CPU-supported gate. Hosted `pr-fast` remains
  the final merge check and the cache seed needed by BUG-164.
- The successful cold `pr-fast` run `33928262586` seeded the exact cache, but
  its warm rerun exposed the same scheduling assumption in
  `SlowQueuedTextureReadDoesNotBlockRunFrame`: the worker may legitimately
  enter `Read()` before `QueueModelTextureImport()` returns. Hosted full-CPU
  independently exposed immediate completion in
  `ExplicitCancelPublishesOneTerminalEvent` and
  `DroppedModelSceneAndTextureImportThroughStreamingQueue`. These are sibling
  test-orchestration defects: each samples cancellability or worker progress
  without first holding the existing geometry hook or blocking IO backend.
- On 2026-09-05, all five synchronized tests passed 200 consecutive
  repetitions each under four-process contention (1,000 executions total),
  and both containing selectors passed all 216 cases without retry.
