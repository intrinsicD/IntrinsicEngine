# Graphics

`src_new/Graphics` owns rendering. It is split into a backend-agnostic RHI
layer, one or more concrete backends, and a renderer that drives frame
production on top of RHI.

## Public module surface

### Renderer

- `Extrinsic.Graphics.Renderer`

### Systems

- `Extrinsic.Graphics.MaterialSystem` — material type/instance owner and GPU SSBO authority.
- `Extrinsic.Graphics.ColormapSystem` — GPU colourmap LUT owner. Uploads one 256×1 RGBA8
  texture per `Colormap::Type` into the bindless heap; provides `GetBindlessIndex(Type)` for
  shaders and `SampleCpu(Type, t)` for CPU-baked line/point colours.
- `Extrinsic.Graphics.VisualizationSyncSystem` — per-entity sci-vis material override owner.
  Registers the `SciVis` material type (`kMaterialTypeID_SciVis = 1`), manages per-entity
  override `MaterialLease` entries, and resolves `MaterialInstance::EffectiveSlot` each frame.
- `Extrinsic.Graphics.CullingSystem` — GPU-driven culling buffer/pipeline authority.
- `Extrinsic.Graphics.LightSystem` — frame-global lighting state owner for camera UBO population.
- `Extrinsic.Graphics.SelectionSystem` — selection request/result owner for selection passes.
- `Extrinsic.Graphics.ForwardSystem` — forward pass family owner scaffold.
- `Extrinsic.Graphics.DeferredSystem` — deferred pass family owner scaffold.
- `Extrinsic.Graphics.PostProcessSystem` — post-process pass family owner scaffold.
- `Extrinsic.Graphics.ShadowSystem` — shadow pass family owner scaffold.
- `Extrinsic.Graphics.TransformSyncSystem` — transform SSBO / world-matrix sync scaffold.

### ECS Components (under `Components/`)

- `Extrinsic.Graphics.Component.GpuSceneSlot` — per-entity GPU scene state (culling slot index,
  named buffer map keyed by canonical name e.g. `"positions"`, `"scalars"`, `"colors"`).
- `Extrinsic.Graphics.Component.Material` — `MaterialInstance`: RAII `MaterialLease` into
  `MaterialSystem` + optional `TintOverride` + `EffectiveSlot` (written by
  `VisualizationSyncSystem`, consumed by `TransformSyncSystem`).
- `Extrinsic.Graphics.Component.RenderGeometry` — render hints (`RenderSurface`, `RenderLines`,
  `RenderPoints`) and `ScalarFieldConfig` (colourmap metadata; no GPU resource).
- `Extrinsic.Graphics.Component.VisualizationConfig` — sci-vis colour-source selector.
  When present on an entity with `MaterialInstance + GpuSceneSlot`, overrides the rendered
  colour for scientific visualisation:
  - `Material` — no override (MaterialInstance governs colour).
  - `UniformColor` — flat RGBA, Unlit.
  - `ScalarField` — GPU colourmap applied to a named `float` buffer via BDA; supports
    isolines and equal-width binning.
  - `PerVertexBuffer` / `PerEdgeBuffer` / `PerFaceBuffer` — raw RGBA from a named buffer.

### Material type system

Two well-known type IDs defined in `Extrinsic.Graphics.Material`:

| Constant | Value | Registered by | Shader path |
|---|---|---|---|
| `kMaterialTypeID_StandardPBR` | 0 | `MaterialSystem::Initialize()` | PBR lighting |
| `kMaterialTypeID_SciVis` | 1 | `VisualizationSyncSystem::Initialize()` | Colourmap/scivis shading mode |

**SciVis CustomData layout** (`GpuMaterialSlot::CustomData[0..3]` when `MaterialTypeID == 1`):

```
CustomData[0]: { colormapBindlessIdx(uint), domain(uint), rangeMin(f), rangeMax(f) }
CustomData[1]: { isolineCount(uint), packedIsolineColor(uint), isolineWidth(f), binCount(uint) }
CustomData[2]: { reserved visual constants }
CustomData[3]: reserved
```
Per-entity BDA pointers, element count, and color source mode are written to
`GpuEntityConfig` via `GpuWorld`, not packed into `GpuMaterialSlot`.

