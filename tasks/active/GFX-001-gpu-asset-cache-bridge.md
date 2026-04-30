# GFX-001: Graphics.GpuAssetCache bridge

## Status

active; in progress on branch `claude/review-todays-commits-ZodRG`.

## Goal

Introduce a graphics-owned side table that maps `Assets::AssetId` to GPU
resources (buffers/textures) without leaking GPU handles into
`Asset.Registry`. The cache owns the AssetId → GPU view mapping, drives the
upload state machine, and preserves old GPU views across hot reloads for at
least `framesInFlight` frames so renderer snapshots captured in
`ExtractRenderWorld()` do not observe a torn handle.

A placeholder for this work already exists in
`src/runtime/Runtime.Engine.cpp:455`:

> `(void)completedGpuValue; // placeholder until GpuAssetCache / deferred-delete lands`

## Non-goals

- Asset-type-specific loaders (mesh import, texture decode). These remain
  with the existing CPU pipeline. The cache is byte-blob agnostic — callers
  hand it `BufferDesc` + `std::span<const std::byte>` (or `TextureDesc` +
  bytes) and an `AssetId`.
- ECS component refactors. ECS components already store `AssetId`-shaped
  data; widening or strong-typing those fields is tracked separately.
- Replacing `Renderer::GetBufferManager()` / `GetTextureManager()` ownership.
- Streaming mip uploads / partial uploads (`TextureManager::Reupload`).
- Removing `src/legacy/`.

## Layering

New owning directory `src/graphics/assets/`:

- Module: `Extrinsic.Graphics.GpuAssetCache`
- Library: `ExtrinsicGraphicsAssets`
- Allowed dependencies (per AGENTS.md §2):
  - `Extrinsic.Core.Error`
  - `Extrinsic.Asset.Registry` — for the `AssetId` type only (no live Registry traffic)
  - `Extrinsic.RHI.Handles` / `Descriptors` / `BufferManager` / `TextureManager` /
    `TransferQueue` / `Bindless`
- Disallowed: ECS, Runtime, Platform, Vulkan backend, AssetService.

Runtime is the only layer that wires `AssetEventBus` -> `GpuAssetCache`.

## State machine

```
NotRequested  -- Reserve(id) -->        CpuPending
NotRequested  -- RequestUpload -->      GpuUploading
CpuPending    -- RequestUpload -->      GpuUploading
GpuUploading  -- transfer complete -->  Ready
GpuUploading  -- create/queue fail -->  Failed
Ready         -- RequestUpload -->      GpuUploading  (old lease moved to retire queue)
Ready         -- NotifyReloaded -->     CpuPending    (old lease retained)
Failed        -- RequestUpload -->      GpuUploading
*             -- NotifyDestroyed -->    NotRequested  (entry removed; lease retired)
```

`NotifyFailed(id)` forces an entry into `Failed` regardless of prior state
(used when the CPU pipeline reports a load failure for an asset that was
never even reserved).

## Old-view lifetime preservation

Each entry holds a `BufferLease` or `TextureLease`. On hot reload the old
lease is moved into a retire queue keyed by
`retireFrame = currentFrame + framesInFlight`. `Tick(currentFrame, …)` drops
retire-queue entries whose `retireFrame <= currentFrame`. The lease's
ref-count decrement happens on the render thread, matching the
`BufferManager` thread-safety contract.

A renderer that captured a `GpuAssetView` in a previous
`ExtractRenderWorld()` frame can continue to dereference its bindless index
and handle for at least `framesInFlight` frames after a reload — the
`Lease`-driven refcount keeps the underlying GPU resource alive.

## Public API sketch

```cpp
namespace Extrinsic::Graphics
{
    enum class GpuAssetState : std::uint8_t {
        NotRequested, CpuPending, GpuUploading, Ready, Failed
    };

    struct GpuAssetView {
        RHI::BufferHandle  Buffer{};
        RHI::TextureHandle Texture{};
        RHI::BindlessIndex BindlessIdx{0};
        std::uint64_t      Generation{0};
    };

    struct GpuBufferRequest  { Assets::AssetId Id; std::span<const std::byte> Bytes;
                               RHI::BufferDesc  Desc; };
    struct GpuTextureRequest { Assets::AssetId Id; std::span<const std::byte> Bytes;
                               RHI::TextureDesc Desc; RHI::SamplerHandle Sampler{}; };

    class GpuAssetCache {
    public:
        GpuAssetCache(RHI::BufferManager&, RHI::TextureManager&, RHI::ITransferQueue&);
        ~GpuAssetCache();

        Core::Result RequestUpload(const GpuBufferRequest&);
        Core::Result RequestUpload(const GpuTextureRequest&);
        void Reserve(Assets::AssetId);                 // CpuPending
        void NotifyFailed(Assets::AssetId);            // -> Failed
        void NotifyReloaded(Assets::AssetId);          // Ready -> CpuPending, retain old lease
        void NotifyDestroyed(Assets::AssetId);         // -> NotRequested, retire lease
        void Tick(std::uint64_t currentFrame, std::uint32_t framesInFlight);

        [[nodiscard]] GpuAssetState                GetState(Assets::AssetId) const;
        [[nodiscard]] Core::Expected<GpuAssetView> GetView(Assets::AssetId)  const;

        [[nodiscard]] std::size_t TrackedCount()       const;
        [[nodiscard]] std::size_t PendingRetireCount() const;
    };
}
```

