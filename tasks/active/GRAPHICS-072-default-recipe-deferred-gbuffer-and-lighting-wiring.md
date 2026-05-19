# GRAPHICS-072 — Default-recipe deferred GBuffer + lighting pass wiring

## Goal
- Wire `Pass.Deferred.GBuffers` and `Pass.Deferred.Lighting` (existing `.cpp/.cppm` files under `src/graphics/renderer/Passes/`) into the renderer executor under the default recipe: pipelines for the gbuffer surface variant and the deferred-lighting fullscreen kernel created at renderer init / `RebuildOperationalResources()`, instances owned by `NullRenderer`, executor routes through new `RecordGBufferPass(...)` and `RecordDeferredLightingPass(...)` helpers, and the `SceneNormal`/`Albedo`/`Material0`/`SceneDepth` framegraph attachments are produced and consumed correctly.

## Non-goals
- No clustered light binning (`GRAPHICS-039` planning, future implementation).
- No virtual shadow maps (`GRAPHICS-047` planning).
- No PBR feature completeness beyond the existing material model (`GRAPHICS-042` planning).
- No transparent / special-forward sub-buckets (`GRAPHICS-025` planning).
- Originally: "No new shaders; reuses `surface_gbuffer.frag`,
  `deferred_lighting.frag`, and `surface.vert`." **Overridden in Slice A.**
  The legacy `surface.vert` / `surface_gbuffer.frag` pair declares the
  pre-GpuScene `mat4 Model + uint64_t Ptr*` push-constant block (and
  `set = 0/2/3` descriptor sets), so feeding `RHI::GpuScenePushConstants`
  bytes from `DeferredGBufferPass::Execute` into that layout would
  silently misinterpret `SceneTableBDA` as `mat4 Model[0]` and corrupt
  every BDA dereference on real Vulkan. This is the same footgun GRAPHICS-070
  hit on the forward path, resolved there by authoring
  `assets/shaders/forward/default_debug_surface.{vert,frag}` against the
  GpuScene contract. Slice A mirrors that decision for the deferred path:
  it reuses `forward/default_debug_surface.vert.spv` for the vertex stage
  and authors a single new minimal three-RT fragment shader
  (`assets/shaders/deferred/default_debug_gbuffer.frag`) under the
  GpuScene `ScenePC` push-constant contract. A dedicated lit deferred
  GBuffer shader pair (material-aware sampling, real normal output,
  metallic-roughness packing beyond the default-debug fallback) remains a
  follow-up; Slice B/C still author no new shaders unless deferred
  lighting requires a fullscreen-vertex variant. The "Shader push-constant
  compatibility policy" subsection in `src/graphics/renderer/README.md`
  records the durable rule.

## Context
- Status: in-progress (Slice A landed; Slice B active on this branch; Slice C pending).
- Owner/agent: local agent workflow.
- Branch: `claude/setup-agentic-workflow-cQjgU` (Slice B); Slice A landed on
  `claude/setup-agentic-workflow-HSYdR`.
- Next verification step: complete Slice B (DeferredLightingPass wiring +
  `"CompositionPass"` executor branch + GBuffer→Lighting barrier test) and
  run the verification block below; Slice C (shadow-atlas binding +
  end-to-end shadow casting test) remains pending on a fresh branch.
