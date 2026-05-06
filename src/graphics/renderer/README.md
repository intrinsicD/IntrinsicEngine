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
structured hard-error findings when `Compile()` fails. `GetLastCompileDiagnostic()`
is retained only as a temporary string compatibility shim; removal is tracked by
`GRAPHICS-027`.

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
- `TransformSyncSystem`, `LightSystem`, and `VisualizationSyncSystem` consume
  graphics-owned snapshot records (`TransformSyncRecord`, `LightSnapshot`, and
  `VisualizationSyncRecord`) instead of querying live ECS registries. Runtime is
  responsible for building those records from ECS/assets/geometry state.
- `IRenderer::SubmitRuntimeSnapshots()` is the promoted handoff from runtime to
  graphics. The renderer copies snapshot records into frame-local storage before
  `ExtractRenderWorld()`/`PrepareFrame()` consume them; it does not retain ECS
  registry references.
- `RenderWorld` exposes immutable spans of renderer-owned `RenderableSnapshot`
  and `LightSnapshot` values, sanitized transient debug line/point/triangle
  packet spans, transform-gizmo render packet spans, `VisualizationSnapshot`
  packet spans/diagnostics, camera/view/frustum snapshots, defaulted optional
  packets for picking, selection, shadows, postprocess/readback, and
  invalid-record diagnostics. These records are valid for the frame and never
  reference live ECS storage.
- `Graphics.CameraSnapshots` is data-only: it validates view/projection
  matrices, extracts frustum planes, and derives pick rays from immutable pixel
  requests. Camera motion, input polling, gizmo hit testing, and transform
  mutation remain runtime/platform/editor responsibilities.
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
- `Graphics.FrameRecipe` imports explicit cull bucket resources for surface,
  line, and point lanes. `LinePass` consumes `Cull.Lines.IndexedArgs` /
  `Cull.Lines.Count`; `PointPass` consumes `Cull.Points.NonIndexedArgs` /
  `Cull.Points.Count`. These cull-bucket resources stay reserved for retained
  `GpuRender_Line`/`GpuRender_Point` renderables and are not the transient
  debug expansion path.
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
  presentation source without platform/window ownership.
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
- `GpuWorld` owns retained GPU-scene pools and exposes generation-checked
  lifetime diagnostics for instance/geometry slots, deferred reuse windows,
  retained-buffer pressure, overflow, stale handles, invalid handles, and
  null-device mode.
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
