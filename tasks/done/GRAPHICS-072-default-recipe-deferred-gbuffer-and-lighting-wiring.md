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
- State: done. Slices A, B, and C all landed.
- Owner/agent: local agent workflow.
- Branch: Slice A — `claude/setup-agentic-workflow-HSYdR`; Slice B —
  `claude/setup-agentic-workflow-cQjgU`; Slice C —
  `claude/setup-agentic-workflow-2HdHw`.
- PR: Slice A — PR #886; Slice B — PR #887, PR #888; Slice C — TBD (this
  branch).
- Commit: Slice A — `87e5489`, `c4aa607`; Slice B — `9469aaa`, `4127670`;
  Slice C — TBD (this branch).
- Completed: 2026-05-19. Slice C retirement landed via the same branch.
- Completion verification: see "Completion note" below.
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
- [x] Wire the deferred lighting descriptor set so the `ShadowSystem`-owned
  shadow atlas is bound. The original spec named `set 1, binding 1` matching
  `assets/shaders/deferred_lighting.frag`'s `layout(set = 1, binding = 1)
  uniform sampler2DShadow shadowAtlas`; on the promoted Vulkan pipeline
  layout (bindless-only, `setLayoutCount = 1` with the bindless heap at
  `set = 0`) the equivalent wiring is the
  `DeferredLightingPushConstants::ShadowAtlasBindlessIndex` push-constant
  field sourced from `ShadowSystem::GetAtlasBindlessIndex()`. The shader
  (`assets/shaders/deferred/lighting.frag`) gained the matching push-constant
  slot and samples the atlas through
  `globalTextures[nonuniformEXT(pc.ShadowAtlasBindlessIndex)]`. (Slice C
  landed; absorbed from GRAPHICS-073 Slice C.)
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
- [x] `contract;graphics` test: barrier packets transition the
  `ShadowSystem`-owned shadow atlas `DepthAttachment → ShaderRead` before
  `Pass.Deferred.Lighting` (Slice C:
  `RendererFrameLifecycle.DeferredLightingShadowAtlasTransitionsDepthToShaderReadBeforeComposition`).
- [x] `contract;graphics` test: with shadows enabled through the renderer's
  `ShadowSystem` (`SetParams(...)` allocates the atlas + registers it in
  the bindless heap), the executor reports `Recorded` for `ShadowPass` and
  `CompositionPass`, and the deferred lighting pass's push constants carry
  `ShadowAtlasBindlessIndex == ShadowSystem::GetAtlasBindlessIndex()` (the
  bindless-heap equivalent of the legacy `set 1, binding 1` shadow-atlas
  binding) (Slice C:
  `RendererFrameLifecycle.DeferredLightingPushConstantsCarryShadowAtlasBindlessIndex`).
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
- [x] Slice C: update `src/graphics/renderer/README.md` to record the
  shadow-atlas bindless-index push-constant binding (the bindless-only
  pipeline-layout equivalent of the legacy `set 1, binding 1`
  `sampler2DShadow` model); forward path's `surface.frag` shadow-atlas
  binding stays unaffected.
- [x] Update `docs/architecture/rendering-three-pass.md` to record that
  the deferred `SurfacePass` no longer samples the shadow atlas (GBuffer
  pass) and that `CompositionPass` samples through the bindless heap with
  the atlas slot pushed via `ShadowAtlasBindlessIndex`.

## Acceptance criteria
- [x] Both passes record draws in the operational state and increment `Recorded`.
- [x] Deferred lighting samples the `ShadowSystem`-owned shadow atlas correctly: the lighting pass pushes the atlas's bindless slot through `DeferredLightingPushConstants::ShadowAtlasBindlessIndex` (the bindless-only pipeline-layout equivalent of the legacy `set 1, binding 1` model), and the cross-pass barrier transitions the atlas `DepthAttachment → ShaderReadOnly` before `Pass.Deferred.Lighting` (absorbed from GRAPHICS-073 Slice C).
- [x] No regression in `Pass.Forward.Surface` (forward and deferred coexist gated by `SetLightingPath`; forward path's `surface.frag` shadow-atlas read is unaffected).
- [x] No regression in CPU/null tests.

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
- (Historical, Slice A.) Land Slice A: pipelines + executor route for
  GBuffer + lighting-path test seam + the three slice-A contract tests.
  Run the verification block above and assert no regression in the
  forward-path contract tests.

## Completion note

Slice C closed the task on 2026-05-19 with the following verification run
against branch `claude/setup-agentic-workflow-2HdHw`:

```
cmake --preset ci
cmake --build build/ci --target IntrinsicGraphicsContractTests
cmake --build build/ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -L 'contract' \
      -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

Result: 161/161 contract tests pass, including the two new Slice C tests
`RendererFrameLifecycle.DeferredLightingShadowAtlasTransitionsDepthToShaderReadBeforeComposition`
and
`RendererFrameLifecycle.DeferredLightingPushConstantsCarryShadowAtlasBindlessIndex`.
Layering / doc-link / task-policy / test-layout checks all clean. The
`IntrinsicGraphicsRendererCpuUnitTests` (41), `IntrinsicGraphicsUnitTests`
(28), and `IntrinsicRuntimeGraphicsCpuTests` (22) targets were also run
to catch incidental regressions from the recipe / push-constant changes;
all green.

Architectural disposition of the original "set 1, binding 1" spec: the
promoted Vulkan pipeline layout declares only the bindless heap at
`set = 0` (`setLayoutCount = 1` in
`src/graphics/vulkan/Backends.Vulkan.Device.cpp`), so the legacy
multi-descriptor-set shadow-sampler binding from
`assets/shaders/deferred_lighting.frag` is not representable. Slice C
substitutes the equivalent wiring: the atlas's bindless slot is published
through `DeferredLightingPushConstants::ShadowAtlasBindlessIndex` and
sampled in `assets/shaders/deferred/lighting.frag` via
`globalTextures[nonuniformEXT(pc.ShadowAtlasBindlessIndex)]`. The
forward path's `assets/shaders/surface.frag` `set 0, binding 1`
`sampler2DShadow` is untouched.
