---
id: BUG-123
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "Codex-GeometryE2E"
branch: "feature/lop-consolidation-e2e"
worktree: "/tmp/intrinsic-geometry-e2e.GJlhXS"
claimed_at: "2026-08-01T22:08:28Z"
---
# BUG-123 — Retired queued scene save intermittently loses its terminal event

## Status
- Completed on 2026-08-01 after METHOD-019's required `ci-ubsan` gate
  reproduced the missing-event ordering once in the full 2,654-test cohort and
  again on the fifth focused `--repeat until-fail:100` attempt.
- Root cause: a worker could store `JobState::Cancelled`, lose the race with an
  empty main-thread drain, and only then queue its unpublished finalizer. The
  drain-until-terminal helper consequently observed a terminal state and
  returned before `SceneDocumentModule` published its failure event.
- Resolution: finalizer-owning jobs now carry an explicit unsettled bit;
  `IsComplete()` remains false and `ReapCompleted()` retains the record until
  either normal publication wins or the unpublished main-thread finalizer has
  actually run. A test-only worker interlock forces the historical ordering
  without sleeps.
- Implementation commit: `90d4fb3014515e5ae5801e2bd1bb88fa67fd36c5`;
  completion evidence and independent review are bound separately under
  `tasks/evidence/BUG-123/`.

## Goal
- Make world cancellation of a queued scene save publish exactly one terminal
  `RuntimeSceneFileEvent` regardless of whether cancellation wins before,
  during, or after the worker finishes.

## Non-goals
- Weakening the terminal-event assertions, adding sleeps, raising the test
  timeout, or quarantining the case.
- Changing unrelated scene-load, document-replacement, or texture-bake
  behavior.

## Context
- Symptom: `RuntimeSceneLifecycle.RetiredQueuedSceneSavePublishesTerminalEvent`
  intermittently finds no last scene-file event after
  `CancelAllForWorld(world)` and `DrainUntilTerminal(...)`. The 2026-07-28
  RUNTIME-191 full CPU gate reproduced the failure twice; a focused
  `--repeat until-fail:10` run passed nine iterations and failed the tenth at
  `Test.RuntimeSceneLifecycle.cpp:854`.
- The 2026-08-01 reproduction is preserved in
  `tasks/evidence/METHOD-019/commands/ci-ubsan-cpu-ctest.json` and
  `tasks/evidence/METHOD-019/commands/ubsan-bug123-repeat.json`; neither gate
  was retried or weakened before activating this repair.
- Expected behavior: cancelling the owning world leaves the task in
  `JobState::Cancelled` and claims the unpublished scene-save finalizer exactly
  once, publishing an `InvalidState` terminal event even if the worker already
  completed.
- Impact: the required default CPU gate is nondeterministically red and can
  obscure regressions in unrelated runtime work.
- Likely owners: `SceneDocumentModule::QueueSceneSaveToPath`, the queued
  operation's unpublished finalizer, and `JobService` world-cancellation /
  completion-drain ordering.

## Required changes
- [x] Build a deterministic interlock that exercises cancellation before and
      after worker completion without wall-clock sleeps.
- [x] Identify which cancellation/completion ordering loses the unpublished
      finalizer and make terminal publication exactly-once for every ordering.
- [x] Preserve `JobState::Cancelled`, event sequence/path/task/error fields,
      and the no-second-event drain assertion.

## Tests
- [x] The deterministic regression fails on the pre-fix ordering and passes
      after the fix.
- [x] The exact existing contract passes at least 100 focused repetitions.
- [x] The complete default CPU-supported gate passes.

## Docs
- [x] Record the causal ordering and verification evidence in this task and
      the retirement log; no architecture-doc change is required unless the
      production cancellation contract changes.

## Acceptance criteria
- [x] Every retired queued scene save publishes exactly one terminal event.
- [x] No timeout, label, quarantine, or assertion is weakened.
- [x] The fix introduces no runtime layering violation.

## Verification results
- `WorkerTerminalStateWaitsForUnpublishedFinalizerBeforeCompletion`: the
  `--repeat until-fail:100` selector passed both registered instances for 200
  deterministic forced-order executions.
- `RetiredQueuedSceneSavePublishesTerminalEvent`: the same 100-repeat selector
  passed both registered instances for 200 executions; the event field and
  exactly-once assertions are unchanged.
- Focused `RuntimeJobService|RuntimeSceneLifecycle|SceneDocumentModule` cohort:
  92/92 passed.
- Canonical CPU-supported gate: 4,812/4,812 passed; the one LSan-only case was
  expectedly skipped by this selector/build identity.
- Isolated ASan and UBSan grouped CPU gates: 2,655/2,655 passed in each build;
  UBSan is the identity that reproduced the defect before the fix.
- Strict layering, test-layout, docs-link, task-schema/policy, module-inventory,
  and diff-hygiene receipts are recorded under `tasks/evidence/BUG-123/`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure \
  -R '^RuntimeSceneLifecycle\.RetiredQueuedSceneSavePublishesTerminalEvent$' \
  --repeat until-fail:100 --timeout 60
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Shipping a fix without deterministic cancellation-order regression coverage.
- Hiding the race with sleeps, retries, timeout increases, or quarantine.
- Folding unrelated scene-document or runtime cleanup into the bug fix.
