# Graphics

`src_new/Graphics` owns rendering. It is split into a backend-agnostic RHI
layer, one or more concrete backends, and a renderer that drives frame
production on top of RHI.

## Public module surface

### Renderer

- `Extrinsic.Graphics.Renderer`

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

- `Extrinsic.Backends.Vulkan` — Vulkan 1.3 implementation of RHI.

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

## Dependency note

`Graphics` depends on `Core` (and, when rendering a scene, on `ECS` component
contracts). It must not depend on `Runtime`, `Platform` backends directly, or
on any `App`. Platform surfaces are passed in by `Runtime` at composition
time; `Graphics` accepts a borrowed `Extrinsic.Platform.Window` reference and
does not know which backend created it.
