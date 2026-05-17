# ADR 0016 — Texture Residency, Fallback, and Asset Cache Policy

- **Status:** Accepted
- **Date:** 2026-05-17
- **Owners:** Graphics (`Graphics.GpuAssetCache`, fallback texture, bindless write batching), Runtime composition (fallback initialization, upload scheduling, `Extrinsic.Runtime.AssetBridges.Texture`)
- **Related tasks:** [`tasks/done/GRAPHICS-015`](../../tasks/done/GRAPHICS-015-gpu-assets-textures-residency.md), [`GRAPHICS-015Q`](../../tasks/done/GRAPHICS-015Q-texture-residency-backend-clarifications.md)
- **Related docs:** [`docs/architecture/graphics.md`](../architecture/graphics.md), [`docs/architecture/rendering-three-pass.md`](../architecture/rendering-three-pass.md), [`src/graphics/renderer/README.md`](../../src/graphics/renderer/README.md), [`src/graphics/assets/README.md`](../../src/graphics/assets/README.md)
- **Supersedes:** none. Extracted from the `## Graphics asset residency` GRAPHICS-015Q clarification paragraph in `docs/architecture/graphics.md` per [`DOCS-001`](../../tasks/active/DOCS-001-reduce-graphics-architecture-prose.md).
- **Related ADRs:** [ADR-0007](0007-picking-selection-and-outline.md) records the `Picking.Readback` drain pattern reused for bindless descriptor write coalescing in §6. [ADR-0010](0010-postprocess-chain-backend-policy.md) records the histogram readback drain that uses the same pattern. [ADR-0009](0009-visualization-packets-and-overlay-upload.md) records the visualization-atlas residency-deferral policy referenced in §5.

## Context

`GRAPHICS-015` established `Graphics.GpuAssetCache` as the promoted graphics-owned cache that maps `Assets::AssetId` values to GPU buffer / texture leases (`GpuTextureRequest`, `InitializeFallbackTexture()`, `GetViewOrFallback()`, `GpuAssetCacheDiagnostics`). The cache is **explicitly non-evicting** in that slice, and the snapshot / view immutability guarantee from `GRAPHICS-002` means immutable renderer snapshots must continue to see valid views for at least `framesInFlight` frames after any lifecycle event.

`GRAPHICS-015Q` answered six producer-side / backend-side questions that `GRAPHICS-015` deferred:

1. What is the future capacity / eviction policy, and how must it preserve snapshot immutability?
2. How does streaming-mip / partial-reupload behavior interact with `RHI::TextureHandle` / bindless index / sampler stability?
3. What does the fallback texture look like, and how do material shaders consume the `UsedFallback` bit per channel?
4. What happens when fallback initialization itself fails?
5. How are bindless texture descriptor writes batched, and how do sampler-only changes interact with bindless re-writes?
6. Who owns fallback initialization, upload scheduling, and `AssetEvent::*` subscription?

This ADR captures those answers as the canonical durable home. `docs/architecture/graphics.md` keeps the short canonical bullets (cache surface, `GpuTextureRequest`, fallback resolution, non-evicting policy, `GpuAssetCacheDiagnostics`) and retains a single pointer line to this ADR for the GRAPHICS-015Q follow-up policies.

## Decision

### 1. Cache capacity / eviction policy

The `GpuAssetCache` stays **explicitly non-evicting** in the `GRAPHICS-015` contract slice. Capacity introspection happens through the existing `GpuAssetCacheDiagnostics` fields:

- `TrackedAssets`.
- `PendingRetireRecords`.
- `NonEvictingCache = true`.

No budget, pressure signal, or LRU / priority queue is added by this ADR. When bounded eviction is added later as a separate semantic task (**not** `GRAPHICS-015Q`), it must:

1. **Extend** the existing diagnostics rather than replace them.
2. Move evicted leases through the **same** frame-anchored retire queue with `retireDeadline = currentFrame + framesInFlight` so renderer snapshots dereferencing those views stay live for at least `framesInFlight` frames after eviction (mirroring the existing `NotifyReloaded` retirement semantics).
3. **Refuse** to evict the fallback texture lease.
4. Prefer a **priority + LRU pair** over pure LRU so runtime / editor can pin critical material textures.

This keeps the snapshot / view immutability guarantee from `GRAPHICS-002` intact and keeps the contract additive.

### 2. Streaming mip / reupload behavior

Partial-mip streaming reuses the existing `RHI::TextureManager::Reupload()` path, which **preserves**:

- The existing `RHI::TextureHandle`.
- The bindless index.
- The sampler binding of the lease.

`RequestUpload(GpuTextureRequest)` is reserved for **full lease replacement**:

- Format / extent / mip-count / usage changes.
- Hot-reload swaps to a distinct asset version.

It always allocates a new lease and retires the previous one on the frame-anchored retire queue.

A future seam:

