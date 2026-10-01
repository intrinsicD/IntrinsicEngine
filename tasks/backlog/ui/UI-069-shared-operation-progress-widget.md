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
- Cancel stays with [RUNTIME-279](../runtime/RUNTIME-279-editor-job-snapshot-and-cancel.md).
  The widget shows Cancel only when the read model says `CanCancel`.
- [UI-060](UI-060-jobs-window.md) should reuse the widget and projection for its
  rows.
- The agent side is RUNTIME-312 slice 8
  ([RUNTIME-312](../runtime/RUNTIME-312-agent-lane-mcp-hardening-and-coverage.md)).

## Slice plan
1. **Read model.**
   - Add `EditorOperationProgress` {State None/Queued/Running/Succeeded/Failed/Cancelled, Determinate, Normalized, ElapsedSeconds, Label, Diagnostic, CanCancel}.
   - Add `EditorOperationRunKey` {Identity | CommandCorrelationId}, pure projections from `EditorJobRecord`/`JobSnapshot`, and `EditorJobCommandSurface::Progress(key)` bound next to `FindActive`.
   - K-Means and consolidation record correlation → job token at submit. Route them through `EditorJobCommandSurface::Submit` if layering allows.
   - A job that never reported progress projects as indeterminate.
   - The K-Means and mesh-field solver workers call `ReportProgress`.
2. **Widget.**
   - Move `ProgressOverlayText` into PanelSupport as a shared `FormatProgressOverlay`; the asset queue uses it.
   - Add `DrawOperationProgress(const EditorOperationProgress&, onCancel)` with a determinate or animated indeterminate bar, a percent/label + elapsed overlay, the diagnostic, and Cancel only if `CanCancel`. It draws nothing for `None`.
   - First adopter: a mesh-field panel.
3. **Representative adoption.** UV regeneration (replaces the text line and the DerivedJob cell), K-Means (correlation key), and registration/CPD (wraps `EditorRegistrationProgress`; its trace stays panel-specific).
4. **Mechanical adoption.** One chunk each:
   - normals/outliers/density/descriptors/curvature segmentation
   - consolidation/Poisson
   - GPU transaction panels (phase → indeterminate Running)
   - texture bake/keypoints

## Acceptance criteria
- [ ] The read-model types live in `Runtime.EditorJobProjection.cppm`. `EditorJobCommandSurface::Progress(key)` resolves an identity or a correlation id to that run's own job, and returns `State::None` for unknown, stale-epoch or pruned keys.
- [ ] K-Means and point-cloud consolidation runs resolve by correlation id, never by "oldest job".
- [ ] A job that has not reported progress projects as indeterminate, never as 0%. The K-Means and mesh-field solver workers report progress.
- [ ] `DrawOperationProgress` (Sandbox.PanelSupport) meets slice 2; the overlay text helper is shared with the asset import queue, with no second copy.
- [ ] Mesh-field, UV regeneration, K-Means and registration/CPD panels use the widget.
- [ ] The remaining method panels use the widget, or each is listed here with its owning follow-up.
- [ ] Tests:
  - projection and lookup contract in `Test.SandboxEditorSessionLifecycle.cpp` (`SandboxEditorJobHarness`): queued, determinate, never-reported, failed, cancelled; own job wins over an older running one; correlation key; stale epoch.
  - ImGui widget test following `Test.SandboxProcessingPanels.cpp`: determinate, indeterminate, Cancel visibility and callback.
- [ ] Docs: `docs/architecture/sandbox-editor-feature-boundaries.md` and the module inventory are current.

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
