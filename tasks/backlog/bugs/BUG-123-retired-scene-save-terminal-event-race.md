---
id: BUG-123
theme: G
depends_on: []
---
# BUG-123 — Retired queued scene save intermittently loses its terminal event

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
- [ ] Build a deterministic interlock that exercises cancellation before and
      after worker completion without wall-clock sleeps.
- [ ] Identify which cancellation/completion ordering loses the unpublished
      finalizer and make terminal publication exactly-once for every ordering.
- [ ] Preserve `JobState::Cancelled`, event sequence/path/task/error fields,
      and the no-second-event drain assertion.

## Tests
- [ ] The deterministic regression fails on the pre-fix ordering and passes
      after the fix.
- [ ] The exact existing contract passes at least 100 focused repetitions.
- [ ] The complete default CPU-supported gate passes.

## Docs
- [ ] Record the causal ordering and verification evidence in this task and
      the retirement log; no architecture-doc change is required unless the
      production cancellation contract changes.

## Acceptance criteria
- [ ] Every retired queued scene save publishes exactly one terminal event.
- [ ] No timeout, label, quarantine, or assertion is weakened.
- [ ] The fix introduces no runtime layering violation.

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
