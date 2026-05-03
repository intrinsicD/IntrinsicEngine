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

### Scene and sync systems

- `Extrinsic.Graphics.RenderFrameInput`
- `Extrinsic.Graphics.RenderWorld`
- `Extrinsic.Graphics.RuntimeRenderSnapshotBatch` (declared by `Extrinsic.Graphics.Renderer`)
- `Extrinsic.Graphics.GpuWorld`
- `Extrinsic.Graphics.Material`
- `Extrinsic.Graphics.MaterialSystem`
- `Extrinsic.Graphics.ColormapSystem`
- `Extrinsic.Graphics.VisualizationSyncSystem`
- `Extrinsic.Graphics.CullingSystem`
- `Extrinsic.Graphics.LightSystem`
- `Extrinsic.Graphics.SelectionSystem`
- `Extrinsic.Graphics.ForwardSystem`
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
  and `LightSnapshot` values, defaulted optional packets for picking,
  selection, shadows, debug primitives, postprocess/readback, and invalid-record
  diagnostics. These records are valid for the frame and never reference live
  ECS storage.
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
