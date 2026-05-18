# GRAPHICS-073 — Default-recipe `Pass.Shadows` wiring + shadow atlas allocation

## Status

- State: in-progress (Slice A landed; Slice B unstarted).
- Owner/agent: local agent workflow.
- Branch: `claude/setup-agent-workflow-Si8we`.
- Activated: 2026-05-18 after GRAPHICS-071 retirement made it the next unblocked Theme A leaf.
- Next verification step: pick up Slice B (`ShadowSystem`-owned atlas + sampler allocation, `FrameRecipeShadowSizing` import seam, atlas-survives-rebuild + barrier-transition tests).

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

Slice B (follow-up):

- [ ] Have `ShadowSystem::Initialize(...)` allocate the shadow-atlas texture (`D32_FLOAT`, sized per `ShadowParams::AtlasResolution`), create a `sampler2DShadow`-bindable sampler at `set 0, binding 1` per the `GRAPHICS-009Q` decision, and free both at `Shutdown()`.
- [ ] Add `FrameRecipeShadowSizing` + an optional `FrameRecipeImports::ShadowAtlas`; teach `BuildDefaultFrameRecipe` to import the `ShadowSystem`-owned atlas (replacing the transient `graph.CreateTexture("ShadowAtlas", ...)`).
- [ ] Confirm the deferred lighting pass (per `GRAPHICS-072`) reads the shadow atlas via `set 0, binding 1`.

## Tests

Slice A (this slice):

- [x] `contract;graphics` test: shadow pipeline lease + descriptor survive `RebuildOperationalResources()` (depth-only, `D32_FLOAT` depth target, no color targets, push-constant size = `sizeof(GpuScenePushConstants)`).
- [x] Existing `Test.LightingShadowContracts.cpp::ShadowPassSkipsDisabledShadowsAndUsesShadowBucketWhenEnabled` already exercises the bind/draw shape against the `ShadowOpaque` cull bucket and stays green; this slice preserves that contract.

Slice B (follow-up):

- [ ] `contract;graphics` test: with one shadow-casting renderable + one light, the shadow atlas is allocated, `Pass.Shadows` records, and the executor reports `Recorded`.
- [ ] `contract;graphics` test: barrier packets transition the shadow atlas `DepthAttachment` → `ShaderRead` before deferred lighting.
- [ ] `contract;graphics` test: missing-caster diagnostic from `ShadowSystem` is surfaced even when the cull bucket is empty (no false-positive `Recorded` status).
- [ ] `contract;graphics` test: shadow atlas survives `RebuildGpuResources()` with byte-identical handle.

## Docs
- [x] Update `src/graphics/renderer/README.md` to record `Pass.Shadows` pipeline + executor branch as operationally wired (Slice A).
- [ ] Update `docs/architecture/rendering-three-pass.md` if shadow ordering shifts (Slice B).

## Acceptance criteria

Slice A:
- [x] `Pass.Shadows` records draws when shadow casters exist; `SkippedUnavailable` when pipeline/culling prerequisites are missing; `SkippedNonOperational` when the device is non-operational.
- [x] Shadow pipeline descriptor is byte-identical across the initial init and `RebuildOperationalResources()`.
- [x] No regression in CPU/null tests.

Slice B:
- [ ] `ShadowSystem`-owned atlas + sampler allocated at renderer init, freed on `Shutdown()`, and imported via the typed sizing seam into the default recipe.
- [ ] Deferred lighting samples the atlas correctly (test asserts the descriptor binding).

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
- Slice A landed: depth-only shadow pipeline created, executor `"ShadowPass"` branch routes, byte-identical rebuild test passes. Slice B pickup is `ShadowSystem`-owned atlas + sampler allocation and the typed import seam.