### Colourmap types (`Extrinsic.Graphics.Colormap`)

`Colormap::Type`: `Viridis`, `Inferno`, `Plasma`, `Jet`, `Coolwarm`, `Heat`.
`Colormap::kColormapCount` (= 6) is the sentinel.

### Frame sync call order

```
1. PipelineManager::CommitPending()                                  — finalize pending PSO objects
2. MaterialSystem::SyncGpuBuffer()                                   — flush dirty base materials
3. VisualizationSyncSystem::Sync(reg, mat, cmap, gpuWorld)          — resolve EffectiveSlot, write GpuEntityConfig
4. MaterialSystem::SyncGpuBuffer()                                   — flush dirty override materials
5. TransformSyncSystem::SyncGpuBuffer(reg, gpuWorld)                — write transforms/flags/material slots/bounds
6. LightSystem::SyncGpuBuffer(reg, gpuWorld)                        — write GpuLight[] payload
7. GpuWorld::SetMaterialBuffer(matSys.GetBuffer(), GetCapacity())   — refresh scene-table material binding
8. GpuWorld::SyncFrame()                                             — upload dirty runs + scene table
```

### Data contracts

- `Extrinsic.Graphics.Material`
- `Extrinsic.Graphics.RenderFrameInput`
- `Extrinsic.Graphics.RenderWorld`

### RHI (under `RHI/`)

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

### Backends (under `Backends/`)

- `Extrinsic.Backends.Null` — stub IDevice (`IsOperational() == false`);
  scaffold for a future `Extrinsic.Backends.Vulkan` module.

### Passes (under `Passes/`)

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

## Target architecture

The rendering design that `src_new/Graphics` builds toward is documented in
`docs/architecture/src_new-rendering-architecture.md`. Key invariants:

- **BufferManager-managed geometry.** No per-entity vertex/index buffers.
- **GPU-driven execution.** The CPU hands the GPU a scene description; the GPU
  builds indirect draw commands.
- **Deferred by default.** Opaque surfaces write a G-buffer; lighting is a
  single full-screen pass. Lines, points, and transparent objects composite on
  top.
- **Components are switches.** Presence of `RenderSurface`, `RenderLines`,
  or `RenderPoints` determines which pipelines render an entity — no boolean
  flags, no enum routing.
- **Visualisation is a separate concern.** `VisualizationConfig` overlays
  sci-vis colour on top of any entity without touching the base `MaterialInstance`.

## Assets ↔ Graphics boundary

`Graphics` is the **read-only consumer** of `Extrinsic.Asset.Service`. The
dependency is strictly one-way: Graphics may import Assets; Assets never
imports Graphics.

GPU-side state for asset-backed resources lives in a Graphics-owned side
table — `GpuAssetCache` — keyed by `AssetId`. Its contract:

- **Per-asset state machine:** `NotRequested → CpuPending → GpuUploading →
  Ready` (plus `Failed`). `Request(AssetId)` is the synchronous "use this
  frame" entry point; `TryGet(AssetId) -> optional<BufferView>` is the
  non-blocking render-extraction accessor. `nullopt` means skip or
  substitute this frame.
- **Upload lane:** reuse `RHI::TransferManager`. Do not invent a second
  staging queue, fence pool, or timeline. The cache submits into
  `TransferManager` and observes the timeline value to flip state.
- **Reload atomicity:** on `AssetEventBus::Reloaded(id)`, the new payload
  runs through the same state machine while the old `BufferView` stays
  alive; the swap to the new view happens in one frame when it reaches
  `Ready`. No flicker, no mid-frame invalidation.
- **No writeback into `AssetRegistry`.** GPU handles live only in this
  cache.

ECS components store `AssetId`, not `BufferView`. Render extraction resolves
the handle via `GpuAssetCache::TryGet` once per frame.

See `CLAUDE.md` → "Assets ↔ Graphics boundary" for the full policy.

## Dependency note

`Graphics` depends on `Core` and `Assets` (read-only via `AssetService` +
`AssetEventBus`). When rendering a scene it also depends on ECS component
contracts. It must not depend on `Runtime`, `Platform` backends directly, or
on any `App`. Platform surfaces are passed in by `Runtime` at composition
time; `Graphics` accepts a borrowed `Extrinsic.Platform.Window` reference and
does not know which backend created it.