## Runtime wiring

`Runtime.Engine.cpp`:

1. After `m_Renderer->Initialize(*m_Device)`, construct the cache as a
   member of Engine using
   `m_Renderer->GetBufferManager()`, `m_Renderer->GetTextureManager()`,
   `m_Device->GetTransferQueue()`.
2. Subscribe to `m_AssetService->SubscribeAll(...)`. Map events:
   - `AssetEvent::Failed`    -> `NotifyFailed`
   - `AssetEvent::Reloaded`  -> `NotifyReloaded`
   - `AssetEvent::Destroyed` -> `NotifyDestroyed`
   - `AssetEvent::Ready`     -> no-op for the cache itself (CPU readiness is
     surfaced by type-specific bridges that call `RequestUpload`).
3. Extend `AssetHooks::TickAssets()` to call
   `gpuAssetCache.Tick(device.GetGlobalFrameNumber(), device.GetFramesInFlight())`
   after `AssetService::Tick()`.
4. In `ShutdownHooks::DestroyAssets()`, unsubscribe and destroy the cache
   before tearing down the renderer (lease lifetime requirement).

The placeholder comment at line 455 is removed.

## Tests

Location: `tests/unit/graphics/Test_GpuAssetCache.cpp`, linked into the
existing `IntrinsicGraphicsUnitTests` target. Uses `MockDevice` from
`tests/support/MockRHI.hpp`. The mock's `GetGlobalFrameNumber` is read in
each `Tick(frame, fif)` call we make explicitly, so no mock change is
required.

Required cases:

1. **Request → Ready** (buffer): `RequestUpload` transitions to
   `GpuUploading`; `Tick` once with the mock's "always complete" transfer
   queue advances to `Ready` with a non-zero generation.
2. **Request → Ready** (texture): same, plus `BindlessIdx` is non-zero
   when a sampler was provided.
3. **Failed**: `MockDevice::FailNextBufferCreate = true` ⇒
   `RequestUpload` returns an error and the entry is `Failed`.
   `NotifyFailed(id)` from a `NotRequested` state creates a `Failed` entry.
4. **Hot reload** (`RequestUpload` on a `Ready` asset):
   - the new upload lands in `GpuUploading`,
   - the old lease is in the retire queue,
   - `Generation` of the new view > old.
5. **Old-view lifetime preservation**: capture the old `GpuAssetView`,
   `RequestUpload` again, `Tick(frame, fif=2)`. The old buffer's
   `DestroyBufferCount` is unchanged until at least `fif` frames have
   advanced; on the `fif`-th tick the count increments by one.
6. **Reload event**: `NotifyReloaded` on a `Ready` entry transitions it to
   `CpuPending` and retains the lease. A subsequent `RequestUpload`
   produces a new generation and retires the lease normally.
7. **Destroy**: `NotifyDestroyed` removes the entry (state goes back to
   `NotRequested`) and queues the lease for retirement.

## Acceptance

- `IntrinsicGraphicsUnitTests` builds and runs `Test_GpuAssetCache.*`
  cases; all pass.
- `IntrinsicTests` builds.
- `tasks/active/GFX-001-gpu-asset-cache-bridge.md` is moved to
  `tasks/done/` after acceptance.
- `docs/architecture/layering.md` and `docs/api/generated/module_inventory.md`
  reflect the new `src/graphics/assets/` directory.
- AGENTS.md §3 source-tree map is extended with `src/graphics/assets/`.

## Out of scope / follow-ups

- ECS asset components (`ECS.Component.AssetInstance`) currently store a
  raw `uint32_t`; tightening to `Core::StrongHandle<AssetTag>` is a
  separate task.
- A type-specific MeshAssetBridge / TextureAssetBridge that converts a
  payload-store `Ready` event into `GpuAssetCache::RequestUpload(...)` is
  also a follow-up — it requires per-type knowledge of the CPU layout
  (vertex format, mip count) which the cache deliberately does not know.
