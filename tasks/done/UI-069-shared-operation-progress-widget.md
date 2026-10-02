---
id: UI-069
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive multi-slice UI/runtime work; evidence is the diff, contract and ImGui tests, review and CI.
contract_schema: 1
contracts: [runtime.editor-prepared-frame-locality, repo.source-documentation]
---
# UI-069 — Shared operation progress read model and panel widget

## Completion — 2026-10-02
Commit: `b7350b6e5` (last of `cbb8a0c7e`, `1301ce3f3`, `f62dc80e8`, `7bc2a13b7`, `cd6beb59b`, `3f7f25b66`,
`b7350b6e5` and the review-fix commits between them). One read model
(`EditorOperationProgress`, `EditorJobCommandSurface::Progress(key)` over a job token, an
output identity or ref, or a correlation id), one widget and one per-panel pattern
(`OperationRunSlot`). The agent lane reads the same model (RUNTIME-312 slice 8).
Panel coverage and exceptions are listed under the acceptance criteria; texture bake moved
to [UI-073](../backlog/ui/UI-073-texture-bake-progress-adoption.md). CPU/null and ImGui
tests only: no live Vulkan check. CPUContracted.

## Goal
Every Sandbox method panel shows the progress of its own running operation through
one shared widget. The widget reads one runtime read model, and the agent lane's
MCP progress notifications read the same model.

## Context
- Operator request 2026-10-01, during RUNTIME-312 slice 5 (agent progress).
- Today no production job calls `JobService::ReportProgress`
  (`Runtime.JobService.cpp`), so every job reads as determinate 0% until it
  finishes. Panels show progress as ad-hoc text at best: the UV job line and
  DerivedJob cells in `Sandbox.PanelSupport.cpp`, and `Pending`/`Queued` strings
  in the method panels. Only the asset import queue draws a bar
  (`Sandbox.EditorShell.cpp`, private `ProgressOverlayText`).
- Run lookup differs by family:
  - Editor-job operations (mesh-field solvers, UV, ICP, descriptors, outliers,
    density, bilateral, keypoints, point sampling) resolve by `EditorJobIdentity`
    through `EditorJobCommandSurface::FindActive`.
  - K-Means and consolidation submit to `JobService` directly. They drop the job
    token and keep only a `CommandCorrelationId`.
  - Registration/CPD has its own `EditorRegistrationProgress` with cancel.
  - GPU transactions expose only a phase.
- Owner: `Runtime.EditorJobProjection.cppm`, which already holds
  `EditorJobRecord`. No new service or registry.
- Cancel stays with [RUNTIME-279](../backlog/runtime/RUNTIME-279-editor-job-snapshot-and-cancel.md).
  The read model carries no `CanCancel` until that task adds `Cancel` to the job
  surface; the widget shows Cancel while a run is active and the panel supplies a
  cancel path (registration/CPD now).
- [UI-060](../backlog/ui/UI-060-jobs-window.md) should reuse the widget and projection for its
  rows.
- The agent side is RUNTIME-312 slice 8
  ([RUNTIME-312](../backlog/runtime/RUNTIME-312-agent-lane-mcp-hardening-and-coverage.md)).

## Slice plan
1. **Read model.**
   - Add `EditorOperationProgress` {State None/Queued/Running/Succeeded/Failed/Cancelled, Determinate, Normalized, ElapsedSeconds, Label, Diagnostic}.
   - Add `EditorOperationRunKey` {JobToken | Identity | EditorRunCorrelation}, pure projections from `EditorJobRecord`/`JobSnapshot`, and `EditorJobCommandSurface::Progress(key)` bound next to `FindActive`. A token names one job; an identity names an output (newest run); a correlation id names a service run.
   - K-Means and consolidation record correlation → job token at submit. Route them through `EditorJobCommandSurface::Submit` if layering allows.
   - A job that never reported progress projects as indeterminate.
   - The K-Means and mesh-field solver workers call `ReportProgress`.
2. **Widget.**
   - Move `ProgressOverlayText` into PanelSupport as a shared `FormatProgressOverlay`; the asset queue uses it.
   - Add `DrawOperationProgress(const EditorOperationProgress&, onCancel, id)` with a determinate or animated indeterminate bar, a percent/label + elapsed overlay, the diagnostic, and Cancel only while the run is active and the panel supplies `onCancel`. It draws nothing for `None`. `OperationRunSlot` (per panel) keeps the last finished projection (the runtime reaps a job a frame after it ends).
   - First adopter: a mesh-field panel.
3. **Representative adoption.** UV regeneration (replaces the text line and the DerivedJob cell), K-Means (correlation key), and registration/CPD (wraps `EditorRegistrationProgress`; its trace stays panel-specific).
4. **Mechanical adoption.** One chunk each:
   - normals/outliers/density/descriptors/curvature segmentation
   - consolidation/Poisson
   - GPU transaction panels (phase → indeterminate Running)
   - texture bake/keypoints