```
RequestStreamingReupload(AssetId, MipRange, std::span<const std::byte>)
```

will validate the lease is `Ready` and forward to `TextureManager::Reupload()` while incrementing a `StreamingMipUploads` counter on `GpuAssetCacheDiagnostics`. The destination `TextureDesc` (extent, format, mip count, usage) must be unchanged.

### 3. Single deterministic fallback texture; shader-side neutrality

A **single** deterministic 4×4 magenta-and-black checkerboard fallback texture covers every sampled material texture slot:

- Format: `RGBA8_UNORM`.
- Alpha: `0xFF`.
- Filter: nearest.
- Addressing: clamp-to-edge.
- Slots: `Albedo`, `Normal`, `MetallicRoughness`, `Emissive`.

**Per-channel "neutral" interpretation is enforced by material shader code** that observes the resolved `UsedFallback` bit:

- `Normal` reverts to a flat `(0.5, 0.5, 1.0)` tangent normal.
- `MetallicRoughness` reverts to per-material `MetallicFactor` / `RoughnessFactor` scalars, treated as `metallic = 0`, `roughness = 1` when factors are absent.
- `Emissive` is multiplied by per-material `EmissiveFactor` defaulting to `0.0` so unbound emissive assets do not silently glow.

**Visualization and Htex / UV bake atlas references do not use the magenta fallback.** Per [ADR-0009](0009-visualization-packets-and-overlay-upload.md), visualization atlas descriptors with deferred residency are dropped from `RenderWorld::Visualization` and counted in `VisualizationDiagnostics::TextureResidencyDeferredCount`.

### 4. Fallback initialization failure

If `InitializeFallbackTexture()` itself fails, the cache reports:

- `FallbackTextureReady = false`.
- `GetViewOrFallback()` returns `GpuAssetFallbackReason::Unavailable`.

Material code can fall back to factor-only shading deterministically. There is no implicit second-chance retry; runtime owns whether and when to call `InitializeFallbackTexture(...)` again.

### 5. Bindless descriptor write coalescing and sampler dedup

Bindless texture descriptor writes are **coalesced per frame**:

- The backend records all bindless slot writes produced during the frame's `IRenderer::PrepareFrame` / `Record` window.
- The backend drains them as a **single descriptor batch** at the start of the next frame's `BeginFrame()`.
- This mirrors the `Picking.Readback` drain pattern from [ADR-0007](0007-picking-selection-and-outline.md) §2 and the histogram readback drain from [ADR-0010](0010-postprocess-chain-backend-policy.md) §2.

Sampler creation is deduplicated through `RHI::SamplerManager`. Sampler changes detected through a different `SamplerDesc` on the next `RequestUpload`:

- Trigger a coalesced bindless re-write of the lease's descriptor in the same per-frame batch.
- Increment a `BindlessDescriptorRewrites` counter on `GpuAssetCacheDiagnostics`.

Material slot updates that swap an `AssetId` flow through `MaterialSystem::ResolveTextureAssetBindings()` and write the resolved `BindlessIndex` into `MaterialParams` **without** forcing a separate descriptor flush — bindless indices are retained-stable per lease for the lease's `Ready` lifetime.

**Stale-bindless-index hazards on hot reload** are prevented by the existing frame-anchored retire queue holding the descriptor live for `framesInFlight` frames after retirement.

Concrete `VkDescriptorSet` layout and heap write batching remain backend-local under `src/graphics/vulkan`.

### 6. Runtime ownership of fallback initialization, upload scheduling, and asset events

Runtime owns **both** fallback initialization and upload scheduling.

**Fallback initialization:**

- `Runtime.Engine` (or a runtime-side graphics-bootstrap step) calls `cache.InitializeFallbackTexture(fallbackDesc)` **exactly once** after the cache is constructed and **before** any runtime asset bridge issues `RequestUpload(GpuTextureRequest)`.
- The fallback bytes come from a baked engine resource owned by the runtime layer — compiled-in byte array or runtime-loaded engine asset, **not** a graphics-layer file read.
- The cache only consumes the `std::span<const std::byte>`.

**Texture-typed asset bridges** live in the planned umbrella module `Extrinsic.Runtime.AssetBridges.Texture`, mirroring:

- The `Extrinsic.Runtime.SpatialDebugAdapters` pattern from [ADR-0008](0008-spatial-debug-visualizer-adapters.md).
- The `Extrinsic.Runtime.VisualizationAdapters` pattern from [ADR-0009](0009-visualization-packets-and-overlay-upload.md).

Bridges:

1. Subscribe to texture-typed `AssetEvent::Ready` events on `AssetService::SubscribeAll`.
2. Read the decoded CPU payload.
3. Construct a `GpuTextureRequest`.
4. Call `cache.RequestUpload(req)` **synchronously** from the asset-event handler thread.