- Owner/layer: `graphics/renderer`.
- Planning anchors: `tasks/done/GRAPHICS-008-depth-surface-gbuffer-passes.md`, `tasks/done/GRAPHICS-009-deferred-lighting-and-shadows.md`, `tasks/done/GRAPHICS-009Q-lighting-shadow-clarifications.md`, `tasks/done/GRAPHICS-073-default-recipe-shadow-pass-wiring.md` (Slice C scope folded in: deferred-lighting shadow-sampler binding + cross-pass `DepthAttachment → ShaderRead` barrier-transition test). Note: per `GRAPHICS-009Q`, the shadow atlas binds at `binding 1` of "the global descriptor set already used by `CameraUBO`". In `assets/shaders/deferred_lighting.frag` that set is `set = 1` (because `set = 0` holds the G-buffer sampled textures `uNormal/uAlbedo/uMaterial/uDepth`), so the deferred lighting binding is `set 1, binding 1` even though the forward path (`surface.frag`) uses `set 0, binding 1` for the same logical slot.
- Today: `Pass.Deferred.GBuffers.cpp` and `Pass.Deferred.Lighting.cpp` exist as command-body shells but are not owned, not piped through the executor, and have no pipelines created. `ShadowSystem` already owns the atlas + `sampler2DShadow`-bindable sampler (GRAPHICS-073 Slice B), so the binding work here is descriptor-set wiring, not resource creation.
- Upstream gates (both done): `GRAPHICS-070` (forward surface; same surface variant, easier to land first), `GRAPHICS-073` (shadows; deferred lighting consumes shadow atlas).

## Required changes
- [x] Add `m_DeferredGBufferPass`, `m_DeferredGBufferPipelineLease` members
  to `NullRenderer` (Slice A landed).
- [x] Add `m_DeferredLightingPass`, `m_DeferredLightingPipelineLease` members
  to `NullRenderer` (Slice B landed).
- [x] In `InitializeOperationalPassResources(device)`, create the GBuffer
  pipeline from `shaders/surface.vert.spv` + `surface_gbuffer.frag.spv` with
  the recorded multi-target output (`SceneNormal` RGBA16F, `Albedo` RGBA8,
  `Material0` RGBA16F) + `SceneDepth` D32_FLOAT (Slice A landed).
- [x] In `InitializeOperationalPassResources(device)`, create the deferred
  lighting pipeline from `shaders/post_fullscreen.vert.spv` +
  `shaders/deferred/lighting.frag.spv` (the GpuScene-aware minimal variant
  whose `layout(push_constant, scalar) PushConstants { uint64_t
  SceneTableBDA; uint _pad0; uint _pad1; }` block matches
  `DeferredLightingPushConstants` byte-for-byte). Full G-buffer sampler +
  shadow-atlas descriptor wiring remains Slice C scope (Slice B landed).
- [x] Republish the GBuffer pipeline byte-identical from
  `RebuildOperationalResources()` (Slice A landed).
- [x] Republish the deferred lighting pipeline byte-identical from
  `RebuildOperationalResources()` (Slice B landed).
- [x] Add deferred-mode `"SurfacePass"` executor branch with the
  `SkippedNonOperational` / `SkippedUnavailable` / `Recorded` taxonomy
  (Slice A landed; routes through `RecordDeferredGBufferPass(...)`).
- [x] Add `"CompositionPass"` executor branch with the same taxonomy
  (Slice B landed; routes through `RecordDeferredLightingPass(...)`).
- [x] Confirm `BuildDefaultFrameRecipe` declares the deferred resources
  (`SceneNormal`, `Albedo`, `Material0`) with the recorded formats and
  finalizes them through `Pass.Deferred.Lighting` (Slice A audited; recipe
  declarations match `BuildDeferredGBufferPipelineDesc()` color-target
  formats).
- [ ] Wire the deferred lighting descriptor set so the `ShadowSystem`-owned
  shadow atlas is bound at `set 1, binding 1` — i.e. `binding 1` of the same
  global descriptor set as `CameraUBO` (`set = 1, binding = 0`) per
  `GRAPHICS-009Q`, matching `assets/shaders/deferred_lighting.frag`'s
  `layout(set = 1, binding = 1) uniform sampler2DShadow shadowAtlas`. Source
  the texture/sampler handles from `ShadowSystem::GetAtlasBindlessIndex()`
  / `GetAtlasSampler()`. (Slice C; absorbed from GRAPHICS-073 Slice C.)
