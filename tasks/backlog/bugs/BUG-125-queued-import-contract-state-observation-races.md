---
id: BUG-125
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
---
# BUG-125 — Queued-import contracts race asynchronous state transitions

## Goal
- Make the queued asset-import world-switch and initial queue-state contracts
  deterministic without weakening their production assertions.

## Non-goals
- No quarantine, retry-only workaround, or production behavior change without
  an independently reproduced production defect.
- Do not reopen `BUG-063`; that retired task fixed cross-test fixture deletion,
  while these failures use unique paths and expose state-observation timing.

## Context
- The RUNTIME-202 hash-bound default-CPU receipt
  `tasks/evidence/RUNTIME-202/commands/ci-full-cpu.json` failed only
  `RuntimeAssetImportFormatCoverage.QueuedImportsRejectActiveWorldSwitchBeforeApply`
  and
  `RuntimeAssetImportFormatCoverage.ManualModelSceneAndTextureImportQueueCompletesThroughStreaming`.
- In the first case, the geometry result applied before the deferred active
  world switch, while the model result observed the switch and failed. The test
  queues both workers and requests the switch without holding decode/apply
  behind an ordering interlock.
- In the second case, the first queue entry had already advanced beyond a
  cancellable `Decoding` state before the immediate snapshot assertion. The
  test assumes an asynchronous worker cannot finish between submit and
  snapshot.
- RUNTIME-202 changes only this file's retired-facade imports and focus-command
  construction; it does not touch either failing test body or asset workflow.
  Each exact failing case subsequently passed 20/20 when repeated alone, which
  confirms schedule sensitivity but does not repair it.

## Required changes
- [ ] Hold geometry/model decode behind deterministic test-only interlocks until
      the active-world transition has committed, then assert both stale results
      fail before any materialization.
- [ ] Hold model/texture reads while asserting the initial queue snapshot, then
      release them before driving the completion loop.
- [ ] Preserve the terminal state, ingest diagnostic, asset identity, and ECS
      materialization assertions; do not replace them with timing sleeps.

## Tests
- [ ] Both exact cases pass at least 100 consecutive repetitions together and
      under a bounded CPU-contention probe.
- [ ] The complete `RuntimeAssetImportFormatCoverage` group and canonical
      default CPU gate pass.

## Docs
- [ ] Record the diagnosed interlocks and verification evidence here and move
      the issue to the closed bug index when retired.

## Acceptance criteria
- [ ] Neither contract relies on the worker remaining in an initial state for
      an unspecified scheduling window.
- [ ] The tests still fail if stale world results materialize or if a blocked
      active import is not cancellable.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure \
  -R 'RuntimeAssetImportFormatCoverage\.(QueuedImportsRejectActiveWorldSwitchBeforeApply|ManualModelSceneAndTextureImportQueueCompletesThroughStreaming)$' \
  --repeat until-fail:100 --timeout 60
ctest --test-dir build/ci --output-on-failure \
  -R '^RuntimeAssetImportFormatCoverage\.' --timeout 60
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Quarantining or loosening the cases instead of controlling their asynchronous
  transition boundary.
- Treating a later passing rerun as proof that the recorded failure did not
  occur.
