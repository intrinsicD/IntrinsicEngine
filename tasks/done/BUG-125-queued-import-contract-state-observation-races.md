---
id: BUG-125
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "Codex-BUG125"
branch: "codex/bug-125-queued-import-contract-races"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-01T01:09:47Z"
---
# BUG-125 — Queued-import contracts race asynchronous state transitions

## Status
- Completed on 2026-08-01. The world-switch contract holds geometry and model
  decode until the replacement world is active; the initial-state contract
  holds model/texture reads until its cancellability snapshot is complete.
  Both cases passed 100 consecutive repetitions normally and another 100 each
  pinned to one CPU. All 26 format-coverage cases, the 19 concurrency-policy
  checks, the aggregate build, and the 4,010-case CPU gate pass.
- Commit: pending this retirement checkpoint.

## Goal
- Make the queued asset-import world-switch and initial queue-state contracts
  deterministic without weakening their production assertions.

## Non-goals
- No quarantine, retry-only workaround, or production behavior change without
  an independently reproduced production defect.
- Do not reopen `BUG-063`; that retired task fixed cross-test fixture deletion,
  while these failures use unique paths and expose state-observation timing.

## Context
- The RUNTIME-202 hash-bound default-CPU receipt retained at
  `tasks/evidence/RUNTIME-202/observations/ci-full-cpu.json` failed only
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
- [x] Hold geometry/model decode behind deterministic test-only interlocks until
      the active-world transition has committed, then assert both stale results
      fail before any materialization.
- [x] Hold model/texture reads while asserting the initial queue snapshot, then
      release them before driving the completion loop.
- [x] Preserve the terminal state, ingest diagnostic, asset identity, and ECS
      materialization assertions; do not replace them with timing sleeps.

## Tests
- [x] Both exact cases pass at least 100 consecutive repetitions together and
      under a bounded CPU-contention probe.
- [x] The complete `RuntimeAssetImportFormatCoverage` group and canonical
      default CPU gate pass.

## Docs
- [x] Record the diagnosed interlocks and verification evidence here and move
      the issue to the closed bug index when retired.

## Acceptance criteria
- [x] Neither contract relies on the worker remaining in an initial state for
      an unspecified scheduling window.
- [x] The tests still fail if stale world results materialize or if a blocked
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