- [x] Slice A only: add `IRenderer::SetLightingPath()` /
  `GetLightingPath()` test seam and
  `GetDeferredGBufferPipeline()` / `GetDeferredGBufferPipelineDesc()`
  accessors.

## Tests
- [x] `contract;graphics` test: with one extracted procedural surface and the
  default recipe + `SetLightingPath(Deferred)`, the deferred-mode
  `"SurfacePass"` records and `RenderGraphCommandPassStats::Recorded`
  includes it (Slice A:
  `RendererFrameLifecycle.DeferredSurfacePassRecordsWhenLightingPathIsDeferred`).
- [x] `contract;graphics` test: with one extracted procedural surface and the
  default recipe, both deferred passes record and
  `RenderGraphCommandPassStats::Recorded` includes them (Slice B extended the
  Slice A test to also assert `"CompositionPass"` records `Recorded`).
- [x] `contract;graphics` test: barrier packets between GBuffers (write) and
  Lighting (read) are emitted in the expected order
  (`SceneNormal/Albedo/Material0` ColorAttachment → ShaderRead) (Slice B:
  `RendererFrameLifecycle.DeferredGBufferToCompositionEmitsColorToShaderReadBarriers`).
- [ ] `contract;graphics` test: barrier packets transition the
  `ShadowSystem`-owned shadow atlas `DepthAttachment → ShaderRead` before
  `Pass.Deferred.Lighting` (Slice C; absorbed from GRAPHICS-073 Slice C).
- [ ] `contract;graphics` test: with one shadow-casting renderable + one
  light wired end-to-end through extraction, the executor reports `Recorded`
  for `ShadowPass` *and* `Pass.Deferred.Lighting`, and the deferred lighting
  descriptor set binds the shadow atlas at `set 1, binding 1` (Slice C;
  absorbed from GRAPHICS-073 Slice C with the binding location corrected to
  match `deferred_lighting.frag`).
- [x] `contract;graphics` test: missing GBuffer pipeline lease →
  `SkippedUnavailable` for the deferred-mode `"SurfacePass"` (Slice A:
  `RendererFrameLifecycle.DeferredSurfacePassSkipsUnavailableWhenPipelineMissing`).
- [x] `contract;graphics` test: missing deferred lighting pipeline lease →
  `SkippedUnavailable` for `"CompositionPass"` (Slice B:
  `RendererFrameLifecycle.DeferredLightingPassSkipsUnavailableWhenPipelineMissing`).
- [x] `contract;graphics` test: GBuffer pipeline lease survives
  `RebuildOperationalResources()` byte-identically (Slice A:
  `RendererFrameLifecycle.DeferredGBufferPipelineSurvivesOperationalRebuild`).
- [x] `contract;graphics` test: deferred lighting pipeline lease survives
  `RebuildOperationalResources()` byte-identically (Slice B:
  `RendererFrameLifecycle.DeferredLightingPipelineSurvivesOperationalRebuild`).

## Docs
- [x] Slice A: update `src/graphics/renderer/README.md` to record deferred
  GBuffer as operationally wired plus the `IRenderer::SetLightingPath()`
  test seam; deferred lighting + shadow-atlas binding documentation stays
  pending in the Slice B/C bullet of the same file.
- [x] Slice B: update `src/graphics/renderer/README.md` to record deferred
  lighting as operationally wired.
- [ ] Slice C: update `src/graphics/renderer/README.md` to record the
  shadow-atlas binding consumer at `set 1, binding 1` (forward path keeps
  using `set 0, binding 1`; absorbed from GRAPHICS-073 Slice C with the
  binding location corrected to match `deferred_lighting.frag`).
- [ ] Update `docs/architecture/rendering-three-pass.md` if pipeline-order
  step numbers shift or shadow ordering relative to deferred lighting
  changes (absorbed from GRAPHICS-073 Slice C).

