# GRAPHICS-073 — Default-recipe `Pass.Shadows` wiring + shadow atlas allocation

## Status

- State: in-progress (Slice A landed via `claude/setup-agent-workflow-Si8we`/PR #881; Slice B in progress on `claude/setup-agentic-workflow-rlgPZ`).
- Owner/agent: local agent workflow.
- Branch: `claude/setup-agentic-workflow-rlgPZ` (Slice B); Slice A landed via `claude/setup-agent-workflow-Si8we`.
- Activated: 2026-05-18 after GRAPHICS-071 retirement made it the next unblocked Theme A leaf.
- Next verification step: land Slice B (`ShadowSystem`-owned atlas + sampler allocation, `FrameRecipeShadowSizing` import seam, atlas-survives-rebuild + missing-caster-diagnostic tests). The deferred-lighting `set 0, binding 1` descriptor wiring + the cross-pass barrier-transition test stay gated on GRAPHICS-072 and are recorded as a Slice C follow-up.

## Goal
- Wire `Pass.Shadows` (`src/graphics/renderer/Passes/Pass.Shadows.cpp/.cppm`) into the renderer executor under the default recipe: shadow-atlas texture/sampler allocated by `ShadowSystem` at renderer init, the depth-only shadow pipeline created, instance owned by `NullRenderer`, and the executor route consumes the `ShadowOpaque` cull bucket per `GRAPHICS-009Q`.

## Non-goals
- No virtual shadow maps (`GRAPHICS-047` planning).
- No alpha-mask shadow casters (gates on the alpha-mask sub-bucket reserved by `GRAPHICS-008Q`).
- No clustered binning (`GRAPHICS-039` planning).

## Context
- Owner/layer: `graphics/renderer`.
- Planning anchors: `tasks/done/GRAPHICS-009-deferred-lighting-and-shadows.md`, `tasks/done/GRAPHICS-009Q-lighting-shadow-clarifications.md`.
- Today: `Pass.Shadows.cpp` exists as a shell; `ShadowSystem` exists with `ApplyTo(camera)` integration but no atlas resource; the executor lambda has no `"ShadowPass"` branch.
- Cascade view-projection matrices and missing-caster diagnostics are runtime/shadow-extraction owned per `GRAPHICS-009Q`; this task consumes those snapshots, it does not own them.
- The frame recipe (`DescribeDefaultFrameRecipe` / `BuildDefaultFrameRecipe`) declares the pass under the executor label `"ShadowPass"` and writes the transient depth target `"ShadowAtlas"` (viewport-sized, `D32_FLOAT`). `GRAPHICS-009Q` decided to keep that transient sizing until a `FrameRecipeShadowSizing` typed seam plumbs the `ShadowSystem`-owned atlas extent through `BuildDefaultFrameRecipe`; that transition is gated to Slice B below rather than smuggled into Slice A.

## Slice plan

- **Slice A (this slice; pipeline + executor routing).** Add an `m_ShadowPass` instance + `m_ShadowPipelineLease` to `NullRenderer`. Move `ShadowSystem::Initialize()` and the `m_ShadowPass.emplace(*m_ShadowSystem)` step ahead of the operational publisher so the publisher can call `SetPipeline(...)` on the live pass before the first frame (same invariant as GRAPHICS-070/071). Create the depth-only shadow pipeline in `InitializeOperationalPassResources()` from `BuildShadowPipelineDesc()` (vertex `shaders/depth_prepass.vert.spv`, no fragment shader, `D32_FLOAT` depth-only attachment, `Topology::TriangleList`, depth write enabled, push-constant size = `sizeof(GpuScenePushConstants)`) and republish it byte-identical from `RebuildOperationalResources()`. Add a `"ShadowPass"` branch in the executor lambda routing through a new `RecordShadowPass(...)` helper that returns the standard `SkippedNonOperational` / `SkippedUnavailable` / `Recorded` taxonomy. Expose `GetShadowPipeline()` / `GetShadowPipelineDesc()` on `IRenderer` so contract tests can assert the byte-identical descriptor across rebuilds. Continue to rely on the recipe-transient `ShadowAtlas` for graph-side barrier emission; do not reshape `FrameRecipeImports`.

- **Slice B (follow-up; atlas + sampler allocation + recipe import seam).** Have `ShadowSystem::Initialize(device, textureMgr, samplerMgr)` allocate the shadow-atlas texture (`D32_FLOAT`, sized per `ShadowAtlasDesc` derived from `ShadowParams::AtlasResolution`) and a `sampler2DShadow`-bindable sampler at `set 0, binding 1` per `GRAPHICS-009Q`. Add `FrameRecipeShadowSizing` and an optional `FrameRecipeImports::ShadowAtlas` so `BuildDefaultFrameRecipe` imports the `ShadowSystem`-owned atlas (replacing the transient `graph.CreateTexture("ShadowAtlas", ...)`). Add the deferred-lighting `set 0, binding 1` descriptor binding wiring once GRAPHICS-072 lands. Cover Slice B with the atlas-survives-rebuild + barrier-transition tests + the missing-caster diagnostic surfacing test described below.

## Required changes

Slice A (this slice):

- [x] Add `m_ShadowPass` and `m_ShadowPipelineLease` members to `NullRenderer`.
- [x] Reorder `Initialize()` so `m_ShadowSystem` is emplaced + `Initialize()`-d and `m_ShadowPass.emplace(*m_ShadowSystem)` runs *before* `InitializeOperationalPassResources(device)` — same publisher-before-first-frame invariant as GRAPHICS-070/071.
- [x] Create the depth-only shadow pipeline via `PipelineManager::Create(...)` from `BuildShadowPipelineDesc()` (vertex `shaders/depth_prepass.vert.spv`, no fragment, depth-only `D32_FLOAT`). Reset + republish byte-identical from `RebuildOperationalResources()`.
- [x] Add a `"ShadowPass"` branch in the executor lambda routing through `RecordShadowPass(...)` consuming the `ShadowOpaque` cull bucket. The helper returns `SkippedNonOperational` when the device is non-operational, `SkippedUnavailable` when culling/pipeline/world prerequisites are missing, otherwise `Recorded` after `ShadowPass::Execute(...)`.
- [x] Expose `GetShadowPipeline()` / `GetShadowPipelineDesc()` on `IRenderer` for byte-identical-rebuild assertions.
- [x] Drop `m_ShadowPass` before `m_ShadowSystem` in `Shutdown()`; reset `m_ShadowPipelineLease` alongside the other forward pipeline leases.

Slice B (this slice):

- [x] Have `ShadowSystem::Initialize(device, textureMgr, samplerMgr)` lazily allocate the shadow-atlas texture (`D32_FLOAT`, sized per `ShadowParams::AtlasResolution * CascadeCount`-by-`AtlasResolution`) and create a `sampler2DShadow`-bindable sampler (Linear/ClampToBorder/OpaqueWhite, CompareEnable=true, Compare=Less) on the first `SetParams(...)` call that enables shadows, and free both at `Shutdown()`.
- [x] Add `FrameRecipeShadowSizing` + an optional `FrameRecipeImports::ShadowAtlas`; teach `BuildDefaultFrameRecipe` to import the `ShadowSystem`-owned atlas (replacing the transient `graph.CreateTexture("ShadowAtlas", ...)` when the import is valid; the typed sizing seam covers the headless transient fallback).
- [x] Surface a `ShadowDiagnostics::MissingCasterCount` counter from `Pass.Shadows::Execute` when shadows are enabled but the `ShadowOpaque` cull bucket is empty, so operators can distinguish "no casters this frame" from "atlas wiring broken".

Slice C (follow-up; gated by GRAPHICS-072):

- [ ] Confirm the deferred lighting pass (per `GRAPHICS-072`) reads the shadow atlas via `set 0, binding 1`.
- [ ] Add the `contract;graphics` test that compiled barriers transition the shadow atlas `DepthAttachment` → `ShaderRead` before deferred lighting once `DeferredLightingPass` is recording in the operational executor.

## Tests

Slice A (this slice):

- [x] `contract;graphics` test: shadow pipeline lease + descriptor survive `RebuildOperationalResources()` (depth-only, `D32_FLOAT` depth target, no color targets, push-constant size = `sizeof(GpuScenePushConstants)`).
- [x] Existing `Test.LightingShadowContracts.cpp::ShadowPassSkipsDisabledShadowsAndUsesShadowBucketWhenEnabled` already exercises the bind/draw shape against the `ShadowOpaque` cull bucket and stays green; this slice preserves that contract.

Slice B (this slice):

- [x] `contract;graphics` test (`Test.LightingShadowContracts.cpp::ShadowSystemAllocatesAtlasAndSamplerWhenShadowsEnabled`): `ShadowSystem` allocates atlas + sampler when shadows are enabled, with the resolved `ShadowAtlasDesc` matching `AtlasResolution * CascadeCount`-by-`AtlasResolution` and no realloc on subsequent `SetParams(...)`.
- [x] `contract;graphics` test (`Test.LightingShadowContracts.cpp::ShadowSystemDoesNotAllocateAtlasWhileDisabled`): no atlas + no device texture/sampler creation while shadows stay disabled.
- [x] `contract;graphics` test (`Test.LightingShadowContracts.cpp::ShadowSystemReleasesAtlasAndSamplerOnShutdown`): atlas/sampler released on `Shutdown()`, device `DestroyTexture` invoked.
- [x] `contract;graphics` test (`Test.LightingShadowContracts.cpp::ShadowPassRecordsMissingCasterDiagnosticWhenBucketEmpty`): missing-caster diagnostic surfaces when shadows are enabled and the cull bucket is empty; no events recorded; disabling shadows preserves the counter (no false increments).
- [x] `contract;graphics` test (`Test.FrameRecipeContract.cpp::ShadowAtlasUsesImportedHandleWhenProvided`): with `imports.ShadowAtlas` valid, the compiled graph marks ShadowAtlas imported and binds the supplied handle; recipe-aware validation reports no errors/warnings.
- [x] `contract;graphics` test (`Test.FrameRecipeContract.cpp::ShadowAtlasFallsBackToTransientWhenImportInvalid`): with `imports.ShadowAtlas` invalid, the compiled graph treats ShadowAtlas as non-imported.
- [x] `contract;graphics` test (`Test.FrameRecipeContract.cpp::ShadowAtlasTransientPathAcceptsTypedShadowSizing`): the transient path accepts the new `FrameRecipeShadowSizing` parameter without build failure.
- [x] `contract;graphics` test (`Test.RendererFrameLifecycle.cpp::ShadowAtlasSurvivesOperationalRebuild`): with shadows enabled via the renderer's exposed `GetShadowSystem()`, the atlas + sampler handles stay byte-identical across `RebuildOperationalResources()`.

Slice C (follow-up; gated by GRAPHICS-072):

- [ ] `contract;graphics` test: with one shadow-casting renderable + one light wired end-to-end through extraction, the executor reports `Recorded` for `ShadowPass` (requires extraction integration).
- [ ] `contract;graphics` test: barrier packets transition the shadow atlas `DepthAttachment` → `ShaderRead` before deferred lighting.

## Docs
- [x] Update `src/graphics/renderer/README.md` to record `Pass.Shadows` pipeline + executor branch as operationally wired (Slice A).
- [x] Update `src/graphics/renderer/README.md` to record `ShadowSystem`-owned atlas + sampler + recipe import seam + `MissingCasterCount` diagnostic (Slice B).
- [ ] Update `docs/architecture/rendering-three-pass.md` if shadow ordering shifts (Slice C — gated on GRAPHICS-072 wiring).

## Acceptance criteria

Slice A:
- [x] `Pass.Shadows` records draws when shadow casters exist; `SkippedUnavailable` when pipeline/culling prerequisites are missing; `SkippedNonOperational` when the device is non-operational.
- [x] Shadow pipeline descriptor is byte-identical across the initial init and `RebuildOperationalResources()`.
- [x] No regression in CPU/null tests.

Slice B:
- [x] `ShadowSystem`-owned atlas + sampler allocated lazily when shadows enabled, freed on `Shutdown()`, and imported via the typed sizing seam into the default recipe.
- [x] Atlas + sampler survive `RebuildOperationalResources()` byte-identically.
- [x] `ShadowDiagnostics::MissingCasterCount` increments only when shadows are enabled and the cull bucket is empty.

Slice C (gated on GRAPHICS-072):
- [ ] Deferred lighting samples the atlas correctly (test asserts the descriptor binding at `set 0, binding 1`).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Implementing virtual shadow maps.
- Adding alpha-mask shadow caster filtering.
- Owning cascade matrices in graphics (runtime extraction owns those).
- Smuggling the typed `FrameRecipeShadowSizing` transition into Slice A (it is the Slice B deliverable per `GRAPHICS-009Q`).

## Next verification step
- Slice B landed: `ShadowSystem`-owned atlas + sampler allocated lazily on `SetParams(...)` enable; `FrameRecipeImports::ShadowAtlas` + `FrameRecipeShadowSizing` typed seam wired into `BuildDefaultFrameRecipe`; `ShadowDiagnostics::MissingCasterCount` surfaces empty-cull-bucket frames; 6 new `contract;graphics` tests pass alongside the Slice A regression test. Full default CPU gate green (1987/1987 tests).
- Slice C pickup is gated on GRAPHICS-072 (deferred GBuffer + lighting wiring) — once `DeferredLightingPass` records in the operational executor, wire its `set 0, binding 1` shadow-sampler binding from `ShadowSystem::GetAtlasBindlessIndex()` / `GetAtlasSampler()` and land the deferred-lighting-bound barrier-transition + `Recorded` integration tests.
