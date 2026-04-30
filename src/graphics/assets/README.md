# `src/graphics/assets/`

Graphics-owned bridge between `Assets::AssetId` and GPU resources.

## Module

- `Extrinsic.Graphics.GpuAssetCache` — side table mapping `AssetId` to a
  refcounted `BufferLease` / `TextureLease`, with a small state machine
  (`NotRequested`, `CpuPending`, `GpuUploading`, `Ready`, `Failed`) and a
  frame-anchored retire queue that preserves old GPU views across hot
  reloads for `framesInFlight` frames.

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
