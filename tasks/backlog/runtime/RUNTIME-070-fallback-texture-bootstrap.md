# RUNTIME-070 — Bootstrap GpuAssetCache fallback texture in Engine::Initialize

## Goal
- Wire the runtime-side graphics-bootstrap step recorded by `GRAPHICS-015Q`: `Runtime::Engine::Initialize()` calls `m_GpuAssetCache->InitializeFallbackTexture(fallbackDesc)` exactly once with the canonical 4×4 magenta-and-black checkerboard bytes (RGBA8_UNORM, alpha 0xFF, nearest filter, clamp-to-edge), so that every sampled material texture slot has a deterministic fallback resolved through `GetViewOrFallback()` instead of returning `GpuAssetFallbackReason::Unavailable`.

## Non-goals
- No texture-typed asset bridge (that is `RUNTIME-080`, the `Extrinsic.Runtime.AssetBridges.Texture` umbrella).
- No texture eviction policy change (the cache stays non-evicting per `GRAPHICS-015Q`).
- No new fallback texture per material slot — the contract is one shared deterministic 4×4 fallback (`Normal`/`MetallicRoughness`/`Emissive` neutrality is enforced by material shader code observing the resolved `UsedFallback` bit, not by allocating per-slot fallback textures).
- No file IO from graphics; the bytes are runtime-baked.

## Context
- Status: not started.
- Owner/layer: `runtime`.
- Planning anchor: `src/graphics/renderer/README.md:385–395` ("Runtime owns both fallback initialization … a runtime-side graphics-bootstrap step calls `cache.InitializeFallbackTexture(fallbackDesc)` exactly once with fallback bytes from a baked engine resource owned by the runtime layer; the cache never reads files"). Also `tasks/done/GRAPHICS-015Q-texture-residency-backend-clarifications.md`.
- Today: `Engine::Initialize()` (`Runtime.Engine.cpp:182–202`) constructs `GpuAssetCache` and registers the `AssetEventBus` listener, but never calls `InitializeFallbackTexture(...)`. Result: any material that resolves through `GetViewOrFallback()` reports `GpuAssetFallbackReason::Unavailable` and material shader fallback paths take the deterministic factor-only branch.

## Required changes
- [ ] Add a runtime-owned constexpr/static byte array carrying the 4×4 magenta-and-black RGBA8 checkerboard (`{255, 0, 255, 255}` and `{0, 0, 0, 255}` alternating in a 2×2 pattern repeated to 4×4). Owner module: a small `Runtime.GraphicsBootstrap.cppm` (or inline within `Runtime.Engine.cpp`; the implementer chooses, prefer the new module if a second bootstrap byte resource is anticipated).
- [ ] After `m_GpuAssetCache` construction in `Engine::Initialize()`, build `GpuTextureRequest fallbackDesc{...}` (RGBA8_UNORM, 4×4, mip 0 only, nearest-clamp sampler) and call `m_GpuAssetCache->InitializeFallbackTexture(fallbackDesc, fallbackBytes)`.
- [ ] If `InitializeFallbackTexture()` reports failure, log a single `Core::Log::Warn(...)` breadcrumb and let `FallbackTextureReady = false` so material code routes to factor-only shading deterministically (matching the cache's documented contract).
- [ ] No-op when running headless on a Null device that cannot back textures: gate the call on `m_Device->IsOperational() || m_Device->GetTransferQueue().CanAcceptUploads()` so non-operational devices keep the existing behavior; document the gate.

## Tests
- [ ] `contract;runtime` test: after `Engine::Initialize()` on the Null device, `GpuAssetCache::GetFallbackTextureReady()` returns the documented value (false on Null today, true once the operational gate flips).
- [ ] `contract;runtime` test: after `Engine::Initialize()` with a hypothetical operational device (use a contract-test seam such as a forced-operational Null), `FallbackTextureReady = true` and `GetViewOrFallback(unknown_id)` returns the fallback view with `UsedFallback = true`.
- [ ] `contract;runtime` test: `Engine::Shutdown()` followed by re-`Initialize()` re-bootstraps the fallback exactly once (no double-init).
- [ ] No `gpu`/`vulkan` test in this slice.

## Docs
- [ ] Update `src/runtime/README.md` to document the bootstrap step in the `Engine::Initialize` ordering section.
- [ ] Update `src/graphics/assets/README.md` to flip the planned fallback-init row to current state.

## Acceptance criteria
- [ ] `Engine::Initialize()` calls `InitializeFallbackTexture` exactly once when the device can accept the upload.
- [ ] No new graphics imports beyond the existing `Extrinsic.Graphics.GpuAssetCache` edge.
- [ ] No regression in the existing AssetEventBus listener wiring (the listener is still subscribed once per init).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Reading the fallback bytes from a file (must be runtime-baked).
- Allocating per-slot fallback textures.
- Adding texture-asset upload scheduling (reserved for `RUNTIME-080`).
- Mutating cache eviction policy.

## Next verification step
- Add the bootstrap call + bytes resource, exercise the contract tests above.
