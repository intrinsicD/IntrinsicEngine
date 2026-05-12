# GRAPHICS-070 — Default-recipe `Pass.Forward.Surface` operational wiring

## Goal
- Promote the existing `ForwardSurfacePass` (`src/graphics/renderer/Passes/Pass.Forward.Surface.cpp:14`) from a stand-alone class to a fully wired pass under the default frame recipe: `NullRenderer` (the sole concrete renderer) owns an instance, creates and binds the forward-surface pipeline at renderer init / `RebuildOperationalResources()`, and the executor lambda routes the `"Pass.Forward.Surface"` pass name through a real `RecordForwardSurfacePass(...)` helper instead of the current `SkippedNonOperational`/`SkippedUnavailable` branch.

## Non-goals
- No alpha-mask sub-bucket; the forward surface here is `SurfaceOpaque` only (alpha-mask is reserved by `GRAPHICS-008Q` infrastructure, opened by a separate task when material alpha evaluation lands).
- No deferred GBuffer wiring (`GRAPHICS-072`).
- No Forward.Line / Forward.Point wiring (`GRAPHICS-071`).
- No new shader; reuses `assets/shaders/surface.vert` + `surface_gbuffer.frag` (forward-surface variant) per `GRAPHICS-008` decisions.
- No new diagnostics counters beyond reusing the existing `RenderCommandPassStatus` taxonomy.

## Context
- Status: not started.
- Owner/layer: `graphics/renderer`.
- Planning anchors: `tasks/done/GRAPHICS-008-depth-surface-gbuffer-passes.md`, `tasks/done/GRAPHICS-008Q-surface-pass-clarifications.md`, `src/graphics/renderer/Passes/Pass.Forward.Surface.cpp:14`.
- Today: `ForwardSurfacePass` exists with a real command body but `NullRenderer` never owns it, never sets its pipeline, and the executor lambda has no branch for `"Pass.Forward.Surface"`. The `BuildDefaultFrameRecipe` already declares the pass.
- This is the first task in the Phase-2 series (`GRAPHICS-070..076`) that wires real pass bodies under the default recipe. Each pass family gets its own focused task.

## Required changes
- [ ] Add `m_ForwardSurfacePass` and `m_ForwardSurfacePipelineLease` members to `NullRenderer`.
- [ ] In `InitializeOperationalPassResources(device)`, create the forward-surface pipeline via `PipelineManager::Create(PipelineDesc{ VertexShaderPath = "assets/shaders/surface.vert", FragmentShaderPath = "assets/shaders/surface_gbuffer.frag" or its forward variant, …pipeline state per GRAPHICS-008 decisions… })` and call `m_ForwardSurfacePass.SetPipeline(...)`.
- [ ] In `RebuildOperationalResources()`, republish the pipeline byte-identical (same handle, same descriptor layout).
- [ ] Add a `"Pass.Forward.Surface"` branch in the executor lambda (`Graphics.Renderer.cpp:736`) routing to `RecordForwardSurfacePass(graphicsContext, camera, frame.FrameIndex)` which:
  - returns `SkippedNonOperational` if device is non-operational,
  - returns `SkippedUnavailable` if pipeline lease or `SurfaceOpaque` cull bucket is missing,
  - otherwise calls `m_ForwardSurfacePass.Execute(cmd, camera, *m_GpuWorld, *m_CullingSystem, frameIndex)` and returns `Recorded`.
- [ ] Verify `BuildDefaultFrameRecipe` continues to declare the pass with the same resource set (no recipe change required if already declared).

## Tests
- [ ] `contract;graphics` test: with one extracted procedural surface and the default recipe, the executor records `Pass.Forward.Surface` with the bind/draw shape from `Pass.Forward.Surface.cpp:14`.
- [ ] `contract;graphics` test: with no surface candidates, the pass status is `SkippedUnavailable` (cull bucket empty) and no draw is recorded.
- [ ] `contract;graphics` test: pipeline lease survives `RebuildGpuResources()` with identical RHI handle.
- [ ] `contract;graphics` test: in the operational state, `Pass.Forward.Surface` accumulates `Recorded` in `RenderGraphCommandPassStats`.

## Docs
- [ ] Update `src/graphics/renderer/README.md` to record that the default-recipe forward-surface pass is no longer soft-skipped.

## Acceptance criteria
- [ ] `Pass.Forward.Surface` records draws in the operational state and increments `Recorded` in `RenderGraphCommandPassStats`.
- [ ] No regression in `Pass.CullingPass` / `Pass.DepthPrepass` routing.
- [ ] No regression in CPU/null tests for the default recipe.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding alpha-mask sub-bucket logic.
- Adding deferred GBuffer routing.
- Mutating `BuildDefaultFrameRecipe` resource declarations.
- Mixing mechanical file moves with semantic refactors.

## Next verification step
- Land the pipeline + executor route, exercise the contract tests above.
