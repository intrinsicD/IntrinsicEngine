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

## Resolution — 2026-10-24
- Root cause: `RecordPositionPreviews` treated a recorded `CommitOnCopy` copy as executed. It
  cleared `PositionShadowStale` and erased the preview, which retired the front lease at record
  time. `CommitGeometryPositions::holdsFront` likewise trusted `CopiedStamp`, also set at record
  time, so Accept of a preview copy lost to a failed frame answered `Committed`. Nothing re-copied
  after a plain submit failure: `RebuildGpuResources` runs only on device loss or an operational
  transition.
- Seam used (no new API): `VulkanDevice::EndFrame` advances `m_GlobalFrameNumber` only after a
  successful submit (`Backends.Vulkan.Device.cpp` ++ at :3137/:3175; the FailedSubmit returns
  skip it). The renderer already infers submission the same way (`globalFrameAdvanced`,
  `Graphics.Renderer.cpp` ~:8747). `MockDevice::AdvanceGlobalFrameOnEndFrame=false` models a
  failed submit.
- Change, `src/graphics/renderer/Graphics.GpuWorld.cpp` only (no `.cppm` or RHI change, no new
  imports):
  - `PositionPreview::CopiedFrame` records the global frame number of the last recorded copy.
  - The record-time commit erase is gone.
  - `Impl::SettleRecordedCopies()` runs first in `GpuWorld::SyncFrame`. A copy whose frame did
    not advance (`now <= CopiedFrame`) is re-armed (`Recopy`). A submitted commit copy clears
    `PositionShadowStale`, retires the lease at `CopiedFrame` and ends the preview.
  - `holdsFront` also requires `GetGlobalFrameNumber() > CopiedFrame`, because Accept runs in
    extraction before that `SyncFrame`.
- Defaults chosen (Delegate posture):
  - The restore is a re-copy while the lease is held, not a shadow upload. It reuses the
    existing `Recopy`/lease/retire machinery.
  - The lease is retired at `CopiedFrame`, not at the settle frame. This keeps today's release
    timing (released once `now - CopiedFrame > FramesInFlight`).
  - The `Graphics.GpuWorld.cppm` comment on `CopyPending` ("one copy ... at the next culling
    head") is left unchanged. It still holds for a submitted frame, and leaving it avoids
    rebuilding every importer. `docs/api/generated/module_inventory.md` is unaffected.
- Tests, `tests/contract/graphics/Test.GpuWorldPositionPreviewContract.cpp`:
  - New fixture helper `EndFrame(bool submitted)`.
  - New tests:
    - `GpuWorldPositionCommitContract.ACommitCopyWhoseFrameWasNotSubmittedIsCopiedAgain`
    - `GpuWorldPositionCommitContract.AcceptAfterAFailedSubmitDoesNotKeepTheLostPreviewCopy`
    - `GpuWorldPositionPreviewContract.APreviewCopyLostToAFailedSubmitIsRecordedAgain` (with
      controls: a submitted copy is not repeated, and a later failed frame that recorded no copy
      does not re-copy)
  - Frame-model edits to existing tests. Their fixtures never modelled submission, so each edit
    inserts a submitted `EndFrame(true)` (plus `SyncFrame` where a settled state is asserted).
    No assertion was removed or loosened. All edits pass on the unfixed code too.
    - `CompactionDoesNotReplayAStaleShadowAndRecopiesTheFront`: added `EndFrame(true)` before its
      `SyncFrame`. Without it, the settle re-arm masks the compaction re-copy it guards (shown below).
    - `CommitOfACopiedFront...`: added `EndFrame(true)` before the Commit.
    - `CommitWithoutACopiedFront...` and `ARecycledSlotRepublished...`: added
      `EndFrame(true)`+`SyncFrame` before each `EXPECT_FALSE(PositionShadowStale)`, and
      `EndFrame(true)` before the control Commit.
    - `APendingCopyHoldsTheFrontLease...`: the loop advances before each `SyncFrame`. The old
      first `SyncFrame` without an advance now means "submit failed". The release still happens
      at recorded+3, as before.
- Verification (build/ci, raw logs in `tasks/evidence/BUG-232/`, git-ignored):
  - Fail-first: the tests-only working tree (fix not applied, test diff sha256 a0d766cd...; the final test file differs from it only in the RUNTIME-311 lease-test comment),
    run with `ctest --test-dir build/ci -R 'GpuWorldPosition' --output-on-failure --timeout 60`,
    gave 18/21 passed. Exactly the 3 new tests failed, as predicted in writing beforehand:
    - 2 copies instead of 3;
    - stale=false right after the record, and 1 copy instead of 2;
    - `Committed` instead of `CopyPending`.
  - With the fix, the same ctest gave 21/21 passed.
  - `build/ci/bin/IntrinsicGraphicsContractCpuTests --gtest_brief=1` gave 579/579 passed
    (baseline 576/576).
  - Mutation checks, each rebuilt with the fix otherwise applied:
    - Without the `holdsFront` frame check, `AcceptAfterAFailedSubmit...` fails (20/21).
    - Without the settle re-arm, `ACommitCopyWhoseFrame...` and `APreviewCopyLost...` fail (19/21).
    - Without the compaction replay re-arm (`ReplayManagedUpload` `Recopy = true`),
      `CompactionDoesNotReplay...` fails (20/21). With that test's new `EndFrame(true)` also
      removed, the same mutation passes (1/1), which is why the edit is needed.
    - After restoring the fixed file (sha256 6ba8d182...): 21/21 and 579/579.
  - `python3 tools/repo/check_layering.py`: no violations.
  - `jscpd --min-lines 6` on both touched files: no clone involves the new code in
    `Graphics.GpuWorld.cpp`. In the test file, two short clones of existing setup idioms
    (7 lines each) are left as they are.
- Remains / unchecked:
  - The real Vulkan recovery after a non-device-lost submit failure has not been run. The fix
    depends only on the frame number not advancing.
  - Vulkan smokes (`GpuPositionsAcceptSmoke`, which waits 8 settle frames before
    `EXPECT_FALSE(PositionShadowStale)`) are CI-owned and were not run.
  - No ASan tree.
  - Runtime contract tests were not run, because their binary is not built in this tree. They
    are unaffected:
    - `PositionPreviewExtraction` uses the headless Null device, which is not operational, so
      GpuWorld records no copies there.
    - `GpuPositionsAccept` uses `MockDevice` only for `GpuPropertyResidency`, without GpuWorld.
  - Behaviour changes for review:
    - A submitted commit copy clears `PositionShadowStale` at the next `SyncFrame` rather than at
      record time.
    - If the copy's frame fails with device loss, the rebuild no longer replays the patched
      shadow for that range. The commit preview is still pending (stale, lease held), so the
      replay skips the range and the front is re-copied. This matches the documented
      `RebuildGpuResources` intent ("pending copies keep their leases and are recorded again").
