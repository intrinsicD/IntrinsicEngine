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
- Trace result (2026-06-09): post-fixed-step editor/gizmo mutations were not flushed before render extraction, so extraction read the old `WorldMatrix` (hypothesis 1 of the four ranked hypotheses; hypotheses 3/4 — culling/shader-side staleness — were ruled out by the CPU trace because the stale value exists before GPU work, and the promoted surface/line/point/selection shaders all read `GpuInstanceDynamic::Model`).
- Fix landed (2026-06-10): `Extrinsic.Runtime.EcsSystemBundle` now exports `FlushPreRenderTransformState(ECS::Scene::Registry&)` (returning `PreRenderTransformFlushStats`), which runs `TransformHierarchy::OnUpdate` → `BoundsPropagation::OnUpdate` → `RenderSync::OnUpdate` directly, outside the fixed-step FrameGraph. `Engine::RunFrame()` invokes it after the variable tick, ImGui editor hook, and `DriveGizmoInteractionForFrame`, and before `TransformGizmoPacketBuilder::Build` and `RenderExtractionCache::ExtractAndSubmit`, so post-fixed-step UI/gizmo edits reach the rendered model matrix and the gizmo packets in the same frame.
- Completed: 2026-06-10.
- PR/commit: pending local commit.

## Maturity

- Target: `CPUContracted` for the deterministic UI/edit-to-render-snapshot contract; `Operational` on Vulkan-capable hosts once the opt-in pixel-shift smoke passes.
- This task landed CPU/null-only; `Operational` owned by `BUG-024B` (opt-in `gpu;vulkan` pixel-shift smoke through the UI/editor command path).

## Required changes

- [x] Add a failing CPU/null repro that drives the real editor-command mutation after the fixed-step phase and asserts the render-facing model/world state changes before extraction/prepare (`RuntimeSandboxAcceptance.InspectorTransformEditFlushedToRenderStateSameFrame`; verified failing with the flush disabled).
- [x] Add or extend a runtime-owned pre-render transform flush so post-fixed-step mutations from `OnVariableTick()`, Sandbox Editor UI, and `GizmoInteraction` run through `TransformHierarchy`, `BoundsPropagation`, and `RenderSync` before `RenderExtractionCache::ExtractAndSubmit()` observes the scene (`FlushPreRenderTransformState` in `Extrinsic.Runtime.EcsSystemBundle`).
- [x] Ensure the flush stays in `runtime` and uses ECS system seams without adding live ECS reads to `graphics`.
- [x] Verify `Transform::WorldMatrix`, `DirtyTags::DirtyTransform`, `RenderWorld::Renderables[*].Model`, and extraction stats all agree after an inspector transform edit (`RuntimeRenderExtraction.UiTransformEditModelReachesRenderWorldAfterPreRenderFlush`).
- [x] If CPU snapshot state updates but Vulkan pixels do not move, inspect default-recipe pass diagnostics, culling buckets, and shader/pipeline pairing before changing shaders — CPU trace ruled out hypotheses 3/4; the Vulkan pixel-shift confirmation is owned by `BUG-024B`.
- [x] Keep gizmo packets aligned with the flushed world matrix by placing the flush before transform-gizmo packet build and render extraction (single flush after `DriveGizmoInteractionForFrame`, before `m_GizmoPacketBuilder.Build`).

## Tests

- [x] Add a `contract;runtime` or `integration;runtime` CPU test that exercises `ApplySandboxEditorTransformEdit(...)` after the normal fixed-step bundle and before render extraction (`RuntimeSandboxAcceptance.InspectorTransformEditFlushedToRenderStateSameFrame` drives the live `EditorCommandHistory` path; `RuntimeEcsSystemBundle.PreRenderFlushRefreshesPostFixedStepTransformEdit` and `RuntimeEcsSystemBundle.PreRenderFlushIsNoOpOnCleanScene` pin the flush helper contract).
- [x] Extend `RuntimeRenderExtraction` coverage to assert the submitted/render-world model translation, not only `SubmittedTransformCount` (`RuntimeRenderExtraction.UiTransformEditModelReachesRenderWorldAfterPreRenderFlush`).
- [x] The opt-in `gpu;vulkan;runtime;regression` pixel-shift smoke in `Test.RuntimeSandboxAcceptanceGpuSmoke.cpp` is owned by the follow-up `BUG-024B` per the Maturity statement.
- [x] Preserve existing focused tests: `SandboxEditorUi.TransformEditCommandMutatesLocalTransformAndMarksDirty`, `FrameGraphSystems.TransformSystem_RegistersAndExecutes`, and `RuntimeRenderExtraction.CreatesUpdatesAndClearsDirtyTransformSidecar` (all pass in the retirement gate).

## Docs

- [x] Update `src/runtime/README.md` to document the pre-render ECS transform flush (module table row for `Extrinsic.Runtime.EcsSystemBundle` and frame-loop phase 4).
- [x] Update relevant task/backlog bug indexes when retiring this task.
- [x] No architecture ADR needed; the flush is runtime-owned composition and changes no dependency boundary.

## Acceptance criteria

- [x] Editing `ReferenceTriangle` local position through the promoted Sandbox Editor path updates the entity's rendered model matrix without mutating mesh vertex positions.
- [x] `Transform::IsDirtyTag` is cleared, `DirtyTags::DirtyTransform` is stamped then drained by extraction, and the render snapshot/model translation matches the authored local position for a root entity.
- [x] The fix preserves layer boundaries: `ecs` remains free of runtime/graphics ownership, and `graphics` consumes snapshots/SSBO state only.
- [x] CPU/null regression coverage fails on the stale-`WorldMatrix` bug and passes after the fix (verified by disabling the flush: `RuntimeSandboxAcceptance.InspectorTransformEditFlushedToRenderStateSameFrame` failed, then passed with the flush restored).
- [x] Vulkan operational proof is explicitly deferred to `BUG-024B` using the maturity statement above.

## Verification

Commands actually run for retirement (2026-06-10):

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|RuntimeRenderExtraction|FrameGraphSystems|RuntimeEcsSystemBundle|RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60   # 74/74 passed
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60   # 2882/2882 passed
# Bug-reproduction check: flush disabled -> InspectorTransformEditFlushedToRenderStateSameFrame FAILED; flush restored -> passed.
```

The opt-in Vulkan run is owned by `BUG-024B`:

```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'RuntimeSandboxAcceptanceGpuSmoke.*Transform|RuntimeSandboxAcceptanceGpuSmoke.*ReferenceTriangle' -L 'gpu' -L 'vulkan' --timeout 120
```

## Forbidden changes

- Do not make `graphics` import ECS, runtime, UI, platform, or live asset services.
- Do not bypass the promoted `TransformHierarchy`/`RenderSync` contract by writing GPU instance buffers directly from UI code.
- Do not treat a manual test helper that writes `Transform::WorldMatrix` directly as proof that the UI path is fixed.
- Do not retire the bug at `Operational` maturity without an actually executed backend-labeled run.

