# `src/graphics/assets/`

Graphics-owned bridge between `Assets::AssetId` and GPU resources.

## Module

- `Extrinsic.Graphics.GpuAssetCache` — side table mapping `AssetId` to a
  refcounted `BufferLease` / `TextureLease`, with a small state machine
  (`NotRequested`, `CpuPending`, `GpuUploading`, `Ready`, `Failed`) and a
  frame-anchored retire queue that preserves old GPU views across hot
  reloads for `framesInFlight` frames. Texture requests may provide either an
  externally owned `SamplerHandle` or a `SamplerDesc`; when constructed with a
  `RHI::SamplerManager`, the cache owns the deduplicated sampler lease for the
  lifetime of the texture lease.

## Texture residency and fallback policy

- `GpuTextureRequest` owns the promoted graphics-side texture upload seam:
  `AssetId`, CPU bytes, `TextureDesc`, and sampler policy. The cache allocates
  a `TextureLease`, queues transfer work, publishes a bindless index when a
  sampler is available, and transitions to `Ready` after `Tick()` observes the
  transfer token complete.
- `InitializeFallbackTexture()` installs one deterministic sampled fallback
  texture. `GetViewOrFallback()` returns the requested ready view when present,
  or the fallback view for missing, pending, or failed texture assets while
  reporting the `GpuAssetFallbackReason`.
- `GpuAssetCacheDiagnostics` records upload counts, texture/sampler allocation
  failures, fallback hits/misses, retire-queue size, and the explicit
  non-evicting cache policy used for this first residency slice.
- The cache does not evict ready assets yet. Capacity/eviction work must be a
  later semantic task; callers can still observe deterministic diagnostics and
  retire-queue behavior today.
- `MaterialSystem::ResolveTextureAssetBindings()` consumes data-only
  `MaterialTextureAssetBindings` (`AssetId` slots) and resolves them through the
  cache into `MaterialParams` bindless indices, using the fallback texture for
  missing/pending/failed assets when available. Runtime owns producer sidecars;
  graphics only consumes IDs and cache views.

### Per `GRAPHICS-015Q`

- **Capacity / eviction.** The cache stays explicitly non-evicting in this
  contract slice; capacity introspection comes from
  `GpuAssetCacheDiagnostics` (`TrackedAssets`, `PendingRetireRecords`,
  `NonEvictingCache = true`). Bounded eviction is a separate semantic task
  that must extend the existing diagnostics, route evicted leases through
  the same frame-anchored retire queue with
  `retireDeadline = currentFrame + framesInFlight` so renderer snapshots
  remain valid for at least `framesInFlight` frames after eviction
  (mirroring the existing `NotifyReloaded` retirement), refuse to evict the
  fallback texture lease, and prefer a priority + LRU pair over pure LRU
  so runtime/editor can pin critical material textures.
- **Streaming mip / reupload.** Partial-mip streaming reuses
  `RHI::TextureManager::Reupload()` to preserve the lease's existing
  `RHI::TextureHandle`, bindless index, and sampler binding when the
  destination `TextureDesc` (extent, format, mip count, usage) is
  unchanged. Full lease replacement through
  `RequestUpload(GpuTextureRequest)` is reserved for format / extent /
  mip-count / usage changes and hot-reload swaps to a distinct asset
  version. A future `RequestStreamingReupload(AssetId, MipRange,
  std::span<const std::byte>)` seam will validate the lease is `Ready`,
  forward to `TextureManager::Reupload()`, and increment a
  `StreamingMipUploads` counter on `GpuAssetCacheDiagnostics` analogous
  to `TextureUploadRequests`. Until that seam lands, runtime bridges
  emit `NotifyReloaded` followed by `RequestUpload`, accepting the
  bindless rebind and one retire-queue entry per stream step.
- **Fallback texture content.** A single deterministic fallback texture
  covers every sampled material texture slot (`Albedo`, `Normal`,
  `MetallicRoughness`, `Emissive`). The fallback is a 4x4
  magenta-and-black checkerboard (`RGBA8_UNORM`, alpha `0xFF`, nearest
  filter, clamp-to-edge addressing); `GetViewOrFallback()` returns it
  with `UsedFallback = true` and the matching
  `GpuAssetFallbackReason` for missing / pending / failed assets so the
  magenta checker is visually obvious in development builds. Per-channel
  "neutral" interpretation is enforced by material shader code that
  observes the resolved `UsedFallback` bit, not by allocating per-slot
  fallback textures: `Normal` reverts to a flat `(0.5, 0.5, 1.0)`
  tangent normal, `MetallicRoughness` reverts to per-material
  `MetallicFactor`/`RoughnessFactor` scalars (treated as `metallic = 0`,
  `roughness = 1` when factors are absent), and `Emissive` is multiplied
  by per-material `EmissiveFactor` defaulting to `0.0` so unbound
  emissive assets do not silently glow. Visualization and Htex/UV bake
  atlas references do **not** use this fallback: per `GRAPHICS-014Q`
  visualization atlas descriptors with deferred residency are dropped
  from `RenderWorld::Visualization` and counted in
  `VisualizationDiagnostics::TextureResidencyDeferredCount`. If
  `InitializeFallbackTexture()` itself fails (for example
  `OutOfDeviceMemory`), the cache leaves
  `GpuAssetCacheDiagnostics::FallbackTextureReady = false` and
  `GetViewOrFallback()` returns
  `GpuAssetFallbackReason::Unavailable`, letting material code fall back
  to factor-only shading deterministically.
