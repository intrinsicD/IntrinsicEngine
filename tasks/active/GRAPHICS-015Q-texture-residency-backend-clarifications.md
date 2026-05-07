# GRAPHICS-015Q — Texture residency backend clarification follow-ups

## Status
- State: in-progress (docs-only clarification slice).
- Owner/agent: local agent workflow.
- Activated: 2026-05-07 after `GRAPHICS-014Q` retirement cleared `tasks/active/`.
- Branch: `claude/agentic-workflow-session-xssSg`.
- Promotion commit: `499e0f2` (move file from `tasks/backlog/rendering/` to `tasks/active/`).
- Implementation commit: pending (this commit records the decisions below and syncs the consequential notes into `docs/architecture/graphics.md`, `docs/architecture/rendering-three-pass.md`, `src/graphics/renderer/README.md`, and `src/graphics/assets/README.md`).
- Resolution: decisions recorded below and consequential notes synced into the four documents listed above. The rendering backlog README entry for `GRAPHICS-015Q` is redirected from the in-place backlog path to the `tasks/active/` path by this implementation commit and will be redirected to `tasks/done/` by the retirement commit.
- Next verification step: run `python3 tools/agents/check_task_policy.py --root . --strict` and `python3 tools/docs/check_doc_links.py --root .` before retiring the task to `tasks/done/`.

## Decisions

- **Cache capacity / eviction policy.** The `GpuAssetCache` stays
  explicitly non-evicting in the `GRAPHICS-015` contract slice. Capacity
  introspection happens through the existing
  `GpuAssetCacheDiagnostics` fields (`TrackedAssets`,
  `PendingRetireRecords`, `NonEvictingCache = true`); no budget,
  pressure signal, or LRU/priority queue is added by this clarification.
  When bounded eviction is added later as a separate semantic task
  (not `GRAPHICS-015Q`), it must (a) extend the existing diagnostics
  rather than replace them, (b) move evicted leases through the same
  frame-anchored retire queue with `retireDeadline = currentFrame +
  framesInFlight` so renderer snapshots dereferencing those views stay
  live for at least `framesInFlight` frames after eviction (mirroring
  the existing `NotifyReloaded` retirement semantics), (c) refuse to
  evict the fallback texture lease, and (d) prefer a priority + LRU
  pair over pure LRU so runtime/editor can pin critical material
  textures. This keeps the snapshot/view immutability guarantee from
  `GRAPHICS-002` intact and keeps the contract additive.
- **Streaming mip / reupload behavior.** Partial-mip streaming reuses
  the existing `RHI::TextureManager::Reupload()` path, which preserves
  the existing `RHI::TextureHandle`, bindless index, and sampler
  binding of the lease. `RequestUpload(GpuTextureRequest)` is reserved
  for full lease replacement (format / extent / mip-count / usage
  changes, or hot-reload swaps to a distinct asset version) and
  always allocates a new lease and retires the previous one on the
  frame-anchored retire queue. The cache exposes a future
  `RequestStreamingReupload(AssetId, MipRange, std::span<const std::byte>)`
  seam (tracked as a follow-up implementation task, not implemented
  here) that validates the lease is `Ready`, asserts the destination
  mip slice fits the existing `TextureDesc` without changing extent /
  format / mip count / usage, forwards to `TextureManager::Reupload()`,
  and increments a new `StreamingMipUploads` counter on
  `GpuAssetCacheDiagnostics` analogous to `TextureUploadRequests`.
  Until that seam lands, runtime bridges that need progressive
  residency emit `NotifyReloaded` followed by `RequestUpload`, paying
  the bindless rebind and one retire-queue entry per stream step. No
  new shader-visible state, descriptor surface, or `MaterialParams`
  field is introduced.
