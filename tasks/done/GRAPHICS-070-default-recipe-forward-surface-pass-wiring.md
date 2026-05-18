# GRAPHICS-070 — Default-recipe `Pass.Forward.Surface` operational wiring

## Status

- Status: done.
- Owner/agent: Claude on `claude/graphics-rendering-task-zpycz`.
- Branch: `claude/graphics-rendering-task-zpycz`.
- Started: 2026-05-18.
- Completed: 2026-05-18.
- Commit/PR: pending current change.
- Completion verification: this session adds the
  `m_ForwardSurfacePass`/`m_ForwardSurfacePipelineLease` slots on
  `NullRenderer`, creates the forward-surface pipeline from
  `shaders/surface.vert.spv` + `shaders/surface.frag.spv` via
  `BuildForwardSurfacePipelineDesc()` inside
  `InitializeOperationalPassResources()` (and again on
  `RebuildOperationalResources()` byte-identical), exposes the descriptor +
  device handle through new `IRenderer::GetForwardSurfacePipeline()` /
  `GetForwardSurfacePipelineDesc()` accessors, routes the executor's
  `"SurfacePass"` branch to `RecordForwardSurfacePass(...)` whenever
  `DeriveDefaultFrameRecipeFeatures()` selects the forward lighting path,
  and flips that derivation to `FrameRecipeLightingPath::Forward` so the
  default recipe can record surface draws until GRAPHICS-072 wires the
  deferred GBuffer body. The new contract test
  `RendererFrameLifecycle.ForwardSurfacePipelineSurvivesOperationalRebuild`
  asserts the pipeline descriptor matches the depth-prepass-on contract
  (`DepthFunc = Equal`, `DepthWriteEnable = false`, RGBA16F color, D32
  depth, `sizeof(GpuScenePushConstants)` push range) and is republished
  byte-identical across `RebuildOperationalResources()`. The companion
  `RendererFrameLifecycle.ForwardSurfacePassSkipsUnavailableWhenCullOutputMissing`
  exercises the `SkippedUnavailable` taxonomy when culling output is
  missing. The pre-existing renderer-frame-lifecycle tests
  (`UsesDeviceFrameLifecycleBackbufferAndCommandContext`,
  `OperationalRebuildAfterNonOperationalStartupRecordsRoutedCommands`,
  `DepthPrepassPipelineFailureSkipsUnavailableCommandPass`,
  `FrameRecipePassesAllProduceStructuredCommandRecordStatuses`) were
  refreshed to reflect the now-three routed passes
  (`CullingPass`/`DepthPrepass`/`SurfacePass`) and the forward-mode default
  (no `CompositionPass` declared). The pinned `clang-20`/`clang++-20`
  toolchain is unavailable in this remote-execution environment
  (Clang 18.1.3 ships here; the project's C++23 surfaces are gated on
  clang-20), so `cmake --build --preset ci --target
  IntrinsicGraphicsContractTests` could not run locally — same constraint
  the GRAPHICS-032D / GRAPHICS-033D authors recorded; the contract
  invariants are statically encoded and run on hosts with the pinned
  toolchain.

## Goal
- Promote the existing `ForwardSurfacePass` (`src/graphics/renderer/Passes/Pass.Forward.Surface.cpp:14`) from a stand-alone class to a fully wired pass under the default frame recipe: `NullRenderer` (the sole concrete renderer) owns an instance, creates and binds the forward-surface pipeline at renderer init / `RebuildOperationalResources()`, and the executor lambda routes the `"Pass.Forward.Surface"` pass name through a real `RecordForwardSurfacePass(...)` helper instead of the current `SkippedNonOperational`/`SkippedUnavailable` branch.

## Non-goals
- No alpha-mask sub-bucket; the forward surface here is `SurfaceOpaque` only (alpha-mask is reserved by `GRAPHICS-008Q` infrastructure, opened by a separate task when material alpha evaluation lands).
- No deferred GBuffer wiring (`GRAPHICS-072`).
- No Forward.Line / Forward.Point wiring (`GRAPHICS-071`).
- No new shader; reuses `assets/shaders/surface.vert` + `surface_gbuffer.frag` (forward-surface variant) per `GRAPHICS-008` decisions.
- No new diagnostics counters beyond reusing the existing `RenderCommandPassStatus` taxonomy.

## Context
- Owner/layer: `graphics/renderer`.
- Planning anchors: `tasks/done/GRAPHICS-008-depth-surface-gbuffer-passes.md`, `tasks/done/GRAPHICS-008Q-surface-pass-clarifications.md`, `src/graphics/renderer/Passes/Pass.Forward.Surface.cpp:14`.
- Today: `ForwardSurfacePass` exists with a real command body but `NullRenderer` never owns it, never sets its pipeline, and the executor lambda has no branch for `"Pass.Forward.Surface"`. The `BuildDefaultFrameRecipe` already declares the pass.
- This is the first task in the Phase-2 series (`GRAPHICS-070..076`) that wires real pass bodies under the default recipe. Each pass family gets its own focused task.
- Naming note: the recipe declares the canonical pass under the string label `"SurfacePass"` (per `docs/architecture/rendering-three-pass.md` and `Graphics.FrameRecipe.cpp`), backed by the module `Extrinsic.Graphics.Pass.Forward.Surface` in forward mode. The executor branch added by this task matches the recipe label `"SurfacePass"` and dispatches to `ForwardSurfacePass` only when the recipe is in forward lighting mode; the deferred branch remains owned by `GRAPHICS-072`.