## Acceptance criteria
- [x] The read-model types live in `Runtime.EditorJobProjection.cppm`. `EditorJobCommandSurface::Progress(key)` resolves a job token, an identity (newest run of that output) or a correlation id to that run's own job, and returns `State::None` for unknown, stale-epoch or pruned keys.
- [x] K-Means and point-cloud consolidation runs resolve by correlation id, never by "oldest job" (`ClusteringModule.CancelledCpuWorkPublishesCanonicalCompletion`, `PointCloudConsolidationModule.SourceMutationDropsQueuedWriteback`, the K-Means and consolidation panel tests).
- [x] A job that has not reported progress projects as indeterminate, never as 0%. The K-Means and mesh-field solver workers report progress (K-Means per Lloyd iteration, implicit smoothing per chained solve; the other mesh-field operations are single-pass or synchronous and stay indeterminate).
- [x] `DrawOperationProgress` (Sandbox.PanelSupport) meets slice 2; the overlay text helper is shared with the asset import queue, with no second copy. Panels keep the last finished projection of their current run in an `OperationRunSlot` (dropped on scene replacement through the session scene epoch); ICP/CPD report finished runs through their own result and phase lines.
- [x] Mesh-field (property smoothing), UV regeneration, K-Means and registration/CPD panels use the widget.
- [x] The remaining method panels use the widget (through `OperationRunSlot`: the run key is captured at submit, shown for its entity only, a GPU transaction waiting for Accept reads "awaiting accept", Discard forgets), or are listed here:
  - Adopted: normals, outliers, keypoints, descriptors, kernel density, density weights, bilateral filter, point construction, Progressive Poisson, consolidation, K-Means, mesh denoise/remesh/subdivide/simplify, mesh curvature, property smoothing, UV regeneration, ICP, CPD.
  - Synchronous, no job to show: curvature segmentation, geodesics, scalar gradient, harmonic field, Laplacian eigenbasis, scalar ridges, and CPU point sampling (its Vulkan path is a job under the same output key). Point spacing is adopted (its Vulkan transaction is a job). Mesh simplify is adopted with the same code as its siblings but has no panel test of its own (its target-face edit needs a long ImGui script); awaiting-accept is tested through the injected smoothing transaction (no seam exists for a real GPU transaction on the null device).
  - Left to [UI-073](../backlog/ui/UI-073-texture-bake-progress-adoption.md): texture bake panels, whose module jobs carry neither identity nor correlation id yet.
- [x] Tests:
  - projection and lookup contract in `Test.SandboxEditorSessionLifecycle.cpp` (`SandboxEditorJobHarness`): `OperationProgressProjectsQueuedRunningNeverReportedAndOwnJob`, `OperationProgressReportsFailedAndCancelledRuns`, `OperationProgressSeparatesNewerRunsTokensAndCorrelationOnlyJobs`, `OperationProgressResolvesCorrelationKeysToTheRunsOwnJob`, and the real-session `OperationProgressRejectsStaleEpochHandlesAndFindsServiceRunsByCorrelation` (stale epoch, scene epoch). `Test.RuntimeJobService.cpp` covers never-reported and worker reports; `Test.ClusteringModule.cpp` the K-Means run's correlation id and its queued projection; `Test_PointCloud.cpp` (`PointCloud_KMeans`) the iteration observer.
  - ImGui tests in `Test.SandboxProcessingPanels.cpp`: `OperationProgressViewFollowsTheReadModel`, `OperationRunSlotKeepsTheLastFinishedRunUntilTheKeyOrTheSceneChanges`, `OperationProgressWidgetCancelRequiresAnActiveRunAndAHandler` (determinate, indeterminate, None, Cancel visibility and callback), `IterationProgressIsADeterminateFractionOfTheIterationCap`, `KMeansProgressShowsOnlyForTheEntityItRanOn`, `IcpProgressWidgetCancelStopsARunThatNeverConverges` (asserts the cancelled result), `CpdProgressWidgetCancelStopsARunToTheCap` (asserts the Cancelled phase), `DerivedJobCellsAndUvStatusLineShowTheSharedWidget`, and `PropertySmoothingAcceptsOrDiscardsAPendingGpuResult` (accepted run stays "done", discarded one does not, a waiting result reads "awaiting accept"). Slice 4: `OperationRunSlotCapturesTheKeyAtSubmitAndMapsTransactionPhases`, `ReusedExecutionPanelsRejectInvalidRequestsBeforePublishing` (keypoints, descriptors, weights, bilateral, normals: run shown, hidden for another entity), `OutlierPanelShowsItsFinishedRunOnlyForItsEntity`, `PointConstructionPanelShowsItsFinishedRunOnlyForItsEntity`, `KernelDensityPanelShowsItsRunForTheOutputItWrites`, `ProgressivePoissonPanelShowsItsFinishedRun`, `ConsolidationPanelShowsItsFinishedRunOnlyForItsEntity`, `SubdividePanelShowsItsFinishedRunOnlyForItsEntity`, `RemeshPanel...`, `DenoisePanel...`, `CurvaturePanel...` (pins the serialized-command key), and `ARunStartedElsewhereShowsForTheDraftsOutput` (the agent-run fallback); `OperationProgressFindsRunsByEntityAndOutputName` (session lookup).
- [x] Docs: `docs/architecture/sandbox-editor-feature-boundaries.md`, `agent-control-lane.md` and the module inventory are current.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorSessionLifecycle|SandboxProcessingPanels|EditorJob|AssetImportQueue|Clustering|PointCloudConsolidation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```