- **Fallback texture content policy.** A single deterministic fallback
  texture covers every sampled material texture slot (`Albedo`,
  `Normal`, `MetallicRoughness`, `Emissive`) and any future
  material-sampled `AssetId` reference. The fallback is a 4x4
  magenta-and-black checkerboard (RGBA8_UNORM, alpha 0xFF, nearest
  filter, clamp-to-edge addressing) initialized through
  `InitializeFallbackTexture()`; `GetViewOrFallback()` returns the
  fallback view with `UsedFallback = true` and the matching
  `GpuAssetFallbackReason` for missing / pending / failed assets so
  the magenta checker appears in development builds wherever a
  texture asset is unbound. Per-channel "neutral" interpretation is
  enforced by material shader code that observes the resolved
  `UsedFallback` bit (or the equivalent fallback flag carried into
  `MaterialParams`), not by allocating per-slot fallback textures:
  `Normal` reverts to the implicit `(0.5, 0.5, 1.0)` flat tangent
  normal, `MetallicRoughness` reverts to the per-material
  `MetallicFactor`/`RoughnessFactor` scalars (treated as
  `metallic = 0`, `roughness = 1` when factors are absent), and
  `Emissive` is multiplied by the per-material `EmissiveFactor` which
  defaults to `0.0` so unbound emissive assets do not silently glow
  in production. Visualization and Htex/UV bake atlas references do
  **not** use the GpuAssetCache magenta fallback: per `GRAPHICS-014Q`
  visualization atlas descriptors whose texture residency has not
  landed are dropped from `RenderWorld::Visualization` and counted in
  `VisualizationDiagnostics::TextureResidencyDeferredCount`. If
  `InitializeFallbackTexture()` itself fails (for example
  `OutOfDeviceMemory`), the cache leaves
  `GpuAssetCacheDiagnostics::FallbackTextureReady = false` and
  `GetViewOrFallback()` returns `GpuAssetFallbackReason::Unavailable`
  so material code can fall back to factor-only shading
  deterministically without a graphics-side panic.
- **Backend descriptor flush cadence.** Bindless texture descriptor
  writes are coalesced per frame: the backend records all bindless
  slot writes produced during the frame's `IRenderer::PrepareFrame`/
  `Record` window and drains them as a single descriptor batch at the
  start of the next frame's `BeginFrame()`, mirroring the
  `Picking.Readback` drain pattern from `GRAPHICS-012Q` and the
  histogram readback drain from `GRAPHICS-013AQ`. Sampler creation is
  deduplicated through `RHI::SamplerManager` (mirroring the
  shader-pipeline registry deduplication from `GRAPHICS-006`); a
  sampler change detected through a different `SamplerDesc` on the
  next `RequestUpload` triggers a coalesced bindless re-write of the
  affected lease's descriptor in the same per-frame batch and
  increments a `BindlessDescriptorRewrites` counter on
  `GpuAssetCacheDiagnostics`. Material slot updates that swap an
  `AssetId` flow through `MaterialSystem::ResolveTextureAssetBindings()`
  and write the resolved `BindlessIndex` into `MaterialParams`; no
  separate descriptor flush is required because the bindless index is
  retained-stable per lease for the lease's entire `Ready` lifetime.
  Stale-bindless-index hazards on hot reload are prevented by the
  existing frame-anchored retire queue: a retired lease's bindless
  descriptor stays live for `framesInFlight` frames, so material
  slots that captured the index in an earlier renderer snapshot
  continue to resolve to a live descriptor for at least that long.
  No additional fence, semaphore, or graphics-side synchronization
  is introduced by this clarification. The exact `VkDescriptorSet`
  layout and the heap write batching implementation remain
  backend-local under `src/graphics/vulkan` and do not leak through
  RHI or renderer module surfaces.