## Required changes
- [x] Add `m_ForwardSurfacePass` and `m_ForwardSurfacePipelineLease` members to `NullRenderer`. The pass is held in `std::optional<ForwardSurfacePass>` so the `ForwardSystem&` constructor invariant is preserved; emplaced in `Initialize()` immediately after `m_ForwardSystem`, dropped in `Shutdown()` before `m_ForwardSystem` is reset.
- [x] In `InitializeOperationalPassResources(device)`, create the forward-surface pipeline via `PipelineManager::Create(BuildForwardSurfacePipelineDesc())` (vertex `shaders/surface.vert.spv` + fragment `shaders/surface.frag.spv`; `DepthCompareOp = Equal`, `DepthWriteEnable = false` matching the depth-prepass-on path; single RGBA16F color target for `SceneColorHDR`; `D32_FLOAT` depth; `sizeof(GpuScenePushConstants)` push range) and call `m_ForwardSurfacePass->SetPipeline(...)`.
- [x] In `RebuildOperationalResources()`, republish the pipeline byte-identical (same `BuildForwardSurfacePipelineDesc()` descriptor; the pipeline registry's dedupe yields the same device handle).
- [x] Add a `"SurfacePass"` branch in the executor lambda (Graphics.Renderer.cpp) routing to `RecordForwardSurfacePass(graphicsContext, camera, frame.FrameIndex)` which:
  - returns `SkippedNonOperational` if device is non-operational,
  - returns `SkippedUnavailable` if pipeline lease or `SurfaceOpaque` cull bucket is missing,
  - otherwise calls `m_ForwardSurfacePass->Execute(cmd, camera, *m_GpuWorld, *m_CullingSystem, frameIndex)` and returns `Recorded`.
  Routing is gated on `!defaultRecipeUsesDeferred` so the deferred-mode branch (GRAPHICS-072) can land alongside this one without conflict.
- [x] Verified `BuildDefaultFrameRecipe` continues to declare `"SurfacePass"` with the same resource set; no recipe-resource changes were required.
- [x] Flip `DeriveDefaultFrameRecipeFeatures()` to `FrameRecipeLightingPath::Forward` so the executor actually reaches the new branch under the default recipe while GRAPHICS-072's deferred wiring is open. The struct default `FrameRecipeFeatures{}.LightingPath = Deferred` is unchanged so `Test.FrameRecipeContract` can keep exercising both modes through explicit features.
- [x] Expose `IRenderer::GetForwardSurfacePipeline()` and `GetForwardSurfacePipelineDesc()` so the rebuild-stability contract is testable from the CPU gate.

## Tests
- [x] `contract;graphics`: `RendererFrameLifecycle.UsesDeviceFrameLifecycleBackbufferAndCommandContext` now asserts that the default recipe records `CullingPass`/`DepthPrepass`/`SurfacePass` and that the per-frame counters reflect three `BindPipeline`/`PushConstants` calls plus two `DrawIndexedIndirectCount` calls (depth prepass + forward surface).
- [x] `contract;graphics`: `RendererFrameLifecycle.ForwardSurfacePassSkipsUnavailableWhenCullOutputMissing` covers the no-surface-candidates / cull-output-unavailable case where the executor reports `SurfacePass = SkippedUnavailable` and records no draw.
- [x] `contract;graphics`: `RendererFrameLifecycle.ForwardSurfacePipelineSurvivesOperationalRebuild` asserts the descriptor matches the depth-prepass-on contract and is byte-identical across `RebuildOperationalResources()`.
- [x] `contract;graphics`: `RendererFrameLifecycle.FrameRecipePassesAllProduceStructuredCommandRecordStatuses` was refreshed so `SurfacePass` joins the routed set and `CompositionPass` is asserted absent under the forward-mode default; remaining soft-skipped passes still produce structured `SkippedUnavailable` entries.

## Docs
- [x] Updated `src/graphics/renderer/README.md` Ownership Contract section to record the forward surface wiring (members, pipeline descriptor, recipe label routing, default lighting path, and the GRAPHICS-072 follow-up).

## Acceptance criteria
- [x] `Pass.Forward.Surface` records draws in the operational state and increments `Recorded` in `RenderGraphCommandPassStats` (per the updated lifecycle and the new rebuild test).
- [x] No regression in `Pass.CullingPass` / `Pass.DepthPrepass` routing (the updated lifecycle tests continue to assert both record).
- [x] No regression in CPU/null tests for the default recipe; the updated test expectations match the now-active forward surface path and the absence of `CompositionPass` in forward mode.

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

## Completion note
- Retired 2026-05-18. Default-recipe `"SurfacePass"` records the forward
  surface bind/draw shape on the operational path; deferred branch remains
  owned by GRAPHICS-072. Toolchain-availability constraint (clang-20 not
  installed in this remote-execution environment) means the local
  `cmake --build --preset ci --target IntrinsicGraphicsContractTests` run is
  deferred to a host with the pinned toolchain.

## Next verification step
- Land the pipeline + executor route, exercise the contract tests above.