## Acceptance criteria
- [ ] Both passes record draws in the operational state and increment `Recorded`.
- [ ] Deferred lighting samples the `ShadowSystem`-owned shadow atlas correctly: descriptor set binds it at `set 1, binding 1` (binding 1 of the deferred CameraUBO global set, matching `deferred_lighting.frag`) and the cross-pass barrier transitions it `DepthAttachment → ShaderRead` before `Pass.Deferred.Lighting` (absorbed from GRAPHICS-073 Slice C).
- [ ] No regression in `Pass.Forward.Surface` (forward and deferred can coexist gated by `RenderConfig` or recipe selection).
- [ ] No regression in CPU/null tests.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding clustered light binning, IBL, or PBR feature completeness.
- Adding transparent / special-forward sub-buckets.
- Mutating canonical material descriptor layout.

## Slice plan

The task is large enough that it is sliced before implementation. Slices land
sequentially under the active task; each slice must keep the CPU/null contract
gate green.

- **Slice A (this slice; GBuffer wiring + lighting-path test seam).**
  Wire `DeferredGBufferPass` into the renderer for the deferred `"SurfacePass"`
  executor branch. Add `m_DeferredGBufferPass` + `m_DeferredGBufferPipelineLease`
  to `NullRenderer`, emplaced in `Initialize()` after `m_DeferredSystem`, reset
  in `Shutdown()` before the system; create `BuildDeferredGBufferPipelineDesc()`
  reusing `surface.vert.spv` + `surface_gbuffer.frag.spv` with three color
  targets (`SceneNormal` RGBA16F, `Albedo` RGBA8, `Material0` RGBA16F) +
  `D32_FLOAT` depth; create/republish from `InitializeOperationalPassResources()`
  identically; add `RecordDeferredGBufferPass(...)` recording helper following
  the `RecordForwardSurfacePass` / `RecordShadowPass` pattern; add a
  `"SurfacePass" && defaultRecipeUsesDeferred` executor branch. Add
  `SetLightingPath()` / `GetLightingPath()` test seam on `IRenderer` so contract
  tests can flip the runtime path off the default-Forward derived from
  `DeriveDefaultFrameRecipeFeatures`. Add
  `GetDeferredGBufferPipeline()` / `GetDeferredGBufferPipelineDesc()` accessors.
  Slice A's contract tests cover: (a) byte-identical rebuild survival,
  (b) deferred-mode `SurfacePass` recording as `Recorded` when culling output
  is available, (c) `SkippedUnavailable` when the pipeline lease is missing,
  (d) forward path remains `Recorded` when lighting path stays Forward.

- **Slice B (follow-up; DeferredLightingPass wiring + GBuffer→Lighting barrier
  test).** Wire `DeferredLightingPass` into the executor for the
  `"CompositionPass"` branch. Add `m_DeferredLightingPass` +
  `m_DeferredLightingPipelineLease`; create the deferred-lighting pipeline
  from a fullscreen vertex pair + `deferred_lighting.frag`. Add a
  `RecordDeferredLightingPass(...)` recording helper. Cover the
  `SceneNormal/Albedo/Material0` ColorAttachment → ShaderRead cross-pass
  barrier test.

- **Slice C (follow-up; shadow-atlas binding + end-to-end shadow casting
  test).** Wire the deferred lighting descriptor set so the
  `ShadowSystem`-owned shadow atlas binds at `set 1, binding 1` (binding 1 of
  the deferred CameraUBO global set per `GRAPHICS-009Q`, matching
  `assets/shaders/deferred_lighting.frag`). Cover the shadow-atlas
  `DepthAttachment → ShaderRead` cross-pass barrier test and the end-to-end
  shadow-casting `Recorded`-for-both-passes test (absorbed from GRAPHICS-073
  Slice C).

## Next verification step
- Land Slice A: pipelines + executor route for GBuffer + lighting-path test
  seam + the three slice-A contract tests. Run the verification block above
  and assert no regression in the forward-path contract tests.
