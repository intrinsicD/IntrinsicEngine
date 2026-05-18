# GRAPHICS-073 — Default-recipe `Pass.Shadows` wiring + shadow atlas allocation

## Status

- State: done. Slice A + Slice B landed; Slice C scope transferred into GRAPHICS-072 (the deferred-lighting `set 0, binding 1` shadow-sampler binding and the cross-pass `DepthAttachment → ShaderRead` barrier-transition test can only be exercised once `DeferredLightingPass` is recording in the operational executor, which is GRAPHICS-072's deliverable).
- Owner/agent: local agent workflow.
- Branch: Slice A — `claude/setup-agent-workflow-Si8we`; Slice B — `claude/setup-agentic-workflow-rlgPZ`.
- PR: Slice A — PR #881; Slice B — PR #882.
- Commit: Slice A — `79fee87`; Slice B — `b746db5`, `2d9508c`.
- Activated: 2026-05-18 after GRAPHICS-071 retirement made it the next unblocked Theme A leaf.
- Completed: 2026-05-18. Retirement landed via `claude/setup-agentic-workflow-XknwW`.
- Completion verification: see "Completion note" below.

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

Slice C (transferred to GRAPHICS-072): see "Completion note" below.

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

Slice C (transferred to GRAPHICS-072): see "Completion note" below.

## Docs
- [x] Update `src/graphics/renderer/README.md` to record `Pass.Shadows` pipeline + executor branch as operationally wired (Slice A).
- [x] Update `src/graphics/renderer/README.md` to record `ShadowSystem`-owned atlas + sampler + recipe import seam + `MissingCasterCount` diagnostic (Slice B).
- Slice C-owned doc updates (rendering-three-pass shadow ordering, if any) transferred to GRAPHICS-072 — see "Completion note" below.

## Acceptance criteria

Slice A:
- [x] `Pass.Shadows` records draws when shadow casters exist; `SkippedUnavailable` when pipeline/culling prerequisites are missing; `SkippedNonOperational` when the device is non-operational.
- [x] Shadow pipeline descriptor is byte-identical across the initial init and `RebuildOperationalResources()`.
- [x] No regression in CPU/null tests.

Slice B:
- [x] `ShadowSystem`-owned atlas + sampler allocated lazily when shadows enabled, freed on `Shutdown()`, and imported via the typed sizing seam into the default recipe.
- [x] Atlas + sampler survive `RebuildOperationalResources()` byte-identically.
- [x] `ShadowDiagnostics::MissingCasterCount` increments only when shadows are enabled and the cull bucket is empty.

Slice C (transferred to GRAPHICS-072): see "Completion note" below.

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

## Completion note

- Slice A (PR #881, commit `79fee87`): `m_ShadowPass` + `m_ShadowPipelineLease` owned by `NullRenderer`; depth-only shadow pipeline built from `BuildShadowPipelineDesc()` and republished byte-identically across `RebuildOperationalResources()`; `"ShadowPass"` executor branch routes through `RecordShadowPass(...)` with the `SkippedNonOperational` / `SkippedUnavailable` / `Recorded` taxonomy; `IRenderer::GetShadowPipeline()` / `GetShadowPipelineDesc()` exposed for byte-identical-rebuild assertions.
- Slice B (PR #882, commits `b746db5`, `2d9508c`): `ShadowSystem::Initialize(device, textureMgr, samplerMgr)` lazily allocates the `D32_FLOAT` atlas (`AtlasResolution * CascadeCount × AtlasResolution`) and a `sampler2DShadow`-bindable sampler on the first shadows-enabled `SetParams(...)`; `FrameRecipeShadowSizing` + optional `FrameRecipeImports::ShadowAtlas` let `BuildDefaultFrameRecipe` import the `ShadowSystem`-owned atlas with `InitialState = Undefined` cross-frame; `ShadowDiagnostics::MissingCasterCount` surfaces empty-cull-bucket frames; six new `contract;graphics` tests cover atlas/sampler lazy allocation, no-allocation while disabled, shutdown release, missing-caster diagnostic, imported vs transient ShadowAtlas paths, typed shadow-sizing acceptance, and atlas survival across `RebuildOperationalResources()`. Full default CPU gate green (1987/1987 tests).
- Slice C (transferred to GRAPHICS-072): the deferred-lighting `set 0, binding 1` shadow-sampler binding and the cross-pass `DepthAttachment → ShaderRead` barrier-transition + `Recorded` integration tests can only be exercised once `DeferredLightingPass` is recording in the operational executor. Those requirements now live in GRAPHICS-072 (Required changes, Tests, Acceptance criteria, Docs) so they are picked up alongside the deferred GBuffer + lighting wiring rather than re-promoted as a stranded sliver.
