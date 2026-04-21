# Graphics

`src_new/Graphics` owns rendering. It is split into a backend-agnostic RHI
layer, one or more concrete backends, and a renderer that drives frame
production on top of RHI.

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
