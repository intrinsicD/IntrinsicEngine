---
id: BUG-232
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive follow-up found in RUNTIME-311 review; evidence is the diff, tests, and CI
contract_schema: 1
contracts: []
contract_review: A frame-submission failure path of the GpuWorld position commit; no catalog contract covers device submit failure recovery.
---
# BUG-232 — A failed frame submit loses an accepted position copy

## Goal
- An accepted GPU position front whose block copy was recorded in a frame whose submission
  fails is copied again (or uploaded from the shadow) instead of leaving the block with older
  bytes than the shadow describes.

## Context
- Found in the RUNTIME-311 slice 5 review (pre-existing, not changed there).
- `GpuWorld::CommitGeometryPositions` (`src/graphics/renderer/Graphics.GpuWorld.cpp`) answers
  `CopyPending` when the block does not hold the accepted front: the shadow already holds the
  accepted bytes and `PositionShadowStale` is set until one copy front -> block runs at the next
  culling head.
- `RecordPositionPreviews` records that copy and, for a `CommitOnCopy` preview, immediately
  clears `PositionShadowStale` and erases the preview (around `if (preview.CommitOnCopy)`), i.e.
  it treats the recorded copy as executed.
- If the frame's EndFrame/submit then fails (device loss is handled by `RebuildGpuResources`,
  which replays the shadow; a plain submit failure is not), the copy never runs: the block keeps
  its older bytes, the shadow says it is current, and nothing re-copies or re-uploads it.
- Since RUNTIME-311 the preview's front lease is retired at that erase and released after the
  frames in flight, so a later retry could no longer read the front either; a fix must keep the
  lease (or fall back to a shadow upload) until the copy's frame is known to have executed.

## Acceptance criteria
- [ ] A recorded commit copy is only treated as done once its frame's submission succeeded;
      otherwise the block is restored (re-copy while the lease is held, or a shadow upload).
- [ ] A contract test with the mock device simulates a failed submit after the copy was recorded
      and observes the restore.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci -R 'GpuWorldPosition' --output-on-failure --timeout 60
```
