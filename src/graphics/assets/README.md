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
and subscribes to `AssetEventBus` via `AssetService::SubscribeAll`,
mapping events to `NotifyFailed` / `NotifyReloaded` / `NotifyDestroyed`.
`AssetEvent::Ready` is intentionally not handled by the cache itself —
type-specific bridges (mesh / texture) are responsible for calling
`RequestUpload` once their CPU payload is final.

`AssetHooks::TickAssets()` calls `cache.Tick(device.GetGlobalFrameNumber(),
device.GetFramesInFlight())` once per frame, after `AssetService::Tick()`.

## Tests

`tests/unit/graphics/Test.GpuAssetCache.cpp`, target
`IntrinsicGraphicsAssetsUnitTests`, labels `unit graphics`. The target
runs in the default CPU correctness gate
(`-LE 'gpu|vulkan|slow|flaky-quarantine'`) — the cache is exercised via
`MockDevice` from `tests/support/MockRHI.hpp` and never touches a real
GPU.