- **Runtime ownership for fallback initialization and upload
  scheduling.** Runtime owns both. (a) Fallback initialization:
  `Runtime.Engine` (or a runtime-side graphics-bootstrap step) calls
  `cache.InitializeFallbackTexture(fallbackDesc)` exactly once after
  the cache is constructed and before any runtime asset bridge issues
  `RequestUpload(GpuTextureRequest)`. The fallback CPU bytes come
  from a baked engine resource owned by the runtime layer (a
  compiled-in byte array or runtime-loaded engine asset, not a
  graphics-layer file read); the cache only consumes the
  `std::span<const std::byte>`. If the call fails the runtime logs
  the failure and continues; `FallbackTextureReady` stays `false`
  and material code falls back to factor-only shading deterministic
  ally per the fallback content policy above. (b) Upload scheduling:
  texture-typed asset bridges (planned umbrella module
  `Extrinsic.Runtime.AssetBridges.Texture`, mirroring the
  `Extrinsic.Runtime.SpatialDebugAdapters` pattern from `GRAPHICS-011Q`
  and the `Extrinsic.Runtime.VisualizationAdapters` pattern from
  `GRAPHICS-014Q`) subscribe to texture-typed `AssetEvent::Ready`
  events on `AssetService::SubscribeAll`, read the decoded CPU
  payload from the asset registry's published payload pointer,
  construct a `GpuTextureRequest` (`AssetId`, `Bytes` span,
  `TextureDesc`, sampler descriptor or pre-allocated `SamplerHandle`),
  and call `cache.RequestUpload(req)` synchronously from the
  asset-event handler thread. Heavy CPU decoding may be queued
  through `Extrinsic.Runtime.StreamingExecutor` (the same async
  surface used for visualization baking under `GRAPHICS-014Q`), but
  the final `RequestUpload` call is always synchronous from the
  runtime side; graphics never schedules CPU work and never imports
  `AssetService` or `AssetEventBus`. Asset destruction flows through
  runtime: `AssetEvent::Destroyed` -> `cache.NotifyDestroyed(id)`,
  which queues any live lease for retirement. Editor / app code may
  expose per-asset priority hints through future runtime APIs, but
  the cache currently has no priority queue and graphics never
  receives priority data.

## Resolution
- Decisions recorded above and consequential notes synced into
  `docs/architecture/graphics.md` ("Graphics asset residency"
  section, new "Per `GRAPHICS-015Q`" paragraph),
  `docs/architecture/rendering-three-pass.md` (texture residency
  paragraph extended with `GRAPHICS-015Q` policy summary next to the
  existing `GRAPHICS-014Q` paragraph), `src/graphics/renderer/README.md`
  (matching ownership-contract bullet near the `MaterialSystem` /
  `GpuAssetCache` description), and `src/graphics/assets/README.md`
  (new "Per `GRAPHICS-015Q`" subsection under "Texture residency and
  fallback policy"). The rendering backlog README entry for
  `GRAPHICS-015Q` is redirected from the `tasks/backlog/rendering/`
  path to the `tasks/active/` path by the promotion commit and will
  be redirected to `tasks/done/` by the retirement commit.
- Verification: `python3 tools/agents/check_task_policy.py --root . --strict`
  and `python3 tools/docs/check_doc_links.py --root .`.

## Goal
- Clarify backend/runtime details that remain after the CPU/null `GRAPHICS-015` GPU asset cache, fallback texture, and material texture binding contracts.

## Non-goals
- No C++ behavior changes.
- No Vulkan implementation work.
- No importer/exporter or filesystem policy changes.

## Context
- `GRAPHICS-015` established texture upload requests, sampler-descriptor ownership, deterministic fallback texture resolution, material `AssetId` texture binding resolution, explicit non-eviction diagnostics, and mock/null tests.
- Runtime still owns asset event translation and producer sidecars; graphics consumes `AssetId` values and cache views only.

## Required changes
- Clarify whether future cache capacity policy should stay non-evicting, use explicit budgets, or support LRU/priority eviction with frames-in-flight retire guarantees.
- Clarify streaming mip/reupload behavior and how it should use `TextureManager::Reupload()` versus full texture lease replacement.
- Clarify fallback texture content policy for color, normal, metallic/roughness, emissive, and visualization/Htex atlas references.
- Clarify backend descriptor flush cadence for bindless texture slots and sampler changes.
- Clarify runtime ownership for initializing fallback textures and scheduling texture uploads from decoded asset payloads.

## Tests
- Documentation/checker only; no C++ tests required unless policy docs introduce checked manifests.

## Docs
- Update graphics architecture and graphics-assets README with the selected policies.

## Acceptance criteria
- Future Vulkan/runtime texture residency work has unambiguous ownership, fallback content, streaming, descriptor flush, and capacity policies.
- No live `AssetService`, ECS, importer, or editor dependency is introduced into graphics layers.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing docs-only clarification with semantic C++ behavior edits.
- Implementing Vulkan texture upload or cache eviction in this clarification task.
- Moving importer/exporter or asset-service ownership into graphics.

