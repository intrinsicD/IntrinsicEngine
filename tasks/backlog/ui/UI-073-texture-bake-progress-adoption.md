---
id: UI-073
theme: F
depends_on: [UI-069]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive UI/runtime slice; evidence is the diff, ImGui tests, review and CI.
contract_schema: 1
contracts: [runtime.editor-prepared-frame-locality, repo.source-documentation]
---
# UI-073 — Texture bake panels use the shared operation progress widget

## Goal
The texture bake panels show their run's progress through `OperationRunSlot`
and `DrawOperationProgress` ([UI-069](../../done/UI-069-shared-operation-progress-widget.md)),
replacing the "Bake pending." overlay text.

## Context
- `Runtime.TextureBakeModule.cpp` submits its jobs straight to `JobService` with
  neither an editor identity nor a correlation id, so `EditorJobCommandSurface::Progress`
  cannot see them. `PropertyTextureBakeResult::Job` already carries the token.
- Left out of UI-069 slice 4 for that reason; the other method panels are
  adopted or synchronous (see the UI-069 note).

- Correction (2026-10-02): the module did not submit to `JobService` at all; a bake is
  GPU-queue participant work and `PropertyTextureBakeResult::Job` was never set.

## Implementation log
- Slice 1 (runtime): every scheduled bake submits one run job (`MakeBakeRunJobDesc`, empty
  work, parked by `IsReadyToApply` until the shared `BakeRun` settles). The queued work owns
  the run through `BakeRunHandle`, which settles Failed on any path that drops the work
  (stale target, detach/scene replacement, shutdown, recording failure); Ready settles on
  publication, a rebake or removal settles Cancelled. The run job's own cancel marks the run
  abandoned and `WithdrawStoppedWork` (maintenance and both GPU-queue callbacks) withdraws the
  work, failing the record as cancelled; the same sweep fails all work once the device is no
  longer operational. The job is submitted after every side-effect-free rejection, so a
  rejected or retried request submits nothing. `TextureBakeService::Bake` takes an optional
  `RunJobSubmitter`; `ApplyEditorTextureBakeCommand` passes `EditorJobCommandSurface::Submit`
  with `EditorJobIdentity{entity, source scope, target semantic, output}`. No fraction is known
  (one GPU pass), so the run reads indeterminate. A failed run projects as `stale_discarded`
  until RUNTIME-317 carries diagnostics. Agent parity: the agent lane has no bake tool; its
  `jobs_*` tools see the run like any editor job. Tests: `RuntimeTextureBakeModule.ScheduledBakeIsOneRunJobThatEndsWithItsOwnBake`,
  `CancellingTheRunJobWithdrawsTheBakeOnce`, `RunJobsEndOnDeviceLossSceneReplacementAndShutdown`,
  `EditorBakeRunResolvesThroughTheJobSurfaceForItsEntityOnly` (bake test harnesses now run the
  shared scheduler and provide `JobService`). Pending: the Ready path (Published) needs a
  recorded GPU frame; Vulkan evidence (`PropertyTextureBakeGpuSmoke`) is pending on a GPU host.
- Slice 1 review fixes: (1) `JobService::DrainCompletions` no longer charges parked records to
  the apply budget, so many parked bake runs (imports, automatic appearance bakes) never starve
  other completions, also while minimized (`RuntimeJobService.ParkedResultsDoNotConsumeTheApplyBudget`).
  (2) Only a lost device fails in-flight bakes: new `RHI::IDevice::IsDeviceLost` (Vulkan:
  `VK_ERROR_DEVICE_LOST`); a device that is merely not operational (swapchain, unclean recipe
  validation) keeps them waiting. (3) A cancel accepted after the bake settled Ready but before the
  next drain ends the run Cancelled with a Ready output (the job service checks cancels first);
  documented, not changed. (4) A refused run-job submission (shutdown, no scheduler) is a recorded
  `BakeFailed`, not the transient `JobSubmitFailed`, so the appearance producer backs off instead of
  retrying every frame. (5) Rebake is latest-wins (documented difference from RUNTIME-313).

## Acceptance criteria
- [x] Bake jobs carry a correlation id (or an editor identity) so the progress surface resolves them; the bake worker reports progress where a fraction is known.
- [ ] The texture bake controls and the UV texture tab draw the widget through a run slot keyed by the submitted bake, shown for its entity only.
- [ ] An ImGui test starts a bake and sees the run, then another entity shows nothing.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxProcessingPanels|TextureBake' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```
