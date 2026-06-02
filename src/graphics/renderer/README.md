# Graphics

`src/graphics/renderer` owns rendering, GPU scene state, and GPU pass orchestration.
It is layered into backend-agnostic RHI abstractions, backend implementations,
and renderer/render-graph orchestration.

## Public module surface

### Renderer and graph

- `Extrinsic.Graphics.Renderer`
- `Extrinsic.Graphics.FrameRecipe`
- `Extrinsic.Graphics.RenderGraph`

`Extrinsic.Graphics.RenderGraph` re-exports:

- `Extrinsic.Graphics.RenderGraph:Resources`
- `Extrinsic.Graphics.RenderGraph:Pass`
- `Extrinsic.Graphics.RenderGraph:Compiler`
- `Extrinsic.Graphics.RenderGraph:Barriers`
- `Extrinsic.Graphics.RenderGraph:TransientAllocator`
- `Extrinsic.Graphics.RenderGraph:Executor`

`Extrinsic.Graphics.RenderGraph` executes barrier packets in pass order: for each pass, any packets tagged with that pass index are emitted immediately before the pass callback, and imported-resource final-state packets (the compiler's end-of-graph sentinel) are emitted after the last pass. The graph compiler's transient handles are logical scheduling handles only; before the concrete renderer lowers packets through `RHI::ICommandContext::SubmitBarriers`, it replaces non-imported used texture/buffer handles with real per-frame RHI allocations cached by frame slot and descriptor. This keeps backend device handle spaces authoritative and prevents synthetic graph handles from colliding with live backend resources such as Vulkan swapchain images.

Render-graph diagnostics use `RenderGraphValidationResult` findings tagged by
`RenderGraphValidationSeverity` and `RenderGraphValidationCode`. Bare compiled
graphs can be checked with `ValidateCompiledGraph(...)`; recipe-built graphs use
`ValidateRecipeCompiledGraph(const FrameRecipeIntrospection&, const
CompiledRenderGraph&)`, which derives `ImportedResourceAuthorization` entries
from the typed frame recipe before forwarding to the framegraph validator.
`CompiledRenderGraph::ValidationFindings` stores the recipe-less findings
generated during compilation, and `GetLastCompileValidationResult()` exposes
structured hard-error findings when `Compile()` fails. Callers needing a
human-readable summary should read `Findings.front().Message`.

Per `GRAPHICS-033E`, `Graphics.Renderer.cpp::ExecuteFrame()` publishes the
recipe-aware validation outcome to the device via
`RHI::IDevice::NoteRecipeGraphValidation(bool)` exactly once per recipe compile
attempt. After a successful `RenderGraph::Compile()` the renderer calls
`ValidateRecipeCompiledGraph(...)` on the active recipe's introspection and
publishes `result.CountBySeverity(RenderGraphValidationSeverity::Error) == 0u`.
The recipe-aware result is the sole source of truth because it carries the
`ImportedResourceAuthorization` entries derived from the recipe; the bare
compile-time `GetLastCompileValidationResult()` lacks that context and will
report `UnauthorizedImportedBufferWrite` errors for any imported write from
a non-side-effect pass (e.g. `CullingPass` writing `Cull.*` buffers), so it
is not consulted by the published bool. A failed recipe build or a failed
`Compile()` publishes `false` so the backend's operational gate cannot
inherit a stale-clean state. Non-Vulkan backends inherit the default no-op
implementation.

### Scene and sync systems

- `Extrinsic.Graphics.RenderFrameInput`
- `Extrinsic.Graphics.RenderWorld`
- `Extrinsic.Graphics.RuntimeRenderSnapshotBatch` (declared by `Extrinsic.Graphics.Renderer`)
- `Extrinsic.Graphics.GpuWorld`
- `Extrinsic.Graphics.Material`
- `Extrinsic.Graphics.MaterialSystem`
- `Extrinsic.Graphics.ColormapSystem`
- `Extrinsic.Graphics.VisualizationPackets`
- `Extrinsic.Graphics.VisualizationSyncSystem`
- `Extrinsic.Graphics.CullingSystem`
- `Extrinsic.Graphics.DebugViewSystem`
- `Extrinsic.Graphics.ImGuiOverlaySystem`
- `Extrinsic.Graphics.LightSystem`
- `Extrinsic.Graphics.SelectionSystem`
- `Extrinsic.Graphics.ForwardSystem`
- `Extrinsic.Graphics.SpatialDebugVisualizers`
- `Extrinsic.Graphics.GpuScene`
- `Extrinsic.Graphics.DeferredSystem`
- `Extrinsic.Graphics.PostProcessSystem`
- `Extrinsic.Graphics.ShadowSystem`
- `Extrinsic.Graphics.TransformSyncSystem`

### RHI modules (`Graphics/RHI`)

- `Extrinsic.RHI.Device`
- `Extrinsic.RHI.CommandContext`
- `Extrinsic.RHI.FrameHandle`
- `Extrinsic.RHI.BufferManager`
- `Extrinsic.RHI.BufferView`
- `Extrinsic.RHI.TextureManager`
- `Extrinsic.RHI.SamplerManager`
- `Extrinsic.RHI.PipelineManager`
- `Extrinsic.RHI.Bindless`
- `Extrinsic.RHI.Transfer`
- `Extrinsic.RHI.Profiler`
- `Extrinsic.RHI.Handles`
- `Extrinsic.RHI.Descriptors`
- `Extrinsic.RHI.Types`

### Backends

- `Extrinsic.Backends.Null`

### Pass modules (`Graphics/Passes`)

- `Extrinsic.Graphics.Pass.Culling`
- `Extrinsic.Graphics.Pass.DepthPrepass`
- `Extrinsic.Graphics.Pass.Deferred.GBuffers`
- `Extrinsic.Graphics.Pass.Deferred.Lighting`
- `Extrinsic.Graphics.Pass.DebugView`
- `Extrinsic.Graphics.Pass.Forward.Surface`
- `Extrinsic.Graphics.Pass.Forward.Line`
- `Extrinsic.Graphics.Pass.Forward.Point`
- `Extrinsic.Graphics.Pass.PostProcess.Bloom`
- `Extrinsic.Graphics.Pass.PostProcess.FXAA`
- `Extrinsic.Graphics.Pass.PostProcess.Histogram`
- `Extrinsic.Graphics.Pass.PostProcess.SMAA`
- `Extrinsic.Graphics.Pass.PostProcess.ToneMap`
- `Extrinsic.Graphics.Pass.Selection.EntityId`
- `Extrinsic.Graphics.Pass.Selection.PointId`
- `Extrinsic.Graphics.Pass.Selection.EdgeId`
- `Extrinsic.Graphics.Pass.Selection.FaceId`
- `Extrinsic.Graphics.Pass.Selection.Outline`
- `Extrinsic.Graphics.Pass.Shadows`
- `Extrinsic.Graphics.Pass.ImGui`
- `Extrinsic.Graphics.Pass.Present`
- `Extrinsic.Graphics.Pass.TransientDebug.Surface`

## Ownership contract

- `Runtime` owns live ECS access, extraction, sidecar mappings, dirty-domain
  interpretation, deletion events, and compaction/relocation handoff.
- `Graphics` consumes immutable snapshots/views supplied by runtime and owns GPU
  resource/state transitions and pass-level scheduling through
  `Graphics.RenderGraph`.
- `Graphics.FrameRecipe` owns the reusable default frame recipe: typed feature
  gates, canonical resource declarations, pass-order introspection, and the
  backend-agnostic graph construction path used by the null renderer.

### Shader push-constant compatibility policy

Every default-recipe pass whose `Execute()` body calls
`cmd.PushConstants(&pc, sizeof(pc))` MUST be paired with a pipeline whose
shaders declare a `layout(push_constant) ...` block that mirrors the pushed
struct byte-for-byte. The CPU/null contract gate happily reports `Recorded`
when a pipeline has a valid handle, but on a real Vulkan run a mismatched
push-constant layout silently reinterprets the bytes — the canonical bug
shape is the renderer pushing `RHI::GpuScenePushConstants` (starting with
`uint64_t SceneTableBDA`) into a shader that declares `mat4 Model + uint64_t
PtrPositions + ...`, so the scene-table pointer lands in `Model[0]` and
every subsequent BDA dereference reads garbage.

Concretely:

- The promoted retained passes (`Pass.DepthPrepass`, `Pass.Forward.Surface`,
  `Pass.Forward.Line`, `Pass.Forward.Point`, `Pass.Shadows`,
  `Pass.Deferred.GBuffers`, `Pass.Deferred.Lighting`) push
  `RHI::GpuScenePushConstants { uint64_t SceneTableBDA; uint FrameIndex;
  uint DrawBucket; uint DebugMode; uint _pad0; }` (and small post-prefix
  extensions like `Pass.Deferred.Lighting`'s `SceneTableBDA`-only variant).
  Their pipelines MUST select shaders that declare the matching
  `layout(push_constant, scalar) ScenePC { uint64_t SceneTableBDA; uint
  FrameIndex; uint DrawBucket; uint DebugMode; uint _pad0; }` block — i.e.
  the shader files under `assets/shaders/forward/`,
  `assets/shaders/deferred/`, and the GpuScene-aware
  `assets/shaders/depth_prepass.vert`.
- The legacy shader pairs under `assets/shaders/` root —
  `surface.vert`, `surface.frag`, `surface_gbuffer.frag`,
  `shadow_depth.vert`, etc. — declare the pre-GpuScene push block
  (`mat4 Model + uint64_t Ptr*`) and DescriptorSet expectations
  (`set = 2/3` SSBOs) that no promoted retained pipeline can satisfy.
  They MUST NOT be referenced by any new `BuildXxxPipelineDesc()`.
  GRAPHICS-070 (forward surface), GRAPHICS-071 (forward line/point),
  GRAPHICS-073 (shadow), and GRAPHICS-072 (deferred GBuffer) each
  resolved this by selecting GpuScene-aware shader pairs from
  `assets/shaders/forward/` or `assets/shaders/deferred/`; new pass
  wirings must follow the same precedent.
- When the natural new-pipeline path would otherwise be the legacy
  shaders, author a `default_debug_*` minimal variant under the matching
  `forward/` or `deferred/` directory rather than feeding the legacy
  layout — see `assets/shaders/forward/default_debug_surface.{vert,frag}`
  (GRAPHICS-031A) and `assets/shaders/deferred/default_debug_gbuffer.frag`
  (GRAPHICS-072 Slice A) as the canonical templates.
- Reviewers and `docs/agent/review-checklist.md` should treat this as a
  hard gate: a passing CPU/null contract test that asserts only
  `Recorded` is *not* sufficient evidence that the pipeline is well-formed
  on a real backend. Either assert the SPV path explicitly in the
  contract test (`EXPECT_TRUE(initialDesc.VertexShaderPath.ends_with(
  "shaders/forward/...")`) or pair the test with a corresponding
  `gpu;vulkan` smoke that runs the pass.

- GRAPHICS-070 wires the default-recipe `"SurfacePass"` to the existing
  `ForwardSurfacePass` body. `NullRenderer` owns the
  `m_ForwardSurfacePass` instance (constructed against the renderer's
  `m_ForwardSystem`) and the `m_ForwardSurfacePipelineLease`. The pipeline is
  created in `InitializeOperationalPassResources()` from
  `BuildForwardSurfacePipelineDesc()` (vertex
  `shaders/forward/default_debug_surface.vert.spv`, fragment
  `shaders/forward/default_debug_surface.frag.spv` — the canonical
  GpuScene-aware shader pair whose push-constant block and BDA-only
  descriptor contract match `sizeof(GpuScenePushConstants)`;
  `DepthCompareOp = Equal` with `DepthWriteEnable = false` matching the
  depth-prepass-on contract, single RGBA16F color target for
  `SceneColorHDR`, and `D32_FLOAT` depth), and republished byte-identical
  through `RebuildOperationalResources()`. `Initialize()` emplaces
  `m_ForwardSystem` + `m_ForwardSurfacePass` *before* calling
  `InitializeOperationalPassResources()` so the publisher's
  `SetPipeline(...)` actually lands on the pass on the initial operational
  path (otherwise the first frame would observe a `has_value()` pipeline
  lease but a default-constructed handle on the pass and `Execute()` would
  early-return on `!m_Pipeline.IsValid()` while the executor still
  reported `Recorded`). The legacy `assets/shaders/surface.vert/frag` pair
  predates the GpuScene seam (it declares `mat4 Model` + `PtrPositions`
  push constants plus `set = 0/2/3` descriptor sets) and is deliberately
  *not* referenced by the new pipeline; a dedicated lit forward-surface
  shader is a GRAPHICS-072 follow-up. The executor's `"SurfacePass"` branch
  routes to `RecordForwardSurfacePass(...)` only when the active
  default-recipe features select the forward lighting path; deferred mode
  routes to `RecordDeferredGBufferPass(...)` per GRAPHICS-072 Slice A. While
  GRAPHICS-072 is still open, `DeriveDefaultFrameRecipeFeatures()` selects
  `FrameRecipeLightingPath::Forward` so the default recipe stays on the
  forward surface body; contract tests opt into the deferred-mode branch
  through `IRenderer::SetLightingPath(FrameRecipeLightingPath::Deferred)`
  (the renderer-stored test seam added in GRAPHICS-072 Slice A).
- GRAPHICS-071 wires the default-recipe `"LinePass"` and `"PointPass"` to
  the existing retained-renderable `ForwardLinePass` and `ForwardPointPass`
  bodies. `NullRenderer` owns `m_ForwardLinePass`, `m_ForwardPointPass`,
  `m_ForwardLinePipelineLease`, and `m_ForwardPointPipelineLease` alongside the
  forward surface pass. The line pipeline uses `shaders/line.vert.spv` +
  `shaders/line.frag.spv` with `Topology::LineList`; the point pipeline uses
  `shaders/point.vert.spv` + `shaders/point_retained.frag.spv` with
  `Topology::PointList`. Both load `SceneDepth`, append into `SceneColorHDR`,
  disable depth writes, enable alpha blending, use `CullMode::None`, and carry
  the canonical `GpuScenePushConstants` block. `point_retained.frag` is the
  canonical retained-renderable point variant for this path; transient
  debug-point expansion remains owned by GRAPHICS-077 and must not route through
  the retained `Points` cull bucket.
- GRAPHICS-073 Slice A wires the default-recipe `"ShadowPass"` to the
  existing `ShadowPass` body. `NullRenderer` owns `m_ShadowPass` (constructed
  against `m_ShadowSystem`) and the depth-only `m_ShadowPipelineLease`. The
  shadow pipeline reuses `shaders/depth_prepass.vert.spv` for the GpuScene
  push-constant + BDA descriptor contract, declares no fragment shader and no
  color targets, runs `Topology::TriangleList` with back-face culling, enables
  depth writes with `DepthOp::LessOrEqual`, targets a single `D32_FLOAT`
  depth attachment matching the recipe's transient `ShadowAtlas` declaration,
  and carries the canonical `GpuScenePushConstants` block (the executor still
  pushes scene-table BDA + `FrameIndex` + `ShadowOpaque` bucket kind, not a
  per-cascade index — that arrives with the dedicated shadow-depth shader in
  a later slice). `Initialize()` emplaces `m_ShadowSystem` + `m_ShadowPass`
  *before* calling `InitializeOperationalPassResources()` so the publisher's
  `SetPipeline(...)` actually lands on the pass on the initial operational
  path. The pipeline is republished byte-identical through
  `RebuildOperationalResources()` using the same reset-then-publish pattern
  as the forward pipelines.
- GRAPHICS-073 Slice B promotes `ShadowSystem` to own the depth atlas + the
  `sampler2DShadow`-bindable sampler. `ShadowSystem::Initialize(device,
  textureMgr, samplerMgr)` stores manager references and lazily allocates the
  atlas (`D32_FLOAT`, sized as `AtlasResolution × CascadeCount`-by-
  `AtlasResolution`) on the first `SetParams(...)` call that enables shadows;
  the same `Initialize` path runs through the renderer's `m_TextureManager` /
  `m_SamplerManager` so the atlas survives `RebuildOperationalResources()`
  byte-identically. The sampler is created `Linear`/`ClampToBorder` with
  `OpaqueWhiteFloat` border + `CompareEnable=true,Compare=Less` so it matches
  the `sampler2DShadow` contract from `GRAPHICS-009Q`. `FrameRecipeImports`
  gains an optional `ShadowAtlas` handle and `FrameRecipeShadowSizing` becomes
  the typed sizing seam; `BuildDefaultFrameRecipe` imports the
  `ShadowSystem`-owned atlas with `InitialState=Undefined,
  FinalState=DepthWrite` (the same idiom the Backbuffer uses) when the
  handle is valid, and falls back to the Slice A viewport-sized transient
  otherwise. The `Undefined/DepthWrite` import pair is the cross-frame
  contract: the compiler seeds imported state from `InitialState` every
  frame, so a fresh `Undefined→DepthWrite` barrier is emitted at every
  `Pass.Shadows` entry. Vulkan handles `Undefined→DepthWrite` as a
  discard-and-transition, which is correct for the shadow atlas (rewritten
  each frame) and keeps the cross-frame loop closed (`FinalState=DepthWrite`
  matches the next frame's first-use layout). `Pass.Shadows::Execute` records a new
  `ShadowDiagnostics::MissingCasterCount` increment when shadows are enabled
  but the `ShadowOpaque` cull bucket is empty, so operators can distinguish
  "no casters this frame" from "atlas wiring broken" without inspecting the
  executor's `SkippedUnavailable` taxonomy. The deferred-lighting
  shadow-sampler binding (`set 1, binding 1` in
  `assets/shaders/deferred_lighting.frag`, i.e. binding 1 of the same
  global descriptor set as the deferred-path CameraUBO per
  `GRAPHICS-009Q`; the forward `surface.frag` keeps the equivalent slot at
  `set 0, binding 1`) is owned by GRAPHICS-072 (absorbed from the original
  GRAPHICS-073 Slice C scope) and lands alongside the deferred GBuffer +
  lighting passes.
- GRAPHICS-072 Slice A wires the default-recipe deferred-mode `"SurfacePass"`
  to the existing `DeferredGBufferPass` body. `NullRenderer` owns
  `m_DeferredGBufferPass` (constructed against `m_DeferredSystem`) and the
  `m_DeferredGBufferPipelineLease`. The pipeline is created in
  `InitializeOperationalPassResources()` from
  `BuildDeferredGBufferPipelineDesc()` (vertex
  `shaders/forward/default_debug_surface.vert.spv` — shared with the
  forward default-debug-surface pipeline so the GpuScene push-constant
  block matches `DeferredGBufferPass::Execute`'s
  `cmd.PushConstants(&GpuScenePushConstants, sizeof(...))` byte-for-byte —
  paired with the new GpuScene-aware
  `shaders/deferred/default_debug_gbuffer.frag.spv` that emits the three
  GBuffer color attachments matching the frame recipe's deferred
  declarations — `SceneNormal` RGBA16F, `Albedo` RGBA8, `Material0` RGBA16F
  — with `D32_FLOAT` depth, `DepthOp::Equal` and `DepthWriteEnable=false`
  matching the depth-prepass-on contract). The legacy
  `assets/shaders/surface.vert` + `surface_gbuffer.frag` pair is
  deliberately *not* referenced — see the "Shader push-constant
  compatibility policy" subsection above for the explicit rule and the
  silent-misinterpretation bug shape it prevents. The pipeline is
  republished byte-identical through `RebuildOperationalResources()`
  using the same reset-then-publish pattern as the forward and shadow
  pipelines. `Initialize()` emplaces
  `m_DeferredSystem` + `m_DeferredGBufferPass` *before* calling
  `InitializeOperationalPassResources()` so the publisher's `SetPipeline(...)`
  actually lands on the pass on the initial operational path. The executor's
  `"SurfacePass"` branch routes to `RecordDeferredGBufferPass(...)` when the
  active default-recipe features select the deferred lighting path; forward
  mode continues to route to `RecordForwardSurfacePass(...)` per GRAPHICS-070.
  `IRenderer::SetLightingPath(FrameRecipeLightingPath)` is the renderer-stored
  test seam that flips the runtime `LightingPath` after
  `DeriveDefaultFrameRecipeFeatures()` derives the default (`Forward`); the
  derivation continues to return `Forward` by default so existing contract
  tests stay green, and contract tests opt into the deferred-mode executor
  branch by calling `SetLightingPath(FrameRecipeLightingPath::Deferred)`. The
  deferred-lighting shadow-atlas binding (originally specified as
  `set 1, binding 1`) is owned by GRAPHICS-072 Slice C; on the promoted
  Vulkan pipeline layout (bindless-only, `setLayoutCount = 1` with the
  bindless heap at `set = 0`) the equivalent wiring is the
  `DeferredLightingPushConstants::ShadowAtlasBindlessIndex` push-constant
  field sourced from `ShadowSystem::GetAtlasBindlessIndex()`.
- GRAPHICS-072 Slice B wires the default-recipe `"CompositionPass"` to the
  existing `DeferredLightingPass` body. `NullRenderer` owns
  `m_DeferredLightingPass` (constructed against `m_DeferredSystem` alongside
  `m_DeferredGBufferPass`) and the `m_DeferredLightingPipelineLease`. The
  pipeline is created in `InitializeOperationalPassResources()` from
  `BuildDeferredLightingPipelineDesc()` (vertex
  `shaders/post_fullscreen.vert.spv` — the canonical fullscreen-triangle
  generator with no vertex inputs and no push constants — paired with
  `shaders/deferred/lighting.frag.spv` whose
  `layout(push_constant, scalar) PushConstants { uint64_t SceneTableBDA;
  uint _pad0; uint _pad1; }` block matches `DeferredLightingPushConstants`
  byte-for-byte, single RGBA16F color target for `SceneColorHDR`, no depth
  attachment, `CullMode::None`, depth test off, blend off), and republished
  byte-identical through `RebuildOperationalResources()` using the same
  reset-then-publish pattern as the GBuffer pipeline. The legacy
  `assets/shaders/deferred_lighting.frag` is deliberately *not* referenced:
  it declares a much larger Blinn-Phong `Push { mat4 InvViewProj; ... }`
  block plus multiple descriptor sets (G-buffer samplers + CameraUBO +
  sampler2DShadow) that no current `DeferredLightingPass::Execute` body
  satisfies, and feeding `DeferredLightingPushConstants` bytes into that
  layout would silently misinterpret `SceneTableBDA` as part of
  `mat4 InvViewProj[0]` — the same footgun shape Slice A documents above
  and the renderer push-constant compatibility policy prohibits. The
  executor's `"CompositionPass"` branch routes to
  `RecordDeferredLightingPass(...)` whenever the recipe declares the pass
  (i.e. `usesDeferred`); the cross-pass `SceneNormal`/`Albedo`/`Material0`
  `ColorAttachment → ShaderReadOnly` barriers are emitted by the framegraph
  compiler from the recipe's `Read(..., ShaderRead)` declarations on the
  CompositionPass. The full G-buffer/CameraUBO sampler wiring remains a
  follow-up; Slice C completes the shadow-atlas binding contract below.
- GRAPHICS-072 Slice C wires the deferred lighting pass to the
  `ShadowSystem`-owned shadow atlas. `DeferredLightingPass` takes a
  `ShadowSystem&` alongside `DeferredSystem&` so `Execute(...)` can publish
  the atlas's bindless slot through push constants; the
  `DeferredLightingPushConstants` struct adds a `ShadowAtlasBindlessIndex`
  field (replacing the Slice B `_pad0` byte-for-byte, so
  `BuildDeferredLightingPipelineDesc()` still records a 16-byte
  `PushConstantSize`) and `assets/shaders/deferred/lighting.frag` adds a
  matching `uint ShadowAtlasBindlessIndex` slot in its push-constant block,
  declares `layout(set = 0, binding = 0) uniform sampler2D globalTextures[]`
  (the engine's bindless heap), and samples the atlas through
  `globalTextures[nonuniformEXT(pc.ShadowAtlasBindlessIndex)]` when the
  index is non-zero (slot 0 stays reserved as the engine default/error
  texture and the `kInvalidBindlessIndex` sentinel keeps shadow sampling
  off when the atlas has not been allocated). The recipe-side change is
  twofold: the deferred `SurfacePass` no longer declares
  `builder.Read(shadowAtlas, ...)` (the GBuffer pass does not sample
  shadows; that responsibility moved entirely to the composition pass), and
  the `CompositionPass`'s shadow-atlas read switched from
  `TextureUsage::DepthRead` to `TextureUsage::ShaderRead` so the
  framegraph compiler emits a `DepthAttachment → ShaderReadOnly` layout
  transition between `ShadowPass` (which writes the atlas as `DepthWrite`)
  and the composition pass. The forward path's `SurfacePass`
  shadow-atlas read stays as `TextureUsage::DepthRead` (the legacy
  `assets/shaders/surface.frag` model — a real `sampler2DShadow` at
  `set = 0, binding = 1` — and is unaffected by this slice). Shadow-atlas
  binding policy: the legacy `set 1, binding 1` `sampler2DShadow` from
  `assets/shaders/deferred_lighting.frag` cannot be honored on the
  bindless-only promoted Vulkan pipeline layout (the engine declares only
  `set = 0` plus a push-constant range), so the bindless-index
  push-constant is the durable wiring. The `DescribeDefaultFrameRecipe`
  introspection removes `ShadowAtlas` from the deferred `SurfacePass`
  inputs to match.
- GRAPHICS-074 Slice A wires the default-recipe `"PickingPass"` to the
  existing `EntityIdPass` body. `NullRenderer` owns `m_SelectionEntityIdPass`
  (constructed against `m_SelectionSystem`) and the
  `m_SelectionEntityIdPipelineLease`. The pipeline is created in
  `InitializeOperationalPassResources()` from
  `BuildSelectionEntityIdPipelineDesc()` and republished byte-identical
  through `RebuildOperationalResources()` using the same reset-then-publish
  pattern as the forward, shadow, and deferred pipelines. The shader pair
  is `shaders/selection/entity_id.vert.spv` + `shaders/selection/entity_id.frag.spv`:
  the vertex stage reuses the GpuScene-aware fetch chain (matches the
  `RHI::GpuScenePushConstants` push block byte-for-byte) and forwards the
  per-instance `inst.EntityID` as a flat varying; the fragment writes two
  R32_UINT outputs matching the recipe's `PickingPass` color targets —
  location 0 = stable entity ID into `EntityId`, location 1 =
  `EncodeSelectionId(SelectionPrimitiveDomain::Entity, 0)` into
  `PrimitiveId` per `GRAPHICS-012Q`. The legacy
  `assets/shaders/pick_id.{vert,frag}` declares the pre-GpuScene
  `mat4 Model + PtrPositions + ... + uint EntityID` push block and is
  deliberately *not* referenced — see the "Shader push-constant
  compatibility policy" subsection above for the explicit rule. The
  executor's `"PickingPass"` branch routes to
  `RecordSelectionEntityIdPass(...)` with the standard
  `SkippedNonOperational` / `SkippedUnavailable` / `Recorded` taxonomy.
  `Initialize()` emplaces `m_SelectionSystem` + `m_SelectionEntityIdPass`
  *before* calling `InitializeOperationalPassResources()` so the
  publisher's `SetPipeline(...)` actually lands on the pass on the
  initial operational path. The Face/Edge/Point selection sub-passes
  (Slice B), the outline pipeline + `"SelectionOutlinePass"` executor
  route (Slice C), and the `Picking.Readback` buffer + drain +
  `PublishPickResult`/`PublishNoHit` wiring (Slice D) remain.
- GRAPHICS-074 recipe-side follow-up (between Slice A and Slice B)
  reorders the default recipe so `addOrderedPass("PickingPass", ...)`
  runs *after* `addOrderedPass("DepthPrepass", ...)` and declares
  `builder.Read(SceneDepth, DepthRead)` on the picking pass. The matching
  introspection gate in `DescribeDefaultFrameRecipe` enables picking on
  `features.EnablePicking && features.EnableDepthPrepass` and lists
  `SceneDepth` in the pass's reads; the recipe declares the pass only
  when a pick request is pending (`world.HasPendingPick ||
  world.PickRequest.Pending`) *and* a depth prepass is configured. Both
  `DescribeDefaultFrameRecipe` and `BuildDefaultFrameRecipe` derive the
  same `pickingActive = EnablePicking && EnableDepthPrepass` conjunction
  and gate the picking-only `PrimitiveId` color target and
  `Picking.Readback` host-visible buffer on it (and the `EntityId` color
  target on `pickingActive || EnableSelectionOutline`, since
  SelectionOutlinePass is the only other `EntityId` consumer), so the
  recipe never allocates dead full-resolution R32_UINT targets / the
  readback buffer when picking is dropped. `BuildSelectionEntityIdPipelineDesc()`
  now mirrors the depth-equal / depth-write-off / `D32_FLOAT` shape the
  forward and deferred GBuffer pipelines use against the same depth
  buffer, so the recipe-emitted render pass with a read-only `D32_FLOAT`
  depth attachment is render-pass-compatible *and* the depth-equal test
  guarantees only the nearest-surface fragment wins each pixel of
  `EntityId`/`PrimitiveId`. Without this reorder Slice D's readback
  drain would return wrong IDs for any pixel covered by more than one
  draw because the previous color-only picking pass had no
  nearest-surface fragment selection. The Face/Edge/Point selection
  pipelines added by Slice B follow this same depth-equal shape; the
  outline pipeline (Slice C) is unaffected by the reorder.
- GRAPHICS-074 Slice B extends the default-recipe `"PickingPass"` executor
  branch to fan out to the Face / Edge / Point selection ID sub-passes
  alongside the EntityId sub-pass. `NullRenderer` owns
  `m_SelectionFaceIdPass` / `m_SelectionEdgeIdPass` /
  `m_SelectionPointIdPass` (each constructed against `m_SelectionSystem`,
  emplaced *before* `InitializeOperationalPassResources()` runs so the
  publisher's `SetPipeline(...)` actually lands on each pass on the
  initial operational path) and the matching pipeline leases
  `m_Selection{Face,Edge,Point}IdPipelineLease`. Each pipeline is created
  in `InitializeOperationalPassResources()` from
  `BuildSelection{Face,Edge,Point}IdPipelineDesc()` and republished
  byte-identical through `RebuildOperationalResources()` using the same
  reset-then-publish pattern as the EntityId / forward / shadow / deferred
  pipelines. The three pipelines share the EntityId pipeline's
  render-pass-compatible shape (two R32_UINT color targets `EntityId` +
  `PrimitiveId`, `D32_FLOAT` depth target, depth-test on, depth-equal,
  depth-write off, color blend off on both targets) so all four selection
  pipelines can be bound inside the same recipe-declared `PickingPass`
  render pass; they differ only in primitive topology (`TriangleList` for
  Face, `LineList` for Edge, `PointList` for Point) and cull mode (`Back`
  for Face — matching the surface bucket — `None` for Edge / Point,
  mirroring the forward line / point pipelines). The shader pairs are
  `shaders/selection/{face,edge,point}_id.{vert,frag}.spv`: each vertex
  stage reuses the GpuScene-aware fetch chain (matches the
  `RHI::GpuScenePushConstants` push block byte-for-byte) and forwards the
  per-instance `inst.EntityID` as a flat varying; each fragment writes
  `EncodeSelectionId(domain, gl_PrimitiveID)` into `PrimitiveId` per
  `GRAPHICS-012Q` where `domain` is `Face`, `Edge`, or `Point` and
  `gl_PrimitiveID` is the per-draw-call primitive index over the
  respective `SurfaceOpaque` / `Lines` / `Points` cull bucket; the
  per-instance stable entity ID is still written into `EntityId`. The
  legacy `assets/shaders/pick_mesh.{vert,frag}` /
  `assets/shaders/pick_line.{vert,frag}` /
  `assets/shaders/pick_point.{vert,frag}` shaders declare the pre-GpuScene
  push block and are deliberately *not* referenced — see the "Shader
  push-constant compatibility policy" subsection above. The executor's
  `"PickingPass"` branch now routes through
  `RecordSelectionEntityIdPass(...)` then
  `RecordSelectionFaceIdPass(...)` then
  `RecordSelectionEdgeIdPass(...)` then
  `RecordSelectionPointIdPass(...)` with the standard
  `SkippedNonOperational` / `SkippedUnavailable` / `Recorded` taxonomy
  accumulated per sub-pass. With depth-equal / depth-write-off, only the
  nearest-surface fragment that survives the prepass depth test can
  write, so the most refined domain code (Face / Edge / Point) wins per
  pixel over the EntityId fallback. The
  `Picking.Readback` buffer + drain +
  `PublishPickResult`/`PublishNoHit` wiring (Slice D) remain.
- GRAPHICS-074 Slice C wires the default-recipe `"SelectionOutlinePass"`
  executor branch. `NullRenderer` owns `m_SelectionOutlinePass`
  (constructed against `m_SelectionSystem`, emplaced *before*
  `InitializeOperationalPassResources()` runs so the publisher's
  `SetPipeline(...)` actually lands on the pass on the initial operational
  path) and `m_SelectionOutlinePipelineLease`. The pipeline is created in
  `InitializeOperationalPassResources()` from
  `BuildSelectionOutlinePipelineDesc(m_BackbufferFormat)` and republished
  byte-identical through `RebuildOperationalResources()` using the same
  reset-then-publish pattern as the four selection-ID pipelines. The
  pipeline pairs the fullscreen `shaders/post_fullscreen.vert.spv` with
  `shaders/selection_outline.frag.spv` and writes a single color target
  whose format matches the recipe's `SelectionOutline` texture
  (`FrameRecipeSizing::BackbufferFormat`); depth state stays off (the
  shader does not test or write depth) but the descriptor declares the
  matching `D32_FLOAT` depth target so the pipeline remains
  render-pass-compatible with the recipe's `Read(SceneDepth, DepthRead)`
  declaration on `"SelectionOutlinePass"`. `PushConstantSize = 144`
  matches `SelectionOutlinePushConstants` exported from
  `Passes/Pass.Selection.Outline.cppm`, which mirrors the
  `selection_outline.frag` `layout(push_constant) uniform Push { ... }`
  block byte-for-byte under Vulkan std430 (vec4 OutlineColor + vec4
  HoverColor + 12 floats/uints + uint[16] SelectedIds). The executor's
  `"SelectionOutlinePass"` branch routes through
  `RecordSelectionOutlinePass(...)` with the standard
  `SkippedNonOperational` / `SkippedUnavailable` / `Recorded` taxonomy,
  and is reached only when `features.EnableSelectionOutline` is true
  (`world.Selection.HasHovered ||
  !world.Selection.SelectedStableIds.empty()`). Slice D.4 lifts the
  push payload from `RenderWorld::Selection` via
  `BuildSelectionOutlinePushConstants(...)`: the helper truncates
  `SelectedStableIds` to `kSelectionOutlineMaxSelectedIds = 16`,
  gates `HoveredId` on `HasHovered`, and forwards the outline visual
  style fields (`OutlineColor`/`HoverColor`/`OutlineWidth`/
  `OutlineMode`/fill alphas/pulse/glow) extended on `SelectionSnapshot`
  with the legacy `Graphics::Passes::SelectionOutlineSettings`
  defaults — so a default-snapshot frame with at least one
  hover/selection still renders a visible orange outline rather than
  the Slice C transparent-black no-op overlay. Portability note: 144
  bytes exceeds the Vulkan-guaranteed minimum `maxPushConstantsSize`
  of 128 — reducing the block (e.g. moving `SelectedIds[16]` into a
  UBO or bindless buffer) is the tracked portability follow-up.
- GRAPHICS-081 retired the bootstrap-only recipe scaffold introduced by
  GRAPHICS-032/033. The renderer now owns only the canonical default recipe
  path: `BuildDefaultFrameRecipe(...)`, the default pass modules, and the
  default-recipe diagnostics/readback counters.
- GRAPHICS-076 Slice A wires the canonical default-recipe `Pass.Present`
  (`Extrinsic.Graphics.Pass.Present`) operationally on the CPU/null path.
  The renderer holds `m_PresentPass` (a default-constructed `PresentPass`)
  plus `m_PresentPipelineLease`, and
  `InitializeOperationalPassResources()` creates the present pipeline
  from `BuildPresentPipelineDesc(m_BackbufferFormat)` — a fullscreen
  pipeline pointed at the new `assets/shaders/present.{vert,frag}` pair
  with `PushConstantSize = 0`, `Rasterizer.Culling = None`, depth test +
  write disabled, and `ColorTargetFormats[0]` pinned to the backbuffer
  format. The present pipeline is created after postprocess inside the
  publisher (call #23 per
  `tests/contract/graphics/Test.RendererFrameLifecycle.cpp`), placed
  after the postprocess histogram pipeline so the existing test
  fixtures' `FailPipelineCreateCall` indices (1-22) remain explicit. On
  the executor side, the `"Present"` branch routes through
  `RecordPresentPass(...)` with the same `Recorded` /
  `SkippedNonOperational` / `SkippedUnavailable` taxonomy the other
  default-recipe pass helpers already use; no new per-pass counter is
  introduced. The recipe-side wiring declares
  `Write(backbuffer, ColorAttachmentWrite)` + `SetRenderPass(...)` on
  the canonical `"Present"` node so the framegraph compiler emits a real
  `CompiledRenderPassAttachment` entry for the backbuffer,
  `BuildActiveRenderPassDesc` reports `HasAttachments = true`, and the
  executor wraps `BindPipeline + Draw(3, 1, 0, 0)` in a
  `BeginRenderPass/EndRenderPass` scope so the draw is well-formed on
  Vulkan. The post-pass `ColorAttachmentWrite → Present` transition is
  emitted by the compiler from the imported backbuffer's
  `FinalState = Present` contract (see `RenderGraph::ImportBackbuffer`),
  so the recipe does not need a separate `TextureUsage::Present` read
  on this pass. The Slice A contract pin is
  `tests/contract/graphics/Test.PresentPass.cpp` (BindPipeline + Draw(3,
  1, 0, 0) shape, missing-pipeline-lease `SkippedUnavailable`,
  non-operational-device `SkippedNonOperational`); the lifecycle test
  picks up the global "Present is Recorded under the default recipe"
  invariant + the `BindPipelineCalls += 1` count bump.
- GRAPHICS-076 Slice B wires the canonical default-recipe
  `Pass.DebugView` (`Extrinsic.Graphics.Pass.DebugView`) operationally
  on the CPU/null path. The renderer holds an `std::optional<DebugViewSystem>`
  (initialized in `Initialize(device)`) plus an
  `std::optional<DebugViewPass>` (constructed with the
  system reference) and `m_DebugViewPipelineLease`. Each frame,
  `ExecuteFrame()` drives
  `DebugViewSystem::SetSettings({.Enabled = world.DebugOverlayEnabled ||
  world.DebugPrimitives.HasTransientDebug, ...})` and
  `ResolveSelection(recipeIntrospection)` immediately after the recipe
  introspection is built, mirroring the recipe-side
  `features.EnableDebugView` gate so the resolved selection's
  `Enabled` flag aligns with what the recipe declares. The DebugView
  pipeline lives in `BuildDebugViewPipelineDesc()`: it targets the new
  canonical `assets/shaders/debug_view.{vert,frag}` pair (push-constant
  block aligned to the 16-byte `DebugViewPushConstants` packing per
  GRAPHICS-013BQ §"Shader visualization modes"), pins
  `ColorTargetFormats[0] = RGBA8_UNORM` (the recipe's `DebugViewRGBA`
  attachment format), and is created after present inside the operational
  publisher (call #24, immediately after present at #23) so the
  existing `FailPipelineCreateCall` indices (1-23) used by other
  lifecycle / present tests remain stable. On the executor side, the
  new `"DebugViewPass"` branch routes through
  `RecordDebugViewPass(graphicsContext, camera)` with the
  `Recorded` / `SkippedNonOperational` / `SkippedUnavailable` taxonomy
  the other default-recipe helpers use. Two new diagnostics surface on
  `RenderGraphFrameStats`: `DebugViewPassExecutions` increments per
  frame the pass records, and `DebugViewFallbackInvocationCount`
  increments when the runtime-requested resource resolved through the
  `DebugViewSystem` fallback path (a non-previewable resource, missing
  resource, or disabled resource — the canonical default
  `"FrameRecipe.PresentSource"` sentinel does not increment the
  counter because it is the "show present source" path, not a
  fallback). `IRenderer::SetDebugViewRequestedResourceName(...)` and
  the matching getter are the public seam runtime / editor use to
  drive the requested resource (the `Enabled` field is driven
  per-frame from the world). On promoted Vulkan, the executor publishes
  the selected debug-view texture through the slot-explicit
  `ICommandContext::BindFrameSampledTextureAt(..., 1)` hook, while the
  canonical `Present` pass publishes `FrameRecipe.PresentSource` at
  descriptor slot 2. The shader pair samples those reserved slots so the
  two fullscreen passes do not overwrite the same global sampled descriptor
  element before a single command-buffer submit; older postprocess bridges
  continue to use slot 0. The Slice B contract pin is
  `tests/contract/graphics/Test.DebugViewPass.cpp` (BindPipeline +
  PushConstants(16) + Draw(3, 1, 0, 0) routing under the default
  recipe; missing pipeline lease `SkippedUnavailable` at call #24;
  non-operational-device `SkippedNonOperational`; invalid-resource
  fallback diagnostic counter increments without silent failure; the
  default `"FrameRecipe.PresentSource"` request does not increment the
  counter; default world omits `"DebugViewPass"` from the recipe
  entirely). The renderer-internal `Pass.DebugView::Execute` contract
  is verified by the existing `Test.DebugViewContract.cpp`.
- GRAPHICS-077 Slice A scaffolds the default-recipe
  `TransientDebugSurfacePass` (`Extrinsic.Graphics.Pass.TransientDebug.Surface`)
  recipe + executor shape on the CPU/null path. Recipe-side,
  `FrameRecipeFeatures::EnableTransientDebugSurface` is derived from
  `!world.DebugPrimitives.Lines.empty() ||
  !world.DebugPrimitives.Points.empty() ||
  !world.DebugPrimitives.Triangles.empty()` so the pass is omitted
  entirely from `CommandRecords` on frames with no transient debug
  payload. When enabled, `DescribeDefaultFrameRecipe` declares the pass
  between the lit-composition family (`SurfacePass` /
  `CompositionPass` / `LinePass` / `PointPass`) and the postprocess
  chain with `Reads = {SceneColorHDR, SceneDepth}` and
  `Writes = {SceneColorHDR}`; `BuildDefaultFrameRecipe` adds the
  ordered pass with `Read(SceneDepth, DepthRead) + Write(SceneColorHDR,
  ColorAttachmentWrite) + SetRenderPass(LOAD-store color, depth-load
  /dontcare-store)` so the framegraph compiler emits a real
  `CompiledRenderPassAttachment` pair (mirrors the canonical
  `Pass.Present` wiring rationale — without `SetRenderPass` the future
  bind/draw would land outside a render-pass scope, invalid on Vulkan).
  Renderer-side, `NullRenderer` owns a plain `m_TransientDebugSurfacePass`
  member (no system dependency) and a new `"TransientDebugSurfacePass"`
  executor branch routes through `RecordTransientDebugSurfacePass(...)`
  with the `SkippedNonOperational` / `SkippedUnavailable` taxonomy used
  by the other default-recipe helpers. The new
  `TransientDebugUploadDiagnostics` struct lives on
  `RenderGraphFrameStats::TransientDebugUpload` and exposes eight
  counters (`UploadOverflowCount`, `{Line,Point,Triangle}RecordsSubmitted`,
  `{Line,Point,Triangle}RecordsRecorded`, `MissingPipelineSkipCount`).
  Slice A pinned all counters at zero except `MissingPipelineSkipCount`,
  which incremented by one each frame the executor reached the branch
  with an operational device but no pipeline (the scaffold-only
  signal that distinguished "feature on" from "feature off").
  GRAPHICS-077 Slice B promotes the triangle lane from
  `SkippedUnavailable` to `Recorded`. `NullRenderer` now owns two
  triangle pipeline leases (`m_TransientDebugTrianglePipelineLeaseDepthTested`
  / `m_TransientDebugTrianglePipelineLeaseAlwaysOnTop`), created via
  `BuildTransientDebugTrianglePipelineDesc(depthTested)` at call indices
  #25 + #26 inside `InitializeOperationalPassResources()` (immediately
  after the GRAPHICS-076 debug-view pipeline at #24 so upstream
  `FailPipelineCreateCall` indices stay stable). Both pipelines bind
  `assets/shaders/transient_debug_triangle.{vert,frag}`, target the
  `RGBA16_FLOAT` `SceneColorHDR` color attachment with depth attachment
  `D32_FLOAT`, and carry the 16-byte
  `TransientDebugTrianglePushConstants` push block (BDA + per-draw
  `FirstVertex`). The renderer also owns a backend-local
  `TransientDebugUploadHelper` (declared in
  `Extrinsic.Graphics.TransientDebugUploadHelper`) that leases a single
  growing host-visible vertex buffer through `BufferManager` and packs
  per-frame triangle vertices via `IDevice::WriteBuffer(...)`; the
  helper is reset before the `BufferManager` in `Shutdown()` so its
  internal `BufferLease` destructor observes a live manager.
  `RecordTransientDebugSurfacePass(...)` now consumes
  `world.DebugPrimitives.Triangles`, gates on both triangle pipeline
  leases, and routes through `TransientDebugSurfacePass::ExecuteTriangles(...)`
  which records `BindPipeline(variant) + PushConstants(16) + Draw(3, 1,
  0, 0)` per packet (switching the bound variant whenever the packet's
  `DepthTested` flag flips). Per-packet recording increments
  `TriangleRecordsSubmitted` + `TriangleRecordsRecorded`;
  `UploadOverflowCount` only ticks if the helper exceeds its
  per-frame vertex-count cap (256 K vertices ≈ 4 MiB).
  `MissingPipelineSkipCount` continues to increment on the
  operational-no-pipeline path (e.g. `FailPipelineCreateCall = 25`).
  GRAPHICS-077 Slice C extends the helper + pass to the line + point
  lanes. `NullRenderer` now owns four additional pipeline leases
  (`m_TransientDebugLinePipelineLeaseDepthTested` /
  `m_TransientDebugLinePipelineLeaseAlwaysOnTop` /
  `m_TransientDebugPointPipelineLeaseDepthTested` /
  `m_TransientDebugPointPipelineLeaseAlwaysOnTop`), created via
  `BuildTransientDebugLinePipelineDesc(depthTested)` and
  `BuildTransientDebugPointPipelineDesc(depthTested)` at call indices
  #27-#30 (immediately after the Slice B triangle pipelines at
  #25+#26 so the Slice B contract tests pinning
  `FailPipelineCreateCall = 25` still exercise the triangle
  DepthTested gate without disturbance). The line lane uses
  `Topology = LineList` with the shader pair
  `assets/shaders/transient_debug_line.{vert,frag}`, draws
  `Draw(2, 1, 0, 0)` per packet, and increments
  `LineRecordsSubmitted` + `LineRecordsRecorded`. The point lane
  uses `Topology = PointList` with the shader pair
  `assets/shaders/transient_debug_point.{vert,frag}`, draws
  `Draw(1, 1, 0, 0)` per packet, and increments
  `PointRecordsSubmitted` + `PointRecordsRecorded`. Both lanes share
  the same `position(vec3) + packed RGBA8 color(uint32)` 16-byte
  packed-vertex layout as the triangle lane and carry the same
  16-byte push block (BDA + per-draw `FirstVertex`) — type-distinct
  as `TransientDebugLinePushConstants` /
  `TransientDebugPointPushConstants` to keep room for per-lane push
  evolution (e.g. line width, point radius) in a follow-up task.
  `TransientDebugUploadHelper` grows three independent host-visible
  vertex buffers (one per lane), each capped at 256 K vertices, with
  geometric doubling under load. `RecordTransientDebugSurfacePass`
  now gates each lane independently: a lane with packets but a
  missing or invalid pipeline pair increments
  `MissingPipelineSkipCount` and is skipped, while sibling lanes
  with valid pipelines still record. The pass status is `Recorded`
  when at least one lane recorded, and `SkippedUnavailable` when
  every submitted lane failed its pipeline gate. Width / radius
  expansion (thick lines, disc points) is deferred — the
  CPUContracted form pins the bind/push/draw shape only; Slice D
  (opt-in `gpu;vulkan` pixel-readback smoke) remains deferred behind
  the same Vulkan-capable-host gate as GRAPHICS-076 Slice D. The
  contract pin is `tests/contract/graphics/Test.TransientDebugSurfacePass.cpp`
  (recipe declaration with/without transient primitives, executor
  `SkippedNonOperational` short-circuit, executor
  `SkippedUnavailable` with `MissingPipelineSkipCount` increment on
  pipeline-create failure per lane, the operational `Recorded`
  taxonomy with per-lane counters, per-packet variant selection,
  per-frame buffer-recycling invariant across multiple frames, a
  mixed-lane all-three-record acceptance test, and a per-lane
  partial-skip test pinning the gate's per-lane independence).
- GRAPHICS-078 Slice A scaffolds the default-recipe
  `VisualizationOverlayPass` (`Extrinsic.Graphics.Pass.VisualizationOverlay`)
  recipe + executor shape on the CPU/null path. Recipe-side,
  `FrameRecipeFeatures::EnableVisualizationOverlay` is derived from
  `!world.Visualization.VectorFields.empty() ||
  !world.Visualization.Isolines.empty()` so the pass is omitted
  entirely from `CommandRecords` on frames with no overlay payload.
  When enabled, `DescribeDefaultFrameRecipe` declares the pass
  immediately after `TransientDebugSurfacePass` (same "post-lit,
  pre-postprocess" band) with `Reads = {SceneColorHDR, SceneDepth}`
  and `Writes = {SceneColorHDR}`; `BuildDefaultFrameRecipe` adds the
  ordered pass with `Read(SceneDepth, DepthRead) + Write(SceneColorHDR,
  ColorAttachmentWrite) + SetRenderPass(LOAD-store color, LOAD/Store
  depth)` so the framegraph compiler emits a real
  `CompiledRenderPassAttachment` pair before any future Slice B/C
  bind/draw lands. Renderer-side, `NullRenderer` owns a plain
  `m_VisualizationOverlayPass` member (no system dependency) and a
  new `"VisualizationOverlayPass"` executor branch routes through
  `RecordVisualizationOverlayPass(...)` with the
  `SkippedNonOperational` / `SkippedUnavailable` taxonomy used by the
  other default-recipe helpers. The new
  `VisualizationOverlayUploadDiagnostics` struct (moved to
  `Extrinsic.Graphics.VisualizationOverlayUploadHelper` in Slice B)
  lives on `RenderGraphFrameStats::VisualizationOverlayUpload` and
  exposes six counters (`UploadOverflowCount`,
  `{VectorField,Isoline}RecordsSubmitted`,
  `{VectorField,Isoline}RecordsRecorded`,
  `MissingPipelineSkipCount`).
- GRAPHICS-078 Slice B promotes the vector-field lane from
  `SkippedUnavailable` to `Recorded` on the CPU/null path.
  `VectorFieldOverlayPacket` grows a `bool DepthTested{true}` field
  mirroring the GRAPHICS-010Q two-variant policy on the transient-
  debug packets. The renderer constructs a
  `VisualizationOverlayUploadHelper` alongside the
  `TransientDebugUploadHelper` (default in-renderer concrete impl
  pairs `RHI::BufferManager` with `IDevice::WriteBuffer(...)` against
  a single growing host-visible vertex buffer per lane, geometric
  growth ×2 up to a per-lane cap of `1 << 18` vertices). Two new
  pipelines land at call indices #31 (vector-field DepthTested) and
  #32 (vector-field AlwaysOnTop), keyed by
  `BuildVisualizationVectorFieldPipelineDesc(depthTested)` against
  the new shader pair
  `assets/shaders/visualization_vector_field.{vert,frag}` (BDA-fetch
  vertex layout matching the transient-debug shaders, 16-byte
  `VisualizationVectorFieldPushConstants` push block carrying
  `VertexBufferBDA + FirstVertex + Reserved`). The pass's new
  `ExecuteVectorFields(...)` body iterates the sanitized packet span
  and records `BindPipeline(variant) + PushConstants(16) +
  Draw(2 * ElementCount, 1, 0, 0)` per packet (one glyph = one line
  segment = two vertices), switching the variant whenever consecutive
  packets disagree on `DepthTested`. Per-lane gating in
  `RecordVisualizationOverlayPass` increments
  `MissingPipelineSkipCount` when the vector-field pipelines are
  missing, increments `VectorFieldRecordsSubmitted/Recorded`
  deterministically per packet, and flips the pass status to
  `Recorded` when at least one packet's draw lands.
  CPU/null contract note: the helper does not have CPU access to
  `PositionBufferBDA` / `VectorBufferBDA` (those are GPU pointers),
  so the CPU/null path writes zero positions and the packet color
  into each packed vertex; per-pixel correctness on a real Vulkan
  device is owned by the optional Slice D `gpu;vulkan` smoke and the
  Vulkan-tuned helper variant that expands actual per-glyph
  endpoints from the source BDAs.
- GRAPHICS-078 Slice C promotes the isoline lane from
  `SkippedUnavailable` to `Recorded` on the CPU/null path.
  `IsolineOverlayPacket` grows a `bool DepthTested{true}` field
  mirroring the GRAPHICS-010Q two-variant policy. The
  `VisualizationOverlayUploadHelper` extends to a second per-lane
  buffer lease (independent geometric growth, same per-lane cap as
  the vector-field lane) and exposes `UploadIsolines(...)`. Two new
  pipelines land at call indices #33 (isoline DepthTested) and #34
  (isoline AlwaysOnTop), keyed by
  `BuildVisualizationIsolinePipelineDesc(depthTested)` against the new
  shader pair `assets/shaders/visualization_isoline.{vert,frag}` (same
  BDA-fetch vertex layout as the vector-field + transient-debug
  shaders, dedicated 16-byte `VisualizationIsolinePushConstants` push
  block). The pass's new `ExecuteIsolines(...)` body iterates the
  sanitized packet span and records `BindPipeline(variant) +
  PushConstants(16) + Draw(2 * IsoValueCount, 1, 0, 0)` per packet
  (each iso value is a `LineList` placeholder segment of two
  vertices on the CPU/null path), switching the variant whenever
  consecutive packets disagree on `DepthTested`. Per-lane gating in
  `RecordVisualizationOverlayPass` flips the placeholder
  `MissingPipelineSkipCount` increment for the isoline lane into the
  same gate-and-record path the vector-field lane uses: a lane with
  packets but a missing pipeline pair increments
  `MissingPipelineSkipCount` and is skipped, while a sibling lane
  with valid pipelines still records. Pass status is `Recorded` when
  at least one lane recorded; `SkippedUnavailable` when every
  submitted lane failed its gate. CPU/null contract note: the helper
  does not have CPU access to the source scalar field (its values +
  topology are GPU-side), so the CPU/null path writes zero positions
  + the packet color into each packed vertex; per-pixel correctness
  on a real Vulkan device is owned by the optional Slice D
  `gpu;vulkan` smoke and the Vulkan-tuned helper variant that
  expands scalar-field-derived contour vertices.
- Slice D adds the opt-in `gpu;vulkan` pixel-readback smoke. The
  contract pin is
  `tests/contract/graphics/Test.VisualizationOverlayPass.cpp`
  (14 tests on CPU/null hosts after Slice C: recipe declaration
  with/without overlay packets, executor `SkippedNonOperational`
  short-circuit, per-lane `Missing*PipelineLeaseSkipsUnavailable`,
  per-packet `Records*` / `Selects*AlwaysOnTop` shape, per-frame
  buffer-recycling invariant per lane, mixed-lane both-record
  acceptance test, and the per-lane partial-skip independence pin).
- GRAPHICS-079 wires the canonical default-recipe `Pass.ImGui`
  (`Extrinsic.Graphics.Pass.ImGui`) executor route on the CPU/null path.
  Slice A added the explicit `"ImGuiPass"` executor branch, the renderer-owned
  `std::optional<ImGuiPass>` consumer, the borrowed
  `ImGuiOverlaySystem* m_ImGuiOverlaySystem`, `IRenderer::SetImGuiOverlaySystem`
  / `HasImGuiOverlaySystem`, and the `m_ImGuiPipelineLease`. Slice B wires the
  runtime `Engine` producer↔consumer handoff so `Engine::Initialize()` passes
  the engine-owned overlay to the renderer and `Engine::Shutdown()` detaches it
  before the adapter shuts the overlay down. Slice C adds the graphics-owned
  retained font atlas and renderer-owned transient upload path:
  `ImGuiOverlaySystem::InitializeGpuResources(device, textureManager,
  samplerManager)` allocates the retained atlas texture (`R8_UNORM` fallback,
  `RGBA8_UNORM` for colored atlas payloads) through `RHI::TextureManager` and
  retains its bindless index; `ImGuiUploadHelper` packs submitted POD
  vertices/indices into one growing host-visible vertex buffer and one growing
  index buffer; `ImGuiPass::Execute(...)` records deterministic
  `BindIndexBuffer + PushConstants + DrawIndexed` blocks and increments
  `ImGuiOverlayDiagnostics::DrawCalls`. Slice D.1 promotes the frame recipe's
  `"ImGuiPass"` node from side-effect-only to a load/store render-pass-scope
  pass that reads and writes the current `FrameRecipe.PresentSource` color
  attachment, so an operational device with an attached uploadable overlay
  frame records under the CPU/null gate. Slice D.2 adds
  `ImGuiOverlayDrawCommand` command metadata, carries direct `ImTextureID`
  values as `RHI::BindlessIndex` slots, pushes the selected texture index per
  command, and samples the retained font atlas or user texture in
  `assets/shaders/imgui.frag` without adding a graphics-visible descriptor
  surface. The imported `Backbuffer` remains owned solely by `Pass.Present`;
  render-graph validation still rejects non-present backbuffer writes. The
  opt-in `gpu;vulkan` smoke remains the GPU-host proof for the same path.
- `TransformSyncSystem`, `LightSystem`, and `VisualizationSyncSystem` consume
  graphics-owned snapshot records (`TransformSyncRecord`, `LightSnapshot`, and
  `VisualizationSyncRecord`) instead of querying live ECS registries. Runtime is
  responsible for building those records from ECS/assets/geometry state.
- `IRenderer::SubmitRuntimeSnapshots()` is the promoted handoff from runtime to
  graphics. The renderer copies snapshot records into frame-local storage before
  `ExtractRenderWorld()`/`PrepareFrame()` consume them; it does not retain ECS
  registry references.
- Renderable asset-generation observation is runtime-owned. Per `GRAPHICS-023C`,
  `Extrinsic.Runtime.RenderExtraction` may compare `AssetInstance::Source` with a
  supplied `Graphics.GpuAssetCache` view and `GpuSceneSlot` metadata, but
  `GpuSceneSlot` itself only exposes value-type comparison helpers and does not
  import `Graphics.GpuAssetCache`, ECS, runtime, live `AssetService`, or backend
  modules. Per `GRAPHICS-023D`, the runtime side advances
  `GpuSceneSlot::LastSeenAssetGeneration` only through the explicit
  `Runtime::AcknowledgeRenderableAssetRebind` helper; the default
  `RenderExtractionCache::ExtractAndSubmit` path observes without
  acknowledging until a later upload/rebind slice actually performs the
  binding work.
- `RenderWorld` exposes immutable spans of renderer-owned `RenderableSnapshot`
  and `LightSnapshot` values, sanitized transient debug line/point/triangle
  packet spans, transform-gizmo render packet spans, `VisualizationSnapshot`
  packet spans/diagnostics, camera/view/frustum snapshots, defaulted optional
  packets for picking, selection, shadows, postprocess/readback, and
  invalid-record diagnostics. These records are valid for the frame and never
  reference live ECS storage.
- `Graphics.CameraSnapshots` is data-only: it validates view/projection
  matrices, extracts frustum planes, and derives pick rays from immutable pixel
  requests. Viewport dimensions use the core-owned `Core::Extent2D` value type
  rather than the live platform window port. Camera motion, input polling, gizmo
  hit testing, and transform mutation remain runtime/platform/editor
  responsibilities. Per
  `GRAPHICS-017Q`, the camera/gizmo runtime follow-ups resolve as
  follows. Concrete camera controllers (orbit, fly, free-look,
  top-down) live as runtime modules under the planned umbrella
  module name `Extrinsic.Runtime.CameraControllers`, mirroring the
  `Extrinsic.Runtime.SpatialDebugAdapters` pattern from
  `GRAPHICS-011Q`, the `Extrinsic.Runtime.VisualizationAdapters`
  pattern from `GRAPHICS-014Q`, and the
  `Extrinsic.Runtime.AssetBridges.Texture` pattern from
  `GRAPHICS-015Q`. Controllers read platform input deltas through
  the existing platform input port, translate them into runtime-owned
  camera state, and runtime extraction fills `CameraViewInput` and
  submits it through `IRenderer::SubmitRuntimeSnapshots()`. Multiple
  cameras (preview, top-down, editor secondary view) are
  runtime-owned and emit one `CameraViewInput` per frame each.
  Pick-request scheduling is runtime-owned and single-shot: each
  input frame's accepted picks are coalesced at runtime by
  `(viewport, pixel, request_kind)` key into the per-frame
  `PickPixelRequest` span on `RenderFrameInput`; the renderer drains
  `Picking.Readback` on the next `BeginFrame()` mirroring the drain
  pattern from `GRAPHICS-012Q`, and there is no graphics-side
  persistent pending-pick queue across frames. Transform-gizmo hit
  testing is runtime/editor-owned under the planned umbrella module
  name `Extrinsic.Runtime.GizmoInteraction`; the hit-test path reads
  selection authoring transforms from runtime ECS/editor state, the
  same `CameraViewSnapshot::ViewProjection`/`PickRay` derivation
  that graphics already produces, and raw pointer pixels from the
  platform input port. Graphics never receives raw pointer
  coordinates and never imports any gizmo hit-test code path. The
  `TransformGizmoRenderPacket` spans on `RenderWorld` carry only
  render-relevant data — world-space origin, camera-relative scale,
  active mode (translate / rotate / scale), highlighted axis or
  plane mask, and per-handle render flags — while drag state, axis
  lock, drag origin, snap thresholds, modifier-key state,
  multi-select pivot policy, and orientation reference frame stay
  runtime-side. Interaction state is runtime/editor-owned (either
  editor-side singleton or ECS component) and never enters graphics;
  the gizmo render packets are carried only until the next
  `BeginFrame()` clears `RenderWorld`, mirroring the existing
  transient debug primitive lifetime from
  `GRAPHICS-002`/`GRAPHICS-010Q`. Transform application is
  runtime/editor-owned: drag-tick writes update authoring transforms
  in runtime ECS / asset / prefab storage; drag-commit pushes a
  single undoable `(entity, before, after)` command onto the editor
  undo stack. Undo / redo lives entirely in the editor and graphics
  never mutates ECS, asset, or prefab state. Legacy
  `Graphics.TransformGizmo` and `Graphics.Interaction` features
  (orientation modes, snap modes, multi-select pivot policy,
  modifier-key behavior, numeric-input commit, per-axis constraint
  locks) are enumerated by the editor-handoff rows in
  `../../../docs/migration/nonlegacy-parity-matrix.md` that
  cross-link `GRAPHICS-017Q`; concrete promoted-implementation task
  IDs are deliberately not allocated by this clarification because
  the matrix already cross-links them and `GRAPHICS-020` (legacy
  graphics retirement gates) is the gating task that consumes the
  matrix.
- Transient debug packets are frame-local runtime submissions, not persistent
  editor overlay entities. The renderer rejects non-finite coordinates/colors,
  clamps line widths to `[0.5, 32]`, clamps point radii to `[0.0001, 1]`, and
  reports rejected records through `InvalidSnapshotRecordCount`. Concrete
  backend expansion of `DebugLinePacket`/`DebugPointPacket`/`DebugTrianglePacket`
  spans goes through dedicated per-frame host-visible (transient) GPU buffers
  owned by a future backend upload helper, never through `GpuWorld` or the
  canonical `CullingPass` buckets. Debug triangles are routed through a
  dedicated debug-surface overlay drawn into `SceneColorHDR`/`SceneDepth`
  after the lit composition (next to `Pass.Forward.Line` / `Pass.Forward.Point`),
  not through `Pass.Forward.Surface` or `Pass.Deferred.GBuffers`. The per-packet
  `DepthTested` field is expressed as two pipeline variants per primitive lane,
  not as separate cull buckets or frame-graph resources, and
  `InvalidSnapshotRecordCount` remains the only CPU diagnostic surface for
  malformed transient records (mirroring the `GRAPHICS-007Q`/`GRAPHICS-008Q`
  diagnostics stance). Concrete upload-helper sizing, the
  `TransientDebugUploadDiagnostics` field, and the numbered pipeline-order step
  for the debug-surface overlay are tracked under `GRAPHICS-018` Vulkan
  integration scope.
- `Graphics.SpatialDebugVisualizers` is a CPU-only packet builder layer for
  spatial debug views. It consumes data-only bounds, hierarchy-node,
  split-plane, convex-hull edge, and point-marker snapshots and produces owned
  transient debug packet vectors plus deterministic diagnostics. It does not
  import geometry tree implementations, runtime, editor UI, or ECS ownership;
  higher layers/adapters translate their structures into these snapshot
  records. Per `GRAPHICS-011Q`, concrete BVH/KD-tree/octree/convex-hull
  adapters live in **runtime extraction** (planned umbrella module name
  `Extrinsic.Runtime.SpatialDebugAdapters`) — not in `src/geometry` and not
  in `src/graphics` — because runtime is the only layer permitted to import
  both geometry tree implementations and the graphics packet types. Adapters
  may apply CPU-side pre-filters (leaf-only, occupancy-only, capped depth) and
  surface adapter-side statistics through `RuntimeRenderExtractionStats`; the
  graphics-side `SpatialDebugVisualizerOptions` budget and
  `SpatialDebugVisualizerDiagnostics` remain the single graphics-visible
  truncation/diagnostics surfaces, and the input record types are frozen by
  the same clarification (no new fields for adapter-specific knowledge).
  Adapter integration tests land under `tests/integration/runtime/` next to
  `Test.RuntimeRenderExtraction.cpp`; the data-only packet contract keeps
  its unit coverage in `tests/unit/graphics/Test.Graphics.SpatialDebugVisualizers.cpp`.
- `Graphics.VisualizationPackets` is a CPU-only packet contract for scalar,
  color, vector-field, isoline, UV-backed fragment-bake, and Htex-backed
  visualization data. Existing mesh texcoords may drive per-fragment bakes;
  Htex can still be recreated and selected for any mesh. The packet contract
  validates domains, ranges, colormap IDs, BDA/resource seams, missing texcoords,
  and Htex atlas descriptors while leaving texture residency and geometry
  algorithm generation to later graphics-assets/runtime/geometry owners.
  Per `GRAPHICS-014Q`, runtime extraction (`Extrinsic.Runtime.RenderExtraction`)
  is the sole owner of translating PropertySet attributes, KMeans labels,
  isoline results, vector fields, and Htex metadata into the
  `RuntimeRenderSnapshotBatch` visualization packet spans
  (`VisualizationAttributeBuffers`, `VisualizationScalars`,
  `VisualizationColors`, `VisualizationVectorFields`, `VisualizationIsolines`,
  `VisualizationHtexAtlases`, `VisualizationFragmentBakeAtlases`); concrete
  producer adapters live under a planned `Extrinsic.Runtime.VisualizationAdapters`
  umbrella, mirroring the `Extrinsic.Runtime.SpatialDebugAdapters` pattern from
  `GRAPHICS-011Q`. Editor/app code provides user-facing surfaces (selected
  attribute, colormap, scalar range, isoline parameters, vector-field
  scale/color, Htex regeneration request) and funnels them into the runtime
  adapter as pre-filter inputs; graphics never imports geometry algorithm
  modules or live ECS ownership. Runtime/extraction performs no packet
  filtering — every authored packet flows through
  `IRenderer::SubmitRuntimeSnapshots()`; validation is centralized in
  `ValidateVisualizationPackets(...)` invoked by the renderer at snapshot
  extraction time, rejected records are dropped from the consumed
  `RenderWorld::Visualization` snapshot and counted in
  `VisualizationDiagnostics`, and future backend upload stages do not
  re-validate (mirroring the `InvalidSnapshotRecordCount` drain pattern from
  `GRAPHICS-002`/`GRAPHICS-010Q`). Vector-field glyphs and isoline polylines
  are NOT routed through retained `GpuRender_Line`/`GpuRender_Point` cull
  buckets and are NOT GPU-scene renderable instances; they are auxiliary draw
  resources owned by a backend-local upload helper under `src/graphics/vulkan`
  mirroring the transient debug expansion from `GRAPHICS-007Q`/`GRAPHICS-010Q`
  (per-frame host-visible
  transient GPU buffers recycled each frame, never retained on `GpuWorld`,
  never exposed through RHI or renderer module surfaces). The backend-local
  helper expands `VectorFieldOverlayPacket`/`IsolineOverlayPacket` into
  per-frame transient vertex/index buffers consumed by dedicated
  visualization-overlay passes that LOAD `SceneColorHDR`/`SceneDepth` next to
  `Pass.Forward.Line`/`Pass.Forward.Point`, expressing depth-tested vs
  always-on-top behavior as the same two-pipeline-variant policy resolved for
  transient debug primitives in `GRAPHICS-010Q`. Concrete pipeline binding,
  pipeline-order placement, and a future `VisualizationOverlayUploadDiagnostics`
  field analogous to `TransientDebugUploadDiagnostics` are tracked under
  `GRAPHICS-018` Vulkan integration scope. Auxiliary GPU resources referenced
  through packet BDAs and Htex/UV bake atlas textures are uploaded by the
  existing `Graphics.GpuAssetCache`/`RHI::BufferManager`/`RHI::TextureManager`
  paths once `GRAPHICS-015` texture/buffer residency lands; until then the
  CPU/null contract validates packet metadata only and
  `VisualizationDiagnostics::TextureResidencyDeferredCount` reports atlas
  descriptors whose texture residency is intentionally deferred. Bake mapping
  selection is runtime/editor-owned: the editor UI maps directly to
  `VisualizationFragmentBakeMapping` (`ExistingTexcoords`/`ExistingHtex`/
  `RecreateHtex`), and `RecreateHtex` is an explicit user-driven request
  scheduled by runtime/geometry on a background task through
  `Extrinsic.Runtime.StreamingExecutor` (async visualization baking remains
  CPU/runtime-only). Graphics increments
  `VisualizationDiagnostics::HtexRecreateRequestCount` and accepts the
  descriptor without owning the Htex regeneration algorithm; once regeneration
  completes the next extraction frame submits the `FragmentBakeAtlasPacket`
  with `Mapping = ExistingHtex`. UV-backed bakes require
  `MeshHasTexcoords = true` and a non-zero `TexcoordBufferBDA`; missing
  texcoords are rejected from the snapshot and counted in
  `MissingTexcoordCount`.
- `Graphics.ColormapSystem` owns the retained 256-sample RGBA8 LUT textures for
  built-in scalar colormaps. Initialization creates the sampler/textures through
  `RHI::SamplerManager`/`RHI::TextureManager`, submits LUT bytes through
  `IDevice::GetTransferQueue().UploadTexture()`, and records the returned
  `TransferToken` values. `IsReady()` is the CPU-visible first-frame readiness
  guard; `GetBindlessIndex()` returns `kInvalidBindlessIndex` until every LUT
  transfer token is valid and complete, so colormap-dependent draws can skip
  deterministically instead of sampling an in-flight upload. The synchronous
  `IDevice::WriteTexture()` helper remains only as the guarded backend
  fail-closed baseline, not a renderer/runtime upload path.
- `Graphics.FrameRecipe` imports explicit cull bucket resources for surface,
  line, and point lanes. `LinePass` consumes `Cull.Lines.IndexedArgs` /
  `Cull.Lines.Count`; `PointPass` consumes `Cull.Points.NonIndexedArgs` /
  `Cull.Points.Count`. These cull-bucket resources stay reserved for retained
  `GpuRender_Line`/`GpuRender_Point` renderables and are not the transient
  debug expansion path.
- `Graphics.GpuAssetCache` and `MaterialSystem` own the texture residency
  contract. Per `GRAPHICS-015Q`, the cache stays explicitly non-evicting in
  the `GRAPHICS-015` slice; capacity introspection comes from
  `GpuAssetCacheDiagnostics` (`TrackedAssets`, `PendingRetireRecords`,
  `NonEvictingCache = true`), and bounded eviction is a separate semantic
  task that must extend the diagnostics, route evicted leases through the
  same frame-anchored retire queue (`retireDeadline = currentFrame +
  framesInFlight`), refuse to evict the fallback texture lease, and prefer
  a priority + LRU pair over pure LRU. Streaming mip / reupload uses
  `RHI::TextureManager::Reupload()` to preserve the lease's existing
  `RHI::TextureHandle`, bindless index, and sampler binding for partial-mip
  updates whose destination `TextureDesc` is unchanged; full
  `RequestUpload(GpuTextureRequest)` is reserved for format / extent /
  mip-count / usage changes and hot-reload swaps. A future
  `RequestStreamingReupload(AssetId, MipRange, std::span<const std::byte>)`
  seam will validate the lease is `Ready`, forward to
  `TextureManager::Reupload()`, and increment a `StreamingMipUploads`
  counter on `GpuAssetCacheDiagnostics`. A single deterministic 4x4
  magenta-and-black checkerboard fallback texture (RGBA8_UNORM, alpha
  0xFF, nearest filter, clamp-to-edge) covers every sampled
  `MaterialParams` texture slot (`Albedo`/`Normal`/`MetallicRoughness`/
  `Emissive`); per-channel "neutral" interpretation is enforced by
  material shader code observing the resolved `UsedFallback` bit, not by
  allocating per-slot fallback textures (`Normal` -> flat `(0.5, 0.5, 1.0)`
  tangent normal, `MetallicRoughness` -> `MetallicFactor`/`RoughnessFactor`
  scalars treated as `metallic = 0`, `roughness = 1` when factors are
  absent, `Emissive` -> per-material `EmissiveFactor` defaulting to
  `0.0` so unbound emissive assets do not silently glow). Visualization
  and Htex/UV bake atlas references do not use the magenta fallback: per
  `GRAPHICS-014Q`, deferred-residency atlas descriptors are dropped from
  `RenderWorld::Visualization` and counted in
  `VisualizationDiagnostics::TextureResidencyDeferredCount`. Bindless
  texture descriptor writes are coalesced per frame: the backend records
  all bindless slot writes during the frame's `IRenderer::PrepareFrame`/
  `Record` window and drains them as a single descriptor batch at the
  start of the next frame's `BeginFrame()`, mirroring the
  `Picking.Readback` drain from `GRAPHICS-012Q` and the histogram
  readback drain from `GRAPHICS-013AQ`. Sampler creation is deduplicated
  through `RHI::SamplerManager`; `SamplerDesc` changes on the next
  `RequestUpload` trigger a coalesced bindless rewrite of the lease's
  descriptor in the same per-frame batch and increment a
  `BindlessDescriptorRewrites` counter on `GpuAssetCacheDiagnostics`.
  `MaterialSystem::ResolveTextureAssetBindings()` writes resolved
  `BindlessIndex` values into `MaterialParams` without forcing a
  separate descriptor flush because bindless indices are retained-stable
  per lease, and stale-bindless hazards on hot reload are prevented by
  the existing frame-anchored retire queue holding the descriptor live
  for `framesInFlight` frames after retirement. Concrete `VkDescriptorSet`
  layout and heap write batching remain backend-local under
  `src/graphics/vulkan`. Runtime owns both fallback initialization (a
  runtime-side graphics-bootstrap step calls
  `cache.InitializeFallbackTexture(fallbackDesc)` exactly once with
  fallback bytes from a baked engine resource owned by the runtime
  layer; the cache never reads files) and upload scheduling
  (texture-typed asset bridges under the planned umbrella
  `Extrinsic.Runtime.AssetBridges.Texture` subscribe to texture-typed
  `AssetEvent::Ready`, build `GpuTextureRequest`, and call
  `cache.RequestUpload(req)` synchronously; heavy CPU decoding may be
  queued through `Extrinsic.Runtime.StreamingExecutor`; graphics never
  imports `AssetService`/`AssetEventBus` and never schedules CPU work).
  If `InitializeFallbackTexture()` fails, `FallbackTextureReady = false`
  and `GetViewOrFallback()` returns `GpuAssetFallbackReason::Unavailable`,
  letting material code fall back to factor-only shading deterministically.
- Per `GRAPHICS-018Q`, the four remaining Vulkan integration follow-ups
  to the `GRAPHICS-018` guarded backend bring-up resolve as follows.
  Texture upload policy keeps the guarded synchronous staging-buffer
  one-subresource `WriteTexture()` path as the fail-closed correctness
  baseline; runtime/streaming uploads must use `RHI::ITransferQueue`
  (the canonical seam declared by `GRAPHICS-026`) rather than the
  blocking graphics-queue helper, per-subresource layout tracking
  stays whole-image until multi-subresource batching lands, and
  multi-mip / multi-layer / cubemap batching plus opt-in `gpu;vulkan`
  smoke is owned by
  `tasks/active/GRAPHICS-018T-texture-upload-batching.md`,
  not by this clarification. Sampler anisotropy stays expressed
  through the existing `RHI::SamplerDesc::MaxAnisotropy` float; the
  Vulkan backend probes `VkPhysicalDeviceFeatures::samplerAnisotropy`
  during physical-device selection alongside the existing required
  Vulkan 1.2 / 1.3 features, enables it on the logical device when
  supported, records support / enablement on
  `GetVulkanBootstrapDiagnosticsSnapshot()`, and at sampler creation
  silently disables anisotropy when the feature is unsupported or
  `MaxAnisotropy <= 1.0`, otherwise clamps to
  `min(MaxAnisotropy, VkPhysicalDeviceLimits::maxSamplerAnisotropy)`
  with one warn breadcrumb when clamping reduces the value. Missing
  support never fails sampler creation and no new RHI-visible enum
  or cap is added. Fallback reason taxonomy keeps each fail-closed
  counter and its reason enum 1:1 to its path: future device-loss /
  extension / feature-negotiation reasons in the pipeline path are
  appended to the existing `FallbackPipelineReason` enum, and any
  second reason in another counter introduces a *new* path-local
  `FallbackXxxReason` enum named after that counter with a matching
  `LastXxxReason` field appended to `FallbackDiagnosticsSnapshot`
  after the existing eight fields. Counters stay process-monotonic
  across `Initialize`/`Shutdown` cycles independent of any reason
  field. Per-call warn breadcrumbs on bindless / transfer-queue /
  pipeline-creation fallback paths remain canonical for now (those
  callsites fire infrequently while non-operational and the
  visibility helps catch accidental loops before bring-up);
  frame-loop counters keep the existing once-per-fail-closed-cycle
  rate-limited breadcrumb policy already locked in by 018, with
  `FallbackDiagnosticsSnapshot` carrying the precise diagnostic
  regardless of breadcrumb suppression, and `Resize` stays
  unrate-limited. Migration of any per-call counter to once-per-frame
  rate-limited breadcrumbs (with a cumulative-skipped count appended)
  is a separate semantic task scoped to that counter only when
  operational bring-up demonstrates many-per-frame fallback firing.
  This clarification adds no new graphics fields, no new RHI enums,
  and no new graphics acceptance criteria.
- `PostProcessSystem` owns the backend-agnostic HDR-to-LDR chain settings,
  deterministic stage description, sanitized diagnostics, and push-constant
  packet data for `Histogram`, `Bloom`, `ToneMap`, `FXAA`, and `SMAA`. The
  `ToneMap` leaf is operationally wired under `GRAPHICS-075` Slice A: the
  `NullRenderer` owns `m_PostProcessToneMapPass` +
  `m_PostProcessToneMapPipelineLease`, the tonemap pipeline (vertex
  `post_fullscreen.vert.spv` + fragment `post_tonemap.frag.spv`, single
  backbuffer-format color target, no depth, `PushConstantSize =
  sizeof(PostProcessPushConstants)`) is created in
  `InitializeOperationalPassResources(device)` and republished byte-identical
  across `RebuildOperationalResources()`, and the recipe's
  `"PostProcessPass"` umbrella executor branch routes through
  `RecordPostProcessToneMapPass(...)` with the recorded
  `SkippedNonOperational` / `SkippedUnavailable` / `Recorded` taxonomy.
  The pass body pushes the pass-local `PostProcessToneMapPushConstants`
  block exported by `Pass.PostProcess.ToneMap` (80 bytes — `Exposure +
  Operator + BloomIntensity + ColorGradingOn` header + four grading
  scalars + three `vec3 + float pad` rows under std430), which mirrors
  the `assets/shaders/post_tonemap.frag` `layout(push_constant)`
  declaration byte-for-byte; `BuildPostProcessToneMapPushConstants(
  settings)` derives `Exposure`/`BloomIntensity` from
  `PostProcessSettings` and keeps the operator at ACES with grading off
  for a deterministic neutral tonemap. The canonical 20-byte
  `PostProcessPushConstants` block shared by the other postprocess
  stages is intentionally not used for tonemap — pushing it would alias
  `HistogramBinCount` onto `ColorGradingOn` (a 256-bin default would
  enable grading) and `StageKind` onto `Saturation`
  (`bit_cast<float>(2)` ≈ 0 → grayscale), with the remaining 60 bytes
  reading implementation-defined memory, the standing "Shader
  push-constant compatibility policy" hard gate. `GRAPHICS-075` Slice B.1
  adds the bloom downsample + upsample pipelines (vertex
  `post_fullscreen.vert.spv` + fragment `post_bloom_downsample.frag.spv`
  / `post_bloom_upsample.frag.spv`, single `RGBA16_FLOAT` color target
  matching the `PostProcess.BloomScratch` recipe declaration, no depth,
  `PushConstantSize = sizeof(PostProcessBloom{Downsample,Upsample}PushConstants)`
  — each 16 bytes mirroring the shader's std430 push block). The
  `NullRenderer` owns `m_PostProcessBloomPass` plus
  `m_PostProcessBloomDownsamplePipelineLease` +
  `m_PostProcessBloomUpsamplePipelineLease`, both republished byte-identical
  across `RebuildOperationalResources()`. The umbrella branch fans out to
  `RecordPostProcessBloomPass(...)` *before* `RecordPostProcessToneMapPass(...)`
  so the bloom write naturally precedes the tonemap read of the bloom
  buffer in recorded order; Slice B.1 kept the helper's body at a single
  bind/push/draw per stage (placeholder coverage for the CPU contract
  gate), and Slice B.2 now iterates the bloom mip pyramid that the
  recipe declared. `BuildDefaultFrameRecipe` clamps
  `PostProcess.BloomScratch.MipLevels` via
  `ComputeBloomMipChainLevels(width, height)` so the declaration
  honors Vulkan's `mipLevels <= floor(log2(max(W, H))) + 1` rule for
  tiny/minimised viewports (the canonical cap is
  `kBloomMipChainLevels = 6`, applied only when the viewport supports
  it). The renderer resolves the per-frame `PostProcess.BloomScratch`
  transient handle from the compiled graph and republishes it
  alongside the same clamped mip count via
  `PostProcessBloomPass::SetBloomScratch(handle, mipLevels)` so the
  pass-side iteration matches the texture's actual mip range. For an
  effective depth `M >= 2` the pass records `M-1` downsamples (mip 0 →
  1, …, M-2 → M-1) followed by `M-1` upsamples (mip M-1 → M-2, …, 1 →
  0) with the per-shader push payload sized for each step's *source*
  mip extent. `IsFirstMip = 1` fires only on the mip 0 → mip 1
  downsample (the `SceneColorHDR`-sourced read), keeping the soft-
  threshold knee off the coarser pyramid mips. The pass body emits
  *no* inline `TextureBarrier(...)` calls: the umbrella render pass is
  active when `Execute(...)` runs, and Vulkan rejects layout
  transitions issued in a render-pass scope on the attachment
  currently being rendered. The inter-pass `BloomScratch
  ColorAttachment → ShaderReadOnly` transition between the bloom and
  tonemap legs is owned by the framegraph compiler from the recipe-
  level `Write(BloomScratch, ColorAttachmentWrite)` /
  `Read(BloomScratch, ShaderRead)` declarations, which is layout-safe
  for the whole-texture case. Correct *per-mip* subresource
  transitions interleaved with the down/up chain remain a follow-up
  slice that needs both an `ICommandContext::TextureBarrier(handle,
  mipRange, ...)` RHI extension and per-mip render-pass restarts
  between iterations. `GRAPHICS-075` Slice C adds the FXAA pipeline
  (vertex `post_fullscreen.vert.spv` + fragment `post_fxaa.frag.spv`,
  single backbuffer-format color target matching the tonemap leg's
  `SceneColorLDR` output, no depth, `PushConstantSize =
  sizeof(PostProcessFXAAPushConstants)` — 20 bytes mirroring the
  shader's `vec2 InvResolution + float ContrastThreshold + float
  RelativeThreshold + float SubpixelBlending` std430 push block). The
  `NullRenderer` owns `m_PostProcessFXAAPass` +
  `m_PostProcessFXAAPipelineLease`, both republished byte-identical
  across `RebuildOperationalResources()`. **GRAPHICS-075 Slice D.2a
  splits the AA umbrella into three ordered graph passes**
  (`"PostProcessAAEdgePass"`, `"PostProcessAABlendPass"`,
  `"PostProcessAAResolvePass"`) so edge / blend / resolve pipelines
  can target format-incompatible color attachments. The recipe
  declares `PostProcess.AATemp.{Edges,Weights,Resolved}` as three
  matched-format transients (`RG8_UNORM` / `RGBA8_UNORM` /
  backbuffer format) and each pass declares a single matched-format
  `Write`. FXAA records under the resolve pass only — its sampled-
  image read of `SceneColorLDR` crosses a real framegraph read-
  after-write barrier rather than aliasing the umbrella's color
  attachment mid-render-pass (Vulkan's classic read-after-write
  feedback hazard). The framegraph compiler emits the
  `SceneColorLDR ColorAttachment → ShaderRead` transition between
  `PostProcessPass` (bloom + tonemap) and `PostProcessAAEdgePass`,
  then the `AATemp.Edges ColorAttachment → ShaderRead` transition
  between edge and blend, and the `AATemp.Weights ColorAttachment →
  ShaderRead` transition before resolve. `presentSource` flips to
  `PostProcess.AATemp.Resolved` only when
  `PostProcessSettings::AntiAliasing != None` **and** the matching
  AA mode's pipeline(s) are actually present
  (`FrameRecipeFeatures::EnableAntiAliasing = true` only if the
  renderer's `SelectedAntiAliasingPipelinesAvailable()` is true:
  FXAA requires the FXAA pipeline; SMAA requires all three SMAA
  pipelines because the resolve shader reads `AATemp.Weights` and
  the blend shader reads `AATemp.Edges`, so a missing upstream
  pipeline would route a cleared resolve to present). Otherwise
  present stays on `SceneColorLDR`, and
  `RecordPostProcessAAResolvePass` reports `SkippedUnavailable`
  rather than falsely recording `Recorded` against a no-op draw.
  `BuildPostProcessFXAAPushConstants(settings, viewportWidth,
  viewportHeight)` derives `InvResolution` from
  `RHI::CameraUBO::Viewport{Width,Height}` (a zero / negative extent
  maps to a zero inverse so the shader's neighbour-tap UVs degenerate
  gracefully) and keeps `ContrastThreshold` / `RelativeThreshold` /
  `SubpixelBlending` at the FXAA 3.11 quality defaults documented in
  `assets/shaders/post_fxaa.frag`; future `PostProcessSettings::FXAA*`
  fields flow through this builder so the pass body and pipeline desc
  stay unchanged. The canonical 20-byte `PostProcessPushConstants`
  block is intentionally not reused even though the wire size matches
  — under std430 it would alias `Exposure` onto `InvResolution.x`,
  `Gamma` onto `InvResolution.y`, `BloomIntensity` onto
  `ContrastThreshold`, etc., and produce visually-meaningless FXAA
  output. The FXAA body is gated by
  `PostProcessSettings::AntiAliasing == FXAA` inside the pass body
  (which `IsStageEnabled` enforces); `None` or `SMAA` short-circuits
  `Execute(...)` to a no-op while the helper still returns
  `Recorded` under the `"PostProcessAAResolvePass"` accumulator,
  mirroring the bloom helper's "structurally-recorded no-op"
  taxonomy when `EnableBloom = false`. `GRAPHICS-075` Slice D.2a
  fixes the three SMAA pipelines (vertex
  `post_fullscreen.vert.spv` paired with `post_smaa_edge.frag.spv` /
  `post_smaa_blend.frag.spv` / `post_smaa_resolve.frag.spv`) at the
  recipe's matched formats: edge writes
  `PostProcess.AATemp.Edges` (`RG8_UNORM`), blend writes
  `PostProcess.AATemp.Weights` (`RGBA8_UNORM`), and resolve writes
  `PostProcess.AATemp.Resolved` (backbuffer format). The edge /
  blend pipeline build helpers are no longer parameterised on
  format because the recipe's transient declarations pin them.
  Each pipeline carries its own 16-byte std430 push block
  (`PostProcessSMAAEdgePushConstants` / `PostProcessSMAABlendPushConstants`
  / `PostProcessSMAAResolvePushConstants`) mirroring the matching
  shader's `Push` declaration byte-for-byte; the canonical 20-byte
  `PostProcessPushConstants` is intentionally not reused per the same
  shader-push-constant compatibility policy that motivated the
  Slice A / B / C pass-local push blocks. `NullRenderer` owns
  `m_PostProcessSMAAPass` +
  `m_PostProcessSMAA{Edge,Blend,Resolve}PipelineLease`, all four
  republished byte-identical across `RebuildOperationalResources()`.
  The SMAA pass exposes per-stage Execute methods
  (`ExecuteEdge` / `ExecuteBlend` / `ExecuteResolve`); each of the
  three per-stage AA helpers
  (`RecordPostProcessAA{Edge,Blend,Resolve}Pass`) invokes the
  matching SMAA Execute, and the resolve helper additionally
  invokes the FXAA pass body. Each per-stage body's `IsStageEnabled`
  gate enforces the `PostProcessSettings::AntiAliasing` selector so
  the helpers run unconditionally and only the active stage emits
  bind/push/draw; partial pipeline outages drop only the affected
  stage rather than collapsing the whole AA leg. Each SMAA push
  builder derives `InvResolution` from
  `RHI::CameraUBO::Viewport{Width,Height}` (zero / negative extent
  maps to a zero inverse so the shaders' neighbour-tap UVs
  degenerate gracefully) and keeps `EdgeThreshold` /
  `MaxSearchSteps` / `MaxSearchStepsDiag` at the SMAA reference
  defaults; future `PostProcessSettings::SMAA*` fields flow through
  these builders without touching the pass body or pipeline descs.
  Retained `AreaTex` / `SearchTex` LUT textures sampled by the
  blend pipeline + exposure-adaptation history buffer +
  device-aware `PostProcessSystem::Initialize(device)` overload have
  landed with `GRAPHICS-075` Slice D.2b: the new
  `PostProcessSystem::Initialize(device, textureMgr, bufferMgr)`
  overload allocates the SMAA `AreaTex` (`RG8_UNORM`, 160x560) and
  `SearchTex` (`R8_UNORM`, 66x33) LUT textures via
  `RHI::TextureManager::Create(...)` and uploads their analytical
  LUT bytes (ported byte-for-byte from
  `src/legacy/Graphics/Passes/Graphics.SMAALookupTextures.hpp` into
  a private namespace inside `Graphics.PostProcessSystem.cpp` so
  promoted `graphics/renderer` never imports from `src/legacy`)
  through `device.GetTransferQueue().UploadTexture(...)`; the
  exposure-adaptation history buffer (new `PostProcessExposureHistory`
  POD with `previous_average_log_lum` / `adaptation_velocity` /
  `frame_index`) is allocated through `RHI::BufferManager::Create(...)`
  with `Storage | TransferDst` for the histogram readback drain to
  populate in Slice E. The overload is idempotent (a no-op when the
  leases are already valid or when the device is non-operational)
  and is invoked from both the renderer's `Initialize(device)` and
  `RebuildOperationalResources(device)` so a non-operational-at-init
  device still picks up the allocation when it becomes operational
  without a `Shutdown()`+`Initialize()` round-trip.
  `PostProcessSystem::Shutdown()` releases the three leases before
  clearing the manager pointers, matching the `ShadowSystem`
  teardown ordering contract so the lease destructors call back
  through a still-live `TextureManager` / `BufferManager`. The
  retained handles + uploaded payload sizes are pinned by the new
  CPU/null contract test
  `RendererFrameLifecycle.PostProcessSMAALookupTexturesSurviveOperationalRebuild`.
  The SMAA blend shader still samples placeholder zero LUT bindings
  on the CPU/null gate; descriptor-set wiring of the retained LUTs
  into the SMAA blend pass body is a GPU/Vulkan-gate concern owned
  by the opt-in `gpu;vulkan` smoke.
  Histogram is wired through its own ordered graph pass
  `"PostProcessHistogramPass"` declared by the recipe with
  `Read(SceneColorHDR, ShaderRead) + Write(PostProcess.Histogram,
  BufferUsage::ShaderWrite)` between `"PointPass"` and
  `"PostProcessPass"`. The histogram is a *compute* dispatch and
  Vulkan rejects `vkCmdDispatch` inside an active render-pass scope,
  so it cannot share the `"PostProcessPass"` umbrella's render-pass
  scope (which hosts bloom + tonemap fragment work into color
  attachments). Slice E.1 lands the pipeline scaffold + dispatch
  helper (`RecordPostProcessHistogramPass(...)` plumbing the
  backbuffer extent into `PostProcessHistogramPass::SetViewport(...)`
  so the dispatch shape `ceil(W/16) x ceil(H/16) x 1` tracks the
  runtime viewport instead of the Slice A stub's `(1, 1, 1)`, and
  the compiled `PostProcess.Histogram` transient buffer handle
  into `SetHistogramBuffer(...)` so the pass body zero-fills the
  256 uint32 bins via `FillBuffer + BufferBarrier(TransferWrite →
  ShaderWrite)` before dispatching — the shader accumulates via
  `atomicAdd`, so without the per-frame clear the transient
  allocator's reused contents would contaminate the next frame's
  luminance distribution and corrupt Slice E.2's exposure-adaptation
  readback; this mirrors `CullingSystem::ResetCounters` for the
  same atomic-add-on-stale-data hazard, and the recipe declares
  `BufferUsage::Storage | TransferSrc | TransferDst` on
  `PostProcess.Histogram` so `vkCmdFillBuffer` is legal) and
  the pass-local 16-byte `PostProcessHistogramPushConstants` block
  (`uint Width + uint Height + float MinLogLum + float RangeLogLum`)
  mirroring `post_histogram.comp` byte-for-byte under std430 — the
  canonical 20-byte `PostProcessPushConstants` block would alias
  `Exposure` onto `Width` as `bit_cast<uint>(1.0f)` ≈ 1.07e9
  pixels, producing a degenerate dispatch. Slice E.2 adds the
  renderer-owned host-visible `Histogram.Readback` buffer
  (`1024 * frames-in-flight` bytes, `HostVisible | TransferDst`)
  imported by the recipe through `FrameRecipeImports::HistogramReadback`,
  the per-frame `CopyBuffer(PostProcess.Histogram → Histogram.Readback
  @ slot * 1024)` recorded by the `"PostProcessHistogramPass"`
  executor branch after the compute dispatch (bracketed by
  `ShaderWrite → TransferRead → ShaderWrite` buffer barriers on
  the per-frame `PostProcess.Histogram` handle so the atomic
  accumulations are visible to the copy), the `BeginFrame()`-side
  drain mirroring the `Picking.Readback` drain pattern, and the
  new `PostProcessSystem::PublishHistogramReadback(bins, frameIndex,
  device)` entry point that decodes the 256-bin payload into the
  retained `PostProcessExposureHistory` CPU mirror through a
  one-pole IIR and uploads the new history to the device-side
  `PostProcess.ExposureHistory` storage buffer through the
  transfer queue. A new `HistogramReadbackCopyCount` stat counter
  on `RenderGraphFrameStats` mirrors the existing
  `PickingReadbackCopyCount` so contract tests can assert the
  copy ran exactly once per operational frame. Each AA stage is free to define its own pass-local push
  block where the shader interface demands more than the canonical 20
  bytes. Frame recipe resources `PostProcess.BloomScratch`,
  `PostProcess.Histogram`, and
  `PostProcess.AATemp.{Edges,Weights,Resolved}` are transient
  postprocess-owned intermediates; concrete Vulkan descriptors and
  shaders remain backend follow-ups. Per `GRAPHICS-013AQ`,
  `PostProcessSystem` is the sole owner of the retained postprocess resources
  (SMAA `AreaTex` `R8G8_UNORM` 160x560 and `SearchTex` `R8_UNORM` 66x33
  lookup textures — the historic `256x33` notation tracked the wrong
  width; the SMAA reference and `post_smaa_blend.frag` both define
  `SMAA_SEARCHTEX_SIZE = vec2(66, 33)` — plus the exposure-adaptation
  history buffer holding
  `previous_average_log_lum` / `adaptation_velocity` / `frame_index`),
  allocated once at `Initialize()` through
  `RHI::TextureManager`/`RHI::BufferManager` and freed at `Shutdown()`. Bloom
  uses one frame-transient `PostProcess.BloomScratch` mip-chain texture with
  per-mip subviews (capped at six mips, truncating at extents below `8x8`),
  the histogram stage uses a fixed 256-bin layout over `[-10, +10]` log2
  luminance stops, and histogram diagnostics readback uses the same drain
  pattern as `Picking.Readback` (host-visible staging copy recorded at
  frame-record time, drained on the next `BeginFrame()` after the issuing
  frame's fences complete). FXAA samples post-tonemap `SceneColorLDR` with no
  intermediate and no LUT, writing the resolved color directly into
  `PostProcess.AATemp.Resolved` (the resolve graph pass). SMAA fans out across
  three matched-format transients
  (`PostProcess.AATemp.Edges` `R8G8_UNORM`,
  `PostProcess.AATemp.Weights` `R8G8B8A8_UNORM`,
  `PostProcess.AATemp.Resolved` backbuffer format), one per ordered AA graph
  pass; FXAA and SMAA remain mutually exclusive per
  `PostProcessSettings::AntiAliasing`, and
  quality presets are encoded into `PostProcessPushConstants::StageKind`
  packing rather than expanding the push-constant struct. Concrete
  `VkDescriptorSetLayout` bindings remain backend-local under
  `src/graphics/vulkan` and never leak through RHI or renderer code.
- `DebugViewSystem` owns backend-agnostic render-target inspection metadata and
  debug-view resource selection. It resolves requested frame-recipe resources to
  enabled previewable texture/depth resources, reports missing/disabled/buffer
  selections through deterministic diagnostics, and falls back to the current
  presentation source without platform/window ownership. Per `GRAPHICS-013BQ`,
  no retained graphics-owned debug-view textures or buffers exist;
  `DebugViewRGBA` is a frame-recipe transient owned by the framegraph and is
  deliberately non-selectable as a preview input
  (`DebugViewSystem::BuildInspectionTable()` excludes
  `FrameRecipeResourceKind::DebugViewRGBA` from `Previewable` to prevent
  self-sampling). `Pass.DebugView` owns one pass-local descriptor set with
  exactly two bindings (sampled image view of the resolved selection +
  linear-clamp sampler), with concrete `VkDescriptorSetLayout` definitions and
  per-aspect view creation (color view for `RGBA8_UNORM`/`RGBA16_FLOAT`
  resources, depth-aspect-only view for depth-class resources, integer-typed
  view for the `R32_UINT` selection-ID resources `EntityId`/`PrimitiveId`)
  remaining backend-local under `src/graphics/vulkan`. Visualization mode is
  derived deterministically from `FrameRecipeResourceKind` plus
  `DebugViewResourceClass`: direct LDR color blit for `SceneColorLDR`,
  Reinhard tonemap for `SceneColorHDR`, depth-linearize-to-grayscale for
  `SceneDepth`/`ShadowAtlas`, world-space normal for `SceneNormal`,
  integer-hash to color for `EntityId`/`PrimitiveId` (`PrimitiveId` decoded
  via `EncodedSelectionId`), direct color for `Albedo`, and scalar
  channel false-color (roughness -> R, metallic -> G, blue zeroed) for
  `Material0` per the `surface_gbuffer.frag` G-buffer contract — `Material0`
  is **not** an integer slot-ID resource and never uses the integer-hash
  path. `DebugViewSettings` does not gain a user-selectable visualization-mode
  field, and `DebugViewPushConstants` keeps its existing four-`uint32`
  packing.
  Runtime/editor code owns the dictionary that maps UI display strings to
  canonical `FrameRecipeIntrospection::Resources[i].Name` keys using the rows
  exposed by `DebugViewSystem::BuildInspectionTable()`, then writes the canonical
  name into `DebugViewSettings::RequestedResourceName` via
  `DebugViewSystem::SetSettings(...)`; graphics never receives display strings,
  never imports ImGui or platform/window state, and the default
  `RequestedResourceName = "FrameRecipe.PresentSource"` remains the graphics-side
  fallback. Buffer-class resources stay listed in the inspection table but
  remain non-previewable in `Pass.DebugView`; textual/statistical buffer
  inspection is deferred to a future runtime/editor visualization surface
  tracked under `GRAPHICS-014Q` that consumes existing per-owner diagnostics
  (`PostProcessDiagnostics`, `SelectionSystem`/`Picking.Readback` drains,
  `GpuWorld::Diagnostics`, `SpatialDebugVisualizerDiagnostics`) rather than
  adding a parallel buffer-readback API on `DebugViewSystem`.
- `SelectionSystem` is the CPU-visible reporting-only seam for picking.
  Selection ID passes write `EntityId` (stable extracted entity ID, `0`
  reserved for "no hit") and `PrimitiveId` (packed via `EncodedSelectionId`
  with the high 4 bits = `SelectionPrimitiveDomain` and the low 28 bits =
  authoritative face/edge/point payload). The renderer copies the requested
  pixel into the graphics-owned host-visible `Picking.Readback` buffer at
  frame-record time and drains it on the next `BeginFrame()` after the
  issuing frame's fences complete, calling `PublishPickResult` for valid
  samples (and an explicit no-hit `PickReadbackResult` for `EntityId == 0` /
  invalidated requests / deterministic readback failures). Because several
  picking slots can complete in one `BeginFrame()`, `PublishPickResult`
  appends to a **FIFO queue** of completed readbacks (not a single
  last-result holder) so no result is dropped; consumers drain it in order
  via `SelectionSystem::PopPickResult()`, while `GetLastPickResult()` remains
  a non-destructive peek at the most recent result. Each published result
  carries the issuing pick's correlation `Sequence` (RUNTIME-089), threaded
  from `RenderWorld::PickRequest` through the per-slot `m_PickingSlotSequence`
  bookkeeping, so the runtime resolves the exact in-flight request even when
  slots publish out of issue order. Backends never invoke `RequestPick` /
  `ConsumePick` themselves, and the CPU/null backend simulates the same
  drain without Vulkan-specific code so it stays the correctness gate. Per
  `GRAPHICS-012Q`, runtime owns `StableEntityId` -> live ECS resolution,
  ECS selection / hover mutation, editor selection policy, and the
  selection-outline input mask consumed by `SelectionOutlinePass`; graphics
  never reads or mutates ECS state. Until `GRAPHICS-025` introduces
  selectable transparent / special-forward sub-buckets, only `Selectable`
  opaque renderables flow through `SelectionSurface` / `SelectionLines` /
  `SelectionPoints`, and transparent picks fall back to runtime CPU
  picking when editor policy requires them.
- `ImGuiOverlaySystem` owns backend-agnostic overlay draw-data summaries and
  diagnostics translated from higher-level UI/runtime code. `ImGuiPass` overlays
  accepted draw data on `FrameRecipe.PresentSource`; it never writes the imported
  backbuffer. `PresentPass` is the explicit finalization shim, and render-graph
  validation rejects non-present writes to imported backbuffer resources.
  Per `GRAPHICS-013CQ`, runtime/editor code (the runtime-side Dear ImGui
  platform/renderer adapter, **not** graphics) walks the `ImDrawData` produced
  by `ImGui::Render()` and constructs `ImGuiOverlayFrame` records, then calls
  `ImGuiOverlaySystem::SubmitFrame(...)` once per frame after `ImGui::Render()`
  and before `IRenderer::PrepareFrame()`, alongside the
  `IRenderer::SubmitRuntimeSnapshots()` handoff; the renderer invokes
  `ClearFrame()` at end-of-frame after `Pass.Present` finalizes the backbuffer.
  Graphics never imports `imgui.h`, never calls Dear ImGui platform/renderer
  backends, and never sees `ImDrawData` directly. Overlay vertex/index payload
  upload mirrors the renderer-owned transient debug / visualization overlay
  helpers: `ImGuiUploadHelper` owns one growing host-visible vertex buffer and
  one growing index buffer, uploads the accepted `ImGuiOverlayFrame` POD
  payloads through `RHI::IDevice::WriteBuffer`, and returns per-list offsets
  plus per-command draw metadata to `Pass.ImGui`. The buffers are transient
  renderer resources, never retained on `GpuWorld` and never exposed to
  runtime. Font atlas texture is graphics-owned
  retained, mirroring SMAA `AreaTex`/`SearchTex` from `GRAPHICS-013AQ`
  (`R8_UNORM` fallback or `R8G8B8A8_UNORM` for colored atlases, allocated
  through `RHI::TextureManager`/`RHI::SamplerManager` and released by
  `ShutdownGpuResources()` before renderer manager teardown); DPI/font rebuilds
  re-run the overlay shutdown/initialize cycle. User textures referenced by
  `ImTextureID` in editor panels resolve through the existing `RHI::Bindless`
  heap by treating the direct `ImTextureID` value as a `BindlessIndex` carried
  on `ImGuiOverlayDrawCommand::TextureBindlessIndex`; `Pass.ImGui` pushes the
  selected index per command, and the ImGui fragment shader samples that slot
  (or the retained font atlas) with no new graphics-visible descriptor surface.
  `ImGuiOverlayFrame::DrawLists[i].UsesUserTexture` remains a diagnostics flag
  summarizing the command metadata.
  `ImGuiPass` owns exactly one pipeline created by the backend at startup
  and bound through the existing `SetPipeline`/`RHI::PipelineHandle` seam;
  backend Vulkan pipeline state (dynamic rendering against the
  present-source attachment, premultiplied-alpha blend, no depth test,
  scissor enabled, viewport from `DisplayWidth`/`DisplayHeight`, vertex
  stride `sizeof(ImDrawVert)`) remains backend-local under
  `src/graphics/vulkan`. `Pass.Present` keeps the CPU-testable
  fullscreen-triangle finalization form (`Draw(3, 1, 0, 0)` after binding
  the present pipeline); backend-native swapchain `vkCmdCopyImage` /
  `vkCmdBlitImage` paths are rejected as the contract form because
  graphics cannot guarantee identical source/backbuffer formats or a
  `TRANSFER_DST_OPTIMAL` swapchain layout without owning swapchain state.
  A backend may opt into a copy/blit fast-path only when it can prove
  identical formats and a compatible source layout after the overlay
  barrier; that decision remains backend-local and never alters the
  `Pass.Present` command contract or the frame-recipe `Present`
  declaration. Platform (`src/platform/`) owns window creation/destroy,
  the window-event pump, and DPI/display reporting; backend
  (`src/graphics/vulkan`) owns surface/swapchain lifecycle and
  acquire/present timing through `IDevice::BeginFrame`/`Present`/`Resize`
  per `GRAPHICS-018`; runtime (`src/runtime/`) owns composition (event
  pump, `IRenderer::BeginFrame`/`EndFrame` bracketing, post-`EndFrame`
  `IDevice::Present(frame)`, resize forwarding); graphics owns only the
  backbuffer-import declaration, the `Pass.Present` command contract,
  and render-graph rejection of non-present writes to the imported
  backbuffer.
- `GpuWorld` owns retained GPU-scene pools and exposes generation-checked
  lifetime diagnostics for instance/geometry slots, deferred reuse windows,
  retained-buffer pressure, overflow, stale handles, invalid handles, and
  null-device mode.
- Per
  [`GRAPHICS-028`](../../../tasks/done/GRAPHICS-028-ecs-renderable-residency-bridge.md),
  renderable ECS residency is a runtime-owned bridge. `Runtime.RenderExtraction`
  may keep entity-keyed sidecars containing graphics-owned values such as
  `GpuSceneSlot`, `GpuInstanceHandle`, material instances, and asset generation
  metadata (`GpuSceneSlot::SourceAsset` plus
  `GpuSceneSlot::LastSeenAssetGeneration`). The value type exposes
  `EvaluateSourceAssetRebind()` / `NeedsSourceAssetRebind()` for comparing an
  observed `(AssetId, generation)` supplied by runtime; it does not import
  `Graphics.GpuAssetCache` or query live asset state. Graphics render passes
  consume only submitted snapshots/views and never query live ECS or runtime
  sidecar state. ECS dirty tags remain CPU-only semantics; runtime maps them to
  `GpuWorld::GeometryUploadDesc` uploads, `GpuSceneSlot::NamedBuffers`, or
  per-instance updates according to the active domain packer.
- Per
  [`GRAPHICS-030`](../../../tasks/done/GRAPHICS-030-runtime-geometry-residency-bridge.md),
  the procedural-source first slice of that bridge keeps `GpuWorld`
  domain-agnostic: a runtime-owned `ProceduralGeometryCache` (lives on
  `RenderExtractionCache`) deduplicates `(Kind, Hash(Params))` keys, the
  per-kind packer in `Extrinsic.Runtime.ProceduralGeometryPacker` produces
  the existing `GpuWorld::GeometryUploadDesc`, and refcount-zero entries
  defer `GpuWorld::FreeGeometry()` until `framesInFlight` ticks have
  elapsed (matched to `Graphics::GpuAssetCache::Tick(currentFrame,
  framesInFlight)`). Procedural sources never participate in
  `GpuAssetCache` generation tracking — `GpuSceneSlot::SourceAsset` stays
  default-constructed, and the existing `HasSourceAsset()` check is the
  GRAPHICS-023C/D observation discriminator. No graphics-module surface
  changes; `Extrinsic.Graphics.GpuWorld` continues to expose only its
  existing `UploadGeometry`/`FreeGeometry`/`SetInstanceGeometry` API.
- Per
  [`GRAPHICS-031`](../../../tasks/done/GRAPHICS-031-default-debug-surface-material.md),
  the canonical missing-material fallback is the default debug surface
  material at slot 0 (`Extrinsic::Graphics::kDefaultMaterialSlotIndex`),
  registered by `MaterialSystem::Initialize()` as
  `"Material.DefaultDebugSurface"` with
  `MaterialTypeID = kMaterialTypeID_DefaultDebugSurface = 2u`,
  `MaterialFlags::Unlit`, and a deterministic non-black `BaseColorFactor`
  (`{0.55, 0.20, 0.85, 1.0}`). The shader pair lives at
  `assets/shaders/forward/default_debug_surface.vert/frag` and is
  authored against the same BDA-only contract as `depth_prepass.vert`:
  the promoted Vulkan pipeline layout binds only the global bindless
  descriptor set at set 0 (`setLayoutCount = 1` /
  `VK_SHADER_STAGE_ALL`-visible push constants), so all per-frame data
  is reached via `GpuScenePushConstants::SceneTableBDA` plus the
  `buffer_reference` chain declared in `common/gpu_scene.glsl`
  (position read from the procedural vertex buffer at the GRAPHICS-030A
  `{vec3 pos, vec2 uv}` 20-byte stride; material slot read via
  `scene.MaterialBDA`). No `set = 0` camera UBO and no `set = 3`
  material SSBO are declared — those would collide with the bindless
  descriptor set or reference an unbound set; no per-material descriptor
  set or new cull bucket is introduced (the lane reuses `SurfaceOpaque`).
  Pipeline state is locked at `CullMode = Back`, `DepthCompareOp = Less`
  (or `Equal` after `Pass.DepthPrepass`), `BlendEnabled = false`,
  `PolygonMode = Fill`, `PrimitiveTopology = TriangleList`,
  `MSAA samples = 1`, dynamic state `{Viewport, Scissor}`, with the
  pipeline created once at renderer init and republished byte-identical
  by `RebuildOperationalResources()`. `PipelineDesc::VertexShaderPath`
  and `FragmentShaderPath` are pre-resolved via
  `Core::Filesystem::GetShaderPath("shaders/forward/default_debug_surface.{vert,frag}.spv")`
  so they reference the compiled SPV artifacts emitted by
  `intrinsic_add_glsl_shaders()` rather than the raw GLSL sources
  (`VulkanDevice::CreatePipeline()` reads the path verbatim as a SPIR-V
  binary). Vertex transform currently lands in `dyn.Model`-space because
  the scene table does not yet expose a camera matrix BDA; wiring view-
  projection into the scene-table contract is a GRAPHICS-031B follow-up. The graphics-owned snapshot-consumption
  substitution at the renderer span-copy step replaces unset/out-of-range
  material slots with `kDefaultMaterialSlotIndex` and increments one of
  three additive `MaterialSystemDiagnostics` counters:
  `MissingMaterialFallbackCount` (sentinel-unset authoring),
  `InvalidMaterialSlotCount` (out-of-range/stale slot integers), and
  `DefaultDebugSurfaceUses` (total per-frame uses of slot 0 after
  substitution). Runtime stays agnostic of graphics-side slot identity;
  the existing `MaterialSystemDiagnostics::FallbackSlotResolveCount`
  continues to track the separate `GetMaterialSlot()` stale-handle path.
  Follow-up debug-material variants (`Wireframe`, `Line`, `Point`,
  `Normals`, `UVs`, `Depth`, `InstanceId`) attach as additional
  `MaterialTypeDesc` registrations and additional well-known slot
  constants under the naming family `Material.DefaultDebug<Variant>` /
  `kDefaultDebug<Variant>MaterialSlotIndex`; they are identified but not
  opened. Implementation children GRAPHICS-031-Impl-A (shader sources +
  pipeline + slot-0 repopulation; landed by
  [`GRAPHICS-031A`](../../../tasks/done/GRAPHICS-031A-default-debug-surface-shaders-and-pipeline.md):
  the shaders are authored at
  `assets/shaders/forward/default_debug_surface.vert/frag`,
  `MaterialSystem::Initialize()` registers the three built-in types
  StandardPBR/SciVis/DefaultDebugSurface and packs slot 0 with
  `kDefaultDebugSurfaceBaseColor` and `MaterialFlags::Unlit`, and the
  renderer caches a `m_DefaultDebugSurfacePipelineLease` built from a
  byte-identical `BuildDefaultDebugSurfacePipelineDesc()` whose
  `VertexShaderPath` / `FragmentShaderPath` are pre-resolved via
  `Core::Filesystem::GetShaderPath` to the compiled
  `shaders/forward/default_debug_surface.{vert,frag}.spv` artifacts
  emitted by `intrinsic_add_glsl_shaders()` so the operational Vulkan
  pipeline-creation path reads SPIR-V directly. Initial `Initialize()`
  and `RebuildOperationalResources()` republish the same descriptor),
  Impl-B (substitution wiring + the three diagnostics counters; landed
  by
  [`GRAPHICS-031B`](../../../tasks/done/GRAPHICS-031B-default-debug-surface-substitution-and-diagnostics.md):
  the renderer's snapshot-copy step in
  `Graphics.Renderer.cpp::SubmitRuntimeSnapshots()` mutates
  `m_TransformSyncRecords` in place so that records with
  `HasMaterialSlot == false` are reassigned to
  `kDefaultMaterialSlotIndex` and counted via
  `MaterialSystem::RecordMissingMaterialFallback()`, records whose
  `MaterialSlot >= MaterialSystem::GetCapacity()` are reassigned and
  counted via `MaterialSystem::RecordInvalidMaterialSlot()`, and every
  record whose final slot equals `kDefaultMaterialSlotIndex` increments
  `MaterialSystem::RecordDefaultDebugSurfaceUse()`. The three
  per-frame counters (`MissingMaterialFallbackCount`,
  `InvalidMaterialSlotCount`, `DefaultDebugSurfaceUses`) live on
  `MaterialSystemDiagnostics` next to the unchanged
  `FallbackSlotResolveCount`, and are zeroed at the start of every
  `SubmitRuntimeSnapshots()` and inside `ResetFrameState()` —
  mirroring the existing `InvalidSnapshotRecordCount` reset cadence —
  via `MaterialSystem::ResetPerFrameSubstitutionCounters()`), and
  the optional Impl-C (one additional debug variant) is identified
  but not opened.
- `Graphics` may depend on `Core`, asset IDs, `RHI`, and geometry GPU views; it
  must not import live ECS ownership and must not store graphics GPU handles in
  canonical ECS components.
- `Graphics.RenderGraph` must not import ECS internals or runtime ownership
  directly.
- `Runtime` must not manipulate render-graph barriers/resources directly.

## Architecture references

- [AGENTS.md](../../../AGENTS.md) — authoritative repository contract and layering rules.
- [docs/architecture/graphics.md](../../../docs/architecture/graphics.md)
- [docs/architecture/rendering-three-pass.md](../../../docs/architecture/rendering-three-pass.md)
- [docs/architecture/rendering-target-architecture.md](../../../docs/architecture/rendering-target-architecture.md)
- [docs/architecture/task-graph-domains.md](../../../docs/architecture/task-graph-domains.md)
- [docs/migration/nonlegacy-parity-matrix.md](../../../docs/migration/nonlegacy-parity-matrix.md) — historical/advisory parity tracking.
