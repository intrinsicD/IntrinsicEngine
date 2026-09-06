---
id: BUG-173
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Incidental test-only ordering corrections during METHOD-040 verification; no production or public surface changes. The parent run retains the original failures and exact focused/full verification logs."
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: "Existing test seams establish existing job cancellation and stale-source ordering; no catalog contract or production behavior changes."
---
# BUG-173 — Runtime fixtures do not establish cancellation and mutation ordering

## Goal
- Make the two observed asynchronous fixtures deterministic without changing
  their assertions, time limits, or production job behavior.

## Context
- METHOD-040's first full ASan run failed
  `RuntimeJobService.CancelBeforeStartFinalizesOnMainThreadInsteadOfPublishing`
  and `PointCloudConsolidationModule.SourceMutationDropsQueuedWriteback`.
  The parent receipt `tasks/evidence/METHOD-040/commands/asan-ctest-final.json`
  and its stdout retain both failures.
- Cancellation was submitted to two free workers before Cancel, so the job could
  reach AwaitingGate, where only the drain could cancel it. The fixture waited
  for Cancelled before draining. Even worker-visible Cancelled precedes finalizer
  publication; the existing BUG-123 interlock control records that window.
- The source-mutation fixture blocked one worker but mutated in UiBuild. The
  main thread can help execute the job during fixed-step completion, then drain
  and commit it before UiBuild. The observed GraphEdge result was Applied with
  output present and Mutated true, consistent with mutation after commit.
- The fixes use a paused sole worker and a complete worker join for precancel,
  and a Simulation-phase test hook after command capture and before completion
  drain for source mutation. Every original assertion and all eight property
  domains remain. No production path or timing threshold changes.

## Acceptance criteria
- [x] Focused CPU and ASan repeated tests pass with the required ordering explicit.
- [x] Cancellation finalizes exactly once on the main thread without publication.
- [x] Mutation precedes the writeback gate for all eight property domains and
      yields StaleSource without creating the output property.
- [x] Full CPU and isolated sanitizer verification retain all previous failures.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeJobService.CancelBeforeStartFinalizesOnMainThreadInsteadOfPublishing|PointCloudConsolidationModule.SourceMutationDropsQueuedWriteback' --repeat until-fail:20 --timeout 60
cmake --build --preset ci-asan --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci-asan --output-on-failure -R 'RuntimeJobService.CancelBeforeStartFinalizesOnMainThreadInsteadOfPublishing|PointCloudConsolidationModule.SourceMutationDropsQueuedWriteback' --repeat until-fail:20 --timeout 60 --parallel 1
python3 tools/agents/check_task_policy.py --root . --strict
```

## Status

- Completed 2026-09-06.
- Commit: `8c3200f1d` (implementation).
- Both corrected fixtures passed 20 ASan repetitions each and the final full
  canonical CPU selector. Original ASan failures and subsequent focused logs
  remain in METHOD-040 evidence. No production behavior or assertion changed.
