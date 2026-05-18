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

`Extrinsic.Graphics.RenderGraph` executes barrier packets in pass order: for each pass, any packets tagged with that pass index are emitted immediately before the pass callback, and imported-resource final-state packets (the compiler's end-of-graph sentinel) are emitted after the last pass. The concrete renderer lowers those packets through `RHI::ICommandContext::SubmitBarriers` when recording a frame.

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

## Ownership contract

- `Runtime` owns live ECS access, extraction, sidecar mappings, dirty-domain
  interpretation, deletion events, and compaction/relocation handoff.
- `Graphics` consumes immutable snapshots/views supplied by runtime and owns GPU
  resource/state transitions and pass-level scheduling through
  `Graphics.RenderGraph`.
- `Graphics.FrameRecipe` owns the reusable default frame recipe: typed feature
  gates, canonical resource declarations, pass-order introspection, and the
  backend-agnostic graph construction path used by the null renderer.
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
  falls through to the catch-all soft-skip until GRAPHICS-072 lands its
  `Pass.Deferred.GBuffers` body. While GRAPHICS-072 is still open,
  `DeriveDefaultFrameRecipeFeatures()` selects
  `FrameRecipeLightingPath::Forward` so the default recipe can actually
  record draws; the deferred-mode branches in
  `Graphics.FrameRecipe.cpp`/`Test.FrameRecipeContract.cpp` remain
  exercise-able through explicit `FrameRecipeFeatures{}` construction.
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
  `set 0, binding 1` shadow-sampler binding is owned by GRAPHICS-072
  (absorbed from the original GRAPHICS-073 Slice C scope) and lands alongside
  the deferred GBuffer + lighting passes.
- GRAPHICS-032A wires `FrameRecipe::MinimalDebugSurface` as a separate opt-in
  recipe contract with the stable label `recipe.minimal-debug-surface`. The
  recipe is built by
  `BuildMinimalDebugSurfaceRecipe(graph, imports, sizing)` and declared by
  `DescribeMinimalDebugSurfaceRecipe()` in `Extrinsic.Graphics.FrameRecipe`;
  callers opt in via `Core::Config::RenderConfig::FrameRecipe =
  Core::Config::FrameRecipeKind::MinimalDebug` and the
  `IRenderer::SetFrameRecipe()` setter (default stays `FrameRecipeKind::Default`).
  The recipe declares only
  `Pass.Surface.MinimalDebug` and `Pass.Present.MinimalDebug`, writes
  `SceneColorHDR`/`SceneDepth`, finalizes the imported `Backbuffer` through the
  existing fullscreen-triangle `Pass.Present` contract, and never mutates the
  default recipe's feature gates or skip/no-op expectations. Its CPU/null command
  contract is property-based (`BeginRenderPass`, pipeline/descriptor/index-buffer
  binds, `DrawIndexedIndirectCount` for `SurfaceOpaque`, then fullscreen
  `Draw(3, 1, 0, 0)` for present) so tests assert pass labels, resource names,
  bucket kind, draw kind, and counts rather than transient allocator/native
  handles. Renderer diagnostics surface three counters on
  `RenderGraphFrameStats`:
  - `MinimalSurfacePassExecutions` — increments per successful surface-pass
    record in `RecordMinimalDebugSurfacePass` (GRAPHICS-032B). The pass body
    is owned by `Extrinsic.Graphics.Pass.Surface.MinimalDebug` and reuses the
    GRAPHICS-031A slot-0 default-debug-surface pipeline lease, drawing the
    SurfaceOpaque cull bucket through `DrawIndexedIndirectCount`.
  - `MinimalPresentPassExecutions` — increments per successful present-pass
    record in `RecordMinimalDebugPresentPass` (GRAPHICS-032C). The pass body
    is owned by `Extrinsic.Graphics.Pass.Present.MinimalDebug` and reuses the
    GRAPHICS-031A slot-0 default-debug-surface pipeline lease, recording the
    canonical fullscreen-triangle present form (`BindPipeline` +
    `Draw(3, 1, 0, 0)`).
  - `MinimalRecipeMissingPrerequisiteCount` — accumulates per-frame from
    three sites:
    - At recipe build time in `Graphics.Renderer.cpp::ExecuteFrame` after
      `BuildMinimalDebugSurfaceRecipe`, once per missing prerequisite:
      invalid material buffer residency, invalid surface-opaque bucket
      residency (counted jointly for the args/count pair), or invalid
      scene-table residency. The recipe still compiles when prerequisites
      are missing so the skip is observable rather than silent.
    - At record time in `RecordMinimalDebugSurfacePass` whenever the slot-0
      pipeline lease, the SurfaceOpaque cull bucket, or the GpuWorld
      scene-table state is unavailable; the pass then routes to
      `SkippedUnavailable` rather than recording an empty draw.
    - At record time in `RecordMinimalDebugPresentPass` whenever the
      shared slot-0 pipeline lease is unavailable; the pass then routes to
      `SkippedUnavailable`. Because the minimal-debug present and surface
      passes share the same lease, lease-failure scenarios increment the
      counter from both record sites.

  All three counters reset per-frame through the existing
  `m_LastRenderGraphStats = {}` cadence in `ResetFrameState()` and
  `ExecuteFrame()`. The GPU/Vulkan recipe-selector smoke landed with
  GRAPHICS-032D as
  `MinimalDebugSurfaceGpuSmoke.RecipeSelectorReachesOperationalVulkanCommandStream`,
  the sibling of the GRAPHICS-033D pixel-readback fixture; both share the
  bounded `engine.Run()` driver helper in
  `tests/integration/graphics/Test.MinimalDebugSurfaceGpuSmoke.cpp` so the
  driver loop is not duplicated. The entire MinimalDebug scaffold (the
  recipe, the two passes, and the three counters) is deleted by
  GRAPHICS-081 once the canonical default recipe records every pass body
  operationally.
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
  and the ImGui overlay upload from `GRAPHICS-013CQ` (per-frame host-visible
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
  packet data for `Histogram`, `Bloom`, `ToneMap`, `FXAA`, and `SMAA`. Frame
  recipe resources `PostProcess.BloomScratch`, `PostProcess.Histogram`, and
  `PostProcess.AATemp` are transient postprocess-owned intermediates; concrete
  Vulkan descriptors/shaders remain backend follow-ups. Per `GRAPHICS-013AQ`,
  `PostProcessSystem` is the sole owner of the retained postprocess resources
  (SMAA `AreaTex` `R8G8_UNORM` 160x560 and `SearchTex` `R8_UNORM` 256x33
  lookup textures, plus the exposure-adaptation history buffer holding
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
  intermediate and no LUT, while SMAA edge/blend intermediates fold under the
  existing `PostProcess.AATemp` slot as two named subresources
  (`AATemp.Edges` `R8G8_UNORM`, `AATemp.Weights` `R8G8B8A8_UNORM`); FXAA and
  SMAA remain mutually exclusive per `PostProcessSettings::AntiAliasing`, and
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
  samples and `PublishNoHit` for `EntityId == 0` / invalidated requests /
  deterministic readback failures. Backends never invoke `RequestPick` /
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
  upload mirrors the transient debug expansion pattern from
  `GRAPHICS-007Q`/`GRAPHICS-008Q`: per-frame host-visible (transient) GPU
  buffers owned by a backend-local upload helper under `src/graphics/vulkan`
  and recycled each frame, never retained on `GpuWorld` and never exposed
  through RHI or renderer module surfaces. Font atlas texture is graphics-
  owned retained, mirroring SMAA `AreaTex`/`SearchTex` from `GRAPHICS-013AQ`
  (`R8_UNORM` default or `R8G8B8A8_UNORM` for colored atlases, allocated
  once at `Initialize()` through `RHI::TextureManager` and freed at
  `Shutdown()`); DPI/font rebuilds re-run the `Shutdown()`/`Initialize()`
  cycle. User textures referenced by `ImTextureID` in editor panels flow
  through the existing `RHI::Bindless` heap as bindless texture indices
  carried in a backend-local per-cmd parameter buffer; no new
  graphics-visible descriptor surface is added, and
  `ImGuiOverlayFrame::DrawLists[i].UsesUserTexture` remains the only
  graphics-visible diagnostics flag for user-texture presence.
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