## Public module surface

### Renderer

- `Extrinsic.Graphics.Renderer`

### Systems

- `Extrinsic.Graphics.MaterialSystem` — material type/instance owner and GPU SSBO authority.
- `Extrinsic.Graphics.CullingSystem` — GPU-driven culling buffer/pipeline authority.
- `Extrinsic.Graphics.LightSystem` — frame-global lighting state owner for camera UBO population.
- `Extrinsic.Graphics.SelectionSystem` — selection request/result owner for selection passes.
- `Extrinsic.Graphics.ForwardSystem` — forward pass family owner scaffold.
- `Extrinsic.Graphics.DeferredSystem` — deferred pass family owner scaffold.
- `Extrinsic.Graphics.PostProcessSystem` — post-process pass family owner scaffold.
- `Extrinsic.Graphics.ShadowSystem` — shadow pass family owner scaffold.
- `Extrinsic.Graphics.TransformSyncSystem` — transform SSBO / world-matrix sync scaffold.

### Data contracts

- `Extrinsic.Graphics.Material`
- `Extrinsic.Graphics.RenderFrameInput`
- `Extrinsic.Graphics.RenderWorld`

### RHI (under `RHI/`)

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

### Backends (under `Backends/`)

- `Extrinsic.Backends.Null` — stub IDevice (`IsOperational() == false`);
  scaffold for a future `Extrinsic.Backends.Vulkan` module.

### Passes (under `Passes/`)

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

## Target architecture

The rendering design that `src_new/Graphics` builds toward is documented in
`docs/architecture/src_new-rendering-architecture.md`. Key invariants:

- **BufferManager-managed geometry.** No per-entity vertex/index buffers.
- **GPU-driven execution.** The CPU hands the GPU a scene description; the GPU
  builds indirect draw commands.
- **Deferred by default.** Opaque surfaces write a G-buffer; lighting is a
  single full-screen pass. Lines, points, and transparent objects composite on
  top.
- **Components are switches.** Presence of `SurfaceComponent`, `LineComponent`,
  or `PointComponent` determines which pipelines render an entity — no boolean
  flags, no enum routing.

## Assets ↔ Graphics boundary

`Graphics` is the **read-only consumer** of `Extrinsic.Asset.Service`. The
dependency is strictly one-way: Graphics may import Assets; Assets never
imports Graphics.

GPU-side state for asset-backed resources lives in a Graphics-owned side
table — `GpuAssetCache` — keyed by `AssetId`. Its contract:

- **Per-asset state machine:** `NotRequested → CpuPending → GpuUploading →
  Ready` (plus `Failed`). `Request(AssetId)` is the synchronous "use this
  frame" entry point; `TryGet(AssetId) -> optional<BufferView>` is the
  non-blocking render-extraction accessor. `nullopt` means skip or
  substitute this frame.
- **Upload lane:** reuse `RHI::TransferManager`. Do not invent a second
  staging queue, fence pool, or timeline. The cache submits into
  `TransferManager` and observes the timeline value to flip state.
- **Reload atomicity:** on `AssetEventBus::Reloaded(id)`, the new payload
  runs through the same state machine while the old `BufferView` stays
  alive; the swap to the new view happens in one frame when it reaches
  `Ready`. No flicker, no mid-frame invalidation.
- **No writeback into `AssetRegistry`.** GPU handles live only in this
  cache.

ECS components store `AssetId`, not `BufferView`. Render extraction resolves
the handle via `GpuAssetCache::TryGet` once per frame.

See `CLAUDE.md` → "Assets ↔ Graphics boundary" for the full policy.

## Dependency note

`Graphics` depends on `Core` and `Assets` (read-only via `AssetService` +
`AssetEventBus`). When rendering a scene it also depends on `ECS` component
contracts. It must not depend on `Runtime`, `Platform` backends directly, or
on any `App`. Platform surfaces are passed in by `Runtime` at composition
time; `Graphics` accepts a borrowed `Extrinsic.Platform.Window` reference and
does not know which backend created it.