Heavy CPU decoding may be queued through `Extrinsic.Runtime.StreamingExecutor`, but the final `RequestUpload` call is always synchronous from runtime, and graphics never imports `AssetService` or `AssetEventBus`.

`AssetEvent::Destroyed` flows to `cache.NotifyDestroyed(id)` which queues live leases for retirement. Graphics never schedules CPU work, never reads priority data, and never owns asset event subscription.

## Consequences

Positive:

- The non-evicting policy is explicit and tracked through `GpuAssetCacheDiagnostics`; bounded eviction is a separately scoped semantic task with a known shape.
- Streaming-mip reupload preserves the bindless index and sampler binding, so material slot writes do not need a flush on partial updates.
- A single magenta-and-black fallback covers every sampled slot; per-channel shader-side neutrality (flat normal, factor-only metallic / roughness, zero-default emissive) means materials degrade gracefully without unbound textures silently shading the scene.
- Bindless write coalescing reuses the established `Picking.Readback` / histogram drain pattern, so backend reviewers have one shape to validate.
- Runtime owns asset-event subscription and fallback initialization, preserving `graphics → no AssetService / AssetEventBus` imports.

Trade-offs and risks:

- The cache cannot evict. A scene that loads textures monotonically will grow `TrackedAssets` without bound until bounded eviction lands as its own task; reviewers must reject any quiet eviction shortcut that does not preserve §1's four constraints.
- Bindless descriptor writes drain at the next `BeginFrame()`, so a slot update issued during frame `N` is not observable to the GPU until frame `N+1`. This is the explicit safety property (snapshot immutability) but means a runtime that expects "write now, read this frame" semantics must instead wait one frame.
- `RequestUpload(GpuTextureRequest)` always allocates a new lease and retires the previous one on the retire queue. Hot-reload loops that swap an asset every frame will keep up to `framesInFlight` copies alive — intentional, but worth noting under memory pressure.
- The fallback magenta-and-black checkerboard is a deliberate "this is wrong" signal in editors. Shipping builds that want a less alarming fallback must replace it through `InitializeFallbackTexture(fallbackDesc)` (still subject to the §3 per-channel neutrality contract on the material side).
- Sampler dedup via `RHI::SamplerManager` means two material slots requesting different samplers for the same texture get separate bindless re-writes; reviewers must check that material code does not accidentally request a near-duplicate sampler that differs only by ignorable bits.

Follow-up tasks required: none from this ADR. Bounded eviction, `RequestStreamingReupload(...)`, and the concrete `Extrinsic.Runtime.AssetBridges.Texture` module land under their own task IDs.

## Alternatives Considered

- **Bounded eviction in this slice.** Rejected per §1: would require a new diagnostics surface, a priority / LRU policy decision, and a snapshot-immutability proof that exceeds this slice's scope.
- **`RequestUpload(GpuTextureRequest)` reused for partial-mip updates.** Rejected per §2: would force every partial update to allocate a new lease and invalidate the bindless index, defeating streaming.
- **Per-slot fallback textures (separate albedo / normal / emissive fallbacks).** Rejected per §3: triples the fallback inventory and forces material shaders to switch on slot identity; one magenta-and-black checkerboard plus per-channel shader-side neutrality covers every slot.
- **Implicit second-chance fallback re-initialization.** Rejected per §4: would hide a real failure mode and let material code shade with an undefined-state texture; runtime owns whether and when to retry.
- **Per-write descriptor flushes.** Rejected per §5: would force one descriptor write per slot update, regress frame cost, and break the snapshot-immutability invariant that prevents stale-index hazards.
- **Graphics-side `AssetService` subscription.** Rejected per §6: graphics must not import `AssetService` or `AssetEventBus`; the runtime bridge is the seam.

## Validation

- [`tasks/done/GRAPHICS-015`](../../tasks/done/GRAPHICS-015-gpu-assets-textures-residency.md) records the underlying `Graphics.GpuAssetCache` contract, `GpuTextureRequest`, `InitializeFallbackTexture` / `GetViewOrFallback`, and `GpuAssetCacheDiagnostics` field set.
- [`tasks/done/GRAPHICS-015Q`](../../tasks/done/GRAPHICS-015Q-texture-residency-backend-clarifications.md) records the six clarification decisions captured in §§1–6.
- `docs/architecture/rendering-three-pass.md` carries the matching "Per `GRAPHICS-015Q`" paragraph next to the existing `GRAPHICS-014Q` paragraph.
- `src/graphics/renderer/README.md` carries the matching `MaterialSystem` / `GpuAssetCache` ownership-contract bullet listing the five visible decisions.
- `src/graphics/assets/README.md` carries the matching "Per `GRAPHICS-015Q`" subsection under "Texture residency and fallback policy".
- The default CPU correctness gate (`ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`) exercises `GpuAssetCache` reservation / upload / retire flows, the fallback resolution, and the `GpuAssetCacheDiagnostics` counter set without requiring a Vulkan device.
