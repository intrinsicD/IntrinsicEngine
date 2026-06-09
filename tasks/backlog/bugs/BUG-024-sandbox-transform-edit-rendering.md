---
id: BUG-024
theme: G
depends_on: []
maturity_target: CPUContracted
---
# BUG-024 — Sandbox transform UI edits do not move rendered triangle

## Goal

- Make edits to the selected entity's local transform through the promoted Sandbox Editor Inspector visibly move the `ReferenceTriangle` renderable through the promoted runtime/render path.

## Non-goals

- No legacy renderer or `src/legacy` fallback work.
- No geometry vertex mutation to simulate transforms; the entity transform/model matrix must drive placement.
- No new editor gizmo features beyond preserving existing gizmo transform behavior.
- No graphics-layer import of live ECS, runtime, UI, or asset services.
- No broad camera-controller or selection redesign.

## Context

- Symptom: dragging the Inspector `Local position` field for the selected triangle entity changes the authored transform state but the visible triangle appears stationary.
- Expected behavior: the edit should update `Transform::Component`, recompute `Transform::WorldMatrix`, forward `DirtyTags::DirtyTransform`, update the renderer's `TransformSyncRecord::Model`, upload `GpuInstanceDynamic::Model`, and move the surface/selection/picking draw in the next rendered frame.
- Owning subsystem/layer: `runtime` owns UI/editor command routing, ECS system scheduling, render extraction, and the ECS-to-graphics handoff. `ecs` owns transform hierarchy and render-sync tag systems. `graphics` remains snapshot/SSBO/shader-only and must not read live ECS.
- Current code evidence from the analysis pass:
  - `Runtime.SandboxEditorUi.cpp` applies `SandboxEditorTransformEditCommand` by mutating `ECS::Components::Transform::Component` and stamping `Transform::IsDirtyTag`; the live engine path routes through `EditorCommandHistory`, which stamps the same tag.
  - `Engine::RunFrame()` currently runs the promoted fixed-step ECS bundle before `OnVariableTick()` and before the ImGui editor callback. Inspector edits and `GizmoInteraction` mutations therefore happen after the scheduled `TransformHierarchy`/`RenderSync` pass for that frame.
  - `RenderExtractionCache::ExtractAndSubmit()` submits `Graphics::TransformSyncRecord{ .Model = world.Matrix }`, so a stale `Transform::WorldMatrix` produces a stale model matrix even though the authored local transform changed.
  - `TransformSyncSystem::SyncGpuBuffer()` writes `GpuWorld::SetInstanceTransform(...)`, and the promoted `forward/default_debug_surface.vert`, `forward/line.vert`, `forward/point.vert`, and selection ID shaders load `GpuInstanceDynamic::Model`. The first-pass evidence does **not** support a blanket claim that promoted shaders ignore the model matrix.
  - Existing tests cover local dirty stamping (`SandboxEditorUi.TransformEditCommandMutatesLocalTransformAndMarksDirty`), fixed-step transform scheduling (`FrameGraphSystems.TransformSystem_RegistersAndExecutes`), and extraction-side transform submission counts (`RuntimeRenderExtraction.CreatesUpdatesAndClearsDirtyTransformSidecar`), but no test covers UI edit → pre-render world-matrix refresh → rendered/snapshot movement.
- Ranked hypotheses to test before fixing:
  1. **Most likely:** post-fixed-step editor/gizmo mutations are not flushed before render extraction, so extraction reads the old `WorldMatrix`. Prediction: after an inspector-command edit, a same-frame CPU/null engine test observes the edited local transform and `IsDirtyTag`, but the render snapshot/model remains at the old world translation unless a pre-render transform flush is inserted.
  2. The fixed-step bundle eventually updates `WorldMatrix`, but the visual report is caused by frame-latency/staleness during continuous UI dragging. Prediction: running a deterministic extra transform/render-sync pass after UI/gizmo edits makes movement immediate without shader changes.
  3. `GpuWorld`/culling state receives the new model but stale bounds or indirect draw state keeps the old visual/culling result. Prediction: a CPU render snapshot shows the new model while a Vulkan pixel-shift test still fails; then the fix belongs in bounds/culling sync, not UI.
  4. A specific pass or shader used by the active recipe is not the inspected GpuScene-aware shader. Prediction: SPIR-V/source inventory or render-graph pass diagnostics show a non-GpuScene shader path for the triangle; fix only that offending pipeline pairing.
- Trace result (2026-06-09): hypothesis 1 is confirmed for the CPU/runtime path, and hypothesis 2 is the immediate fix shape. Temporary focused probes (removed after diagnosis) showed:
  - `ApplySandboxEditorTransformEdit(...)` updates `Transform::Component::Position` and leaves `Transform::IsDirtyTag` set, while `Transform::WorldMatrix` remains at the old translation until `TransformHierarchy::OnUpdate(...)` runs again.
  - Before that flush, `RenderExtractionCache::ExtractAndSubmit(...)` plus `Renderer::ExtractRenderWorld(...)` publish a `RenderableSnapshot::Model` with the stale translation; after the manual `TransformHierarchy` → `BoundsPropagation` → `RenderSync` sequence, the snapshot model matches the edited position and extraction drains `DirtyTags::DirtyTransform`.
  - Focused probe commands run during diagnosis: `cmake --build --preset ci --target IntrinsicRuntimeContractTests`, `ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUiBug023Probe\\.TransformEditAfterScheduledSystemsLeavesWorldMatrixStaleUntilFlush' --timeout 60`, `cmake --build --preset ci --target IntrinsicRuntimeGraphicsCpuTests`, and `ctest --test-dir build/ci --output-on-failure -R 'RuntimeRenderExtractionBug023Probe\\.UiTransformEditStaleWorldMatrixReachesRenderWorldUntilFlush' --timeout 60`.
  - Hypotheses 3 and 4 are not supported by the CPU trace: the stale value is already present before GPU culling/shader execution, and the inspected promoted surface/line/point/selection shaders all read `GpuInstanceDynamic::Model`.