- **Bindless descriptor flush cadence.** Bindless texture descriptor
  writes are coalesced per frame: the backend records all bindless slot
  writes produced during the frame's `IRenderer::PrepareFrame`/`Record`
  window and drains them as a single descriptor batch at the start of
  the next frame's `BeginFrame()`, mirroring the `Picking.Readback`
  drain pattern from `GRAPHICS-012Q` and the histogram readback drain
  from `GRAPHICS-013AQ`. Sampler creation is deduplicated through
  `RHI::SamplerManager`; sampler changes detected through a different
  `SamplerDesc` on the next `RequestUpload` trigger a coalesced
  bindless rewrite of the affected lease's descriptor in the same
  per-frame batch and increment a `BindlessDescriptorRewrites` counter
  on `GpuAssetCacheDiagnostics`. Material slot updates that swap an
  `AssetId` flow through `MaterialSystem::ResolveTextureAssetBindings()`
  and write the resolved `BindlessIndex` into `MaterialParams` without
  forcing a separate descriptor flush, because bindless indices are
  retained-stable per lease for the lease's `Ready` lifetime.
  Stale-bindless-index hazards on hot reload are prevented by the
  existing frame-anchored retire queue holding the descriptor live for
  `framesInFlight` frames after retirement; no additional fence,
  semaphore, or graphics-side synchronization is introduced. Concrete
  `VkDescriptorSet` layout and heap write batching remain backend-local
  under `src/graphics/vulkan` and never leak through RHI or renderer
  module surfaces.
- **Runtime ownership.** Runtime owns both fallback initialization and
  upload scheduling. `Runtime.Engine::Initialize()` calls
  `cache.InitializeFallbackTexture(fallbackDesc)` exactly once
  immediately after constructing the cache (RUNTIME-070), sourcing the
  fallback bytes from a runtime-baked compiled-in `constexpr` byte
  array (4×4 `RGBA8_UNORM` magenta-and-black checkerboard, alpha
  `0xFF`); the cache only consumes the `std::span<const std::byte>`.
  The call is gated on `m_Device->IsOperational()`, so the Null backend
  leaves `FallbackTextureReady = false` deterministically. Engine
  passes the renderer's `SamplerManager` into the cache constructor so
  the fallback's nearest/clamp-to-edge sampler descriptor resolves
  through the deduplicated manager path; the cache never reads files.
  `Extrinsic.Runtime.AssetModelTextureHandoff` subscribes to
  `AssetEvent::Ready` events on `AssetService::SubscribeAll`, reads promoted
  `AssetTexture2DPayload` records, maps supported CPU formats to
  `TextureDesc`, constructs `GpuTextureRequest` (`AssetId`, `Bytes` span,
  `TextureDesc`, sampler descriptor), and calls `cache.RequestUpload(req)`
  synchronously from the asset-event handler thread. RGB8 and unknown texture
  payload formats fail closed before a graphics upload is requested. Heavy CPU
  decoding may be queued through `Extrinsic.Runtime.StreamingExecutor` (the
  same async surface used for visualization baking under `GRAPHICS-014Q`), but
  the final `RequestUpload` call is always synchronous from runtime; graphics
  never schedules CPU work and never imports `AssetService` or `AssetEventBus`.
  `AssetEvent::Destroyed` flows to `cache.NotifyDestroyed(id)` which queues
  live leases for retirement.
  Editor / app code may expose per-asset upload priority hints through
  future runtime APIs, but the cache currently has no priority queue
  and graphics never receives priority data.

## Layering

This subdirectory is part of the `graphics/*` layer. Per `AGENTS.md` §2 it
may depend on:

- `Extrinsic.Core.*`
- `Extrinsic.Asset.Registry` — for the `AssetId` type only. The cache does
  not call `AssetRegistry` or `AssetService` methods.
- `Extrinsic.RHI.*`

It must **not** depend on:

- `Extrinsic.ECS.*` (no live ECS knowledge).
- `Extrinsic.Runtime.*`.
- `Extrinsic.Graphics.Vulkan.*` (RHI-only consumer).
- `Extrinsic.Asset.Service` / `Extrinsic.Asset.EventBus` (event subscription
  is performed by `Runtime`; the cache exposes plain notify methods).

## Wiring

`Runtime.Engine` constructs the cache after the renderer is initialized
(passing the renderer's `BufferManager`, `TextureManager`, and
`SamplerManager` plus the device's `TransferQueue`), invokes
`InitializeFallbackTexture(...)` once on the operational path
(RUNTIME-070), and subscribes to `AssetEventBus` via
`AssetService::SubscribeAll`, mapping events to `NotifyFailed` /
`NotifyReloaded` / `NotifyDestroyed`. `AssetEvent::Ready` is
intentionally not handled by the cache itself. Runtime-owned type-specific
bridges are responsible for calling `RequestUpload` once their CPU payload is
final; `Extrinsic.Runtime.AssetModelTextureHandoff` covers decoded texture
payloads, while model-scene ECS/material handoff remains deferred.

`AssetHooks::TickAssets()` calls `cache.Tick(device.GetGlobalFrameNumber(),
device.GetFramesInFlight())` once per frame, after `AssetService::Tick()`.

## Tests

`tests/unit/graphics/Test.GpuAssetCache.cpp`, target
`IntrinsicGraphicsAssetsUnitTests`, labels `unit graphics`. The target
runs in the default CPU correctness gate
(`-LE 'gpu|vulkan|slow|flaky-quarantine'`) — the cache is exercised via
`MockDevice` from `tests/support/MockRHI.hpp` and never touches a real
GPU.
