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
and `DrawOperationProgress` ([UI-069](UI-069-shared-operation-progress-widget.md)),
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
- Review follow-up: readiness gates may do real work, so `DrainCompletions` takes a per-drain
  gate-check cap (the engine passes 32 next to the apply budget of 8, in full and minimized
  frames); unchecked gated records stay ahead of the ones just checked, so every parked record is
  checked within ceil(N / cap) drains. `AwaitingApplyJobs` counts every queued record whose gate
  has rejected it at least once (never-checked records stay AwaitingGate)
  (`RuntimeJobService.GateChecksAreCappedPerDrainAndRotateThroughParkedResults`).
- Slice 2 (UI): `TextureBakeMutationUiState` carries an `OperationRunSlot`; the Bake button watches
  the returned run-job token and the controls draw the widget with Cancel (`CancelEditorJob`) for
  that bake's entity. The UV texture tab draws `DrawTextureBakeOutputRun` (the tab output's newest
  run, any surface, with Cancel) above the canvas; the canvas no longer prints "Bake pending."
  (failed and unavailable tabs keep their diagnostic). `OperationRunSlot::Draw` gained an optional
  `onCancel`. The bake test fixture moved to `tests/support/TextureBakeHarness.hpp` so the panel
  test drives a real bake. Test: `SandboxProcessingPanels.TextureBakeControlsShowTheirBakeRunOnlyForItsEntity`
  (Bake press -> run shown, another entity's controls and tab show nothing, the tab finds the run,
  the widget's Cancel ends it and fails the record).

## Acceptance criteria
- [x] Bake jobs carry a correlation id (or an editor identity) so the progress surface resolves them; the bake worker reports progress where a fraction is known.
- [x] The texture bake controls and the UV texture tab draw the widget through a run slot keyed by the submitted bake, shown for its entity only.
- [x] An ImGui test starts a bake and sees the run, then another entity shows nothing.

## Completion

Commit: `8c1ee6e89`, `fbb94e0ae`, `62955173f`, `18a6a4989`, `be2f10e01`, `e50b4d059`. Completed 2026-10-02 with independent Opus reviews per commit; the reviewer confirmed each acceptance tick.
- Each scheduled bake submits one run job that ends exactly once. Bakes fail only on a real device loss.
- Parked results no longer consume the completion-drain apply budget, and gate checks are capped at 32 per drain and rotate through the parked results.
- The bake controls and the UV texture tab show the run through `OperationRunSlot` with run-level Cancel. A Pending tab without an editor run keeps "Bake pending.".
- Full CPU suite 5672/5672.
- Maturity: CPUContracted. The Ready→Published run on a GPU frame and real device-loss evidence are owned by [GRAPHICS-159](../backlog/rendering/GRAPHICS-159-texture-bake-run-and-device-loss-gpu-evidence.md).

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxProcessingPanels|TextureBake' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```