## Maturity

- Target: `CPUContracted` for the deterministic UI/edit-to-render-snapshot contract; `Operational` on Vulkan-capable hosts once the opt-in pixel-shift smoke passes.
- If the implementation lands CPU/null-only first, `Operational` owned by a follow-up `BUG-024B` task unless this task also cites the `gpu;vulkan` run.

## Required changes

- [ ] Add a failing CPU/null repro that drives the real editor-command mutation after the fixed-step phase and asserts the render-facing model/world state changes before extraction/prepare.
- [ ] Add or extend a runtime-owned pre-render transform flush so post-fixed-step mutations from `OnVariableTick()`, Sandbox Editor UI, and `GizmoInteraction` run through `TransformHierarchy`, `BoundsPropagation`, and `RenderSync` before `RenderExtractionCache::ExtractAndSubmit()` observes the scene.
- [ ] Ensure the flush stays in `runtime` and uses ECS system seams without adding live ECS reads to `graphics`.
- [ ] Verify `Transform::WorldMatrix`, `DirtyTags::DirtyTransform`, `RenderWorld::Renderables[*].Model`, and extraction stats all agree after an inspector transform edit.
- [ ] If CPU snapshot state updates but Vulkan pixels do not move, inspect default-recipe pass diagnostics, culling buckets, and shader/pipeline pairing before changing shaders.
- [ ] Keep gizmo packets aligned with the flushed world matrix by placing the flush before transform-gizmo packet build and render extraction, or by adding a second guarded flush after gizmo mutation.

## Tests

- [ ] Add a `contract;runtime` or `integration;runtime` CPU test that exercises `ApplySandboxEditorTransformEdit(...)` or an equivalent live engine editor-command callback after the normal fixed-step bundle and before render extraction.
- [ ] Extend `RuntimeRenderExtraction` or renderer contract coverage to assert the submitted/render-world model translation, not only `SubmittedTransformCount`.
- [ ] Add an opt-in `gpu;vulkan;runtime;regression` smoke in `Test.RuntimeSandboxAcceptanceGpuSmoke.cpp` that edits `ReferenceTriangle` via the UI/editor command path (not the existing helper that writes `WorldMatrix` directly), runs enough frames for the pre-render flush, and verifies the center pixel returns to background while the expected shifted sample contains the triangle.
- [ ] Preserve existing focused tests: `SandboxEditorUi.TransformEditCommandMutatesLocalTransformAndMarksDirty`, `FrameGraphSystems.TransformSystem_RegistersAndExecutes`, and `RuntimeRenderExtraction.CreatesUpdatesAndClearsDirtyTransformSidecar`.

## Docs

- [ ] Update `src/runtime/README.md` to document the pre-render ECS transform flush if new helper/API names or frame-order comments change.
- [ ] Update relevant task/backlog bug indexes when promoting or retiring this task.
- [ ] No architecture ADR is expected unless the fix changes dependency boundaries or frame-order ownership beyond a runtime-owned flush.

## Acceptance criteria

- [ ] Editing `ReferenceTriangle` local position through the promoted Sandbox Editor path updates the entity's rendered model matrix without mutating mesh vertex positions.
- [ ] `Transform::IsDirtyTag` is cleared, `DirtyTags::DirtyTransform` is stamped then drained by extraction, and the render snapshot/model translation matches the authored local position for a root entity.
- [ ] The fix preserves layer boundaries: `ecs` remains free of runtime/graphics ownership, and `graphics` consumes snapshots/SSBO state only.
- [ ] CPU/null regression coverage fails on the stale-`WorldMatrix` bug and passes after the fix.
- [ ] Vulkan operational proof is either cited in this task's verification or explicitly deferred to `BUG-024B` using the maturity statement above.

## Verification

```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|RuntimeRenderExtraction|FrameGraphSystems|BUG024|TransformEdit' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

# If this task claims Operational Vulkan coverage:
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'RuntimeSandboxAcceptanceGpuSmoke.*Transform|RuntimeSandboxAcceptanceGpuSmoke.*ReferenceTriangle' -L 'gpu' -L 'vulkan' --timeout 120
```

## Forbidden changes

- Do not make `graphics` import ECS, runtime, UI, platform, or live asset services.
- Do not bypass the promoted `TransformHierarchy`/`RenderSync` contract by writing GPU instance buffers directly from UI code.
- Do not treat a manual test helper that writes `Transform::WorldMatrix` directly as proof that the UI path is fixed.
- Do not retire the bug at `Operational` maturity without an actually executed backend-labeled run.
