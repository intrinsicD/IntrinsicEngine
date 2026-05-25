# RUNTIME-080 — `Extrinsic.Runtime.AssetBridges.Texture` umbrella

## Goal
- Open the runtime-side texture-asset bridge declared by `GRAPHICS-015Q`: a new module umbrella `Extrinsic.Runtime.AssetBridges.Texture` (planned home: `src/runtime/AssetBridges/Runtime.AssetBridges.Texture.cppm`) that subscribes to texture-typed `AssetEvent::Ready` from `AssetEventBus`, builds `GpuTextureRequest` descriptors, and calls `GpuAssetCache::RequestUpload(req)` synchronously. Heavy CPU decoding is queued through `Extrinsic.Runtime.StreamingExecutor`.

## Non-goals
- No mesh-asset residency bridge (that is `GRAPHICS-034` and gates on `ASSETIO-001`).
- No graphics-side asset polling or file watching.
- No mutation of `GpuAssetCache` eviction policy.
- No `imgui.h` import.
- No new RHI surface.

## Context
- Status: not started.
- Owner/layer: `runtime`.
- Planning anchors: `tasks/done/GRAPHICS-015Q-texture-residency-backend-clarifications.md` ("texture-typed asset bridges under the planned umbrella `Extrinsic.Runtime.AssetBridges.Texture` subscribe to texture-typed `AssetEvent::Ready`, build `GpuTextureRequest`, and call `cache.RequestUpload(req)` synchronously"); `src/graphics/renderer/README.md:385–395`.
- Today: the runtime-owned fallback bootstrap (`RUNTIME-070`) is the only path that touches `GpuAssetCache` directly; texture-typed `AssetEvent::Ready` events fire but no one consumes them. Material `BindlessIndex` resolutions miss because `RequestUpload` is never called.
- Streaming: heavy CPU texture decoding (PNG/EXR/KTX) happens off the main thread via `Extrinsic.Runtime.StreamingExecutor`; the bridge schedules and observes completion before issuing `RequestUpload`.

## Open clarification (recorded 2026-05-25 by `claude/intrinsicengine-agent-onboarding-k31Vm` during pick-up triage; nonblocking — picker chose RUNTIME-082 as next earliest unblocked Theme A leaf and left this question for the eventual implementer)

This task has two implicit upstream dependencies that are not currently
recorded in the dependency anchors of
[`tasks/backlog/README.md`](../README.md):

1. **CPU texture payload type ownership.** The task assumes a typed payload
   exists on `AssetPayloadStore` for a "texture-typed asset" — the bridge
   reads it via `PayloadStore::ReadSpan<T>(id)` to derive
   `RHI::TextureDesc.{Format, Width, Height, MipLevels}`, `RHI::SamplerDesc`,
   and the source bytes for `GpuTextureRequest`. No such type exists in
   `src/assets/` today. `ASSETIO-001`
   ([`tasks/backlog/assets/ASSETIO-001-asset-model-texture-ingest-ownership.md`](../assets/ASSETIO-001-asset-model-texture-ingest-ownership.md))
   explicitly owns "Define and implement CPU texture decode payload
   ownership and metadata (`dimensions`, format/color space, component
   count, source path/generation) without creating GPU resources in
   `assets`". Implementing RUNTIME-080 before ASSETIO-001 would either (a)
   require the bridge to define a runtime-local placeholder payload type
   (which is a speculative abstraction outside the selected task and
   violates the prompt anti-pattern of cross-domain scope creep), or (b)
   block on ASSETIO-001 landing the canonical type first.

2. **Asset-type discriminator on `AssetEventBus`.** The task says
   "subscribes to texture-typed `AssetEvent::Ready` … (filtered via the
   existing asset-type discriminator)". `AssetEventBus` today carries only
   `(AssetId, AssetEvent)` payloads (see
   `src/assets/Asset.EventBus.cppm`) — there is no type discriminator on
   the event surface. Filtering by payload type must be done after fact
   via `PayloadStore::ReadSpan<T>(id)` returning a typed expected, which
   depends on the payload type from clarification (1).

**Recommended resolution before promotion.** Either:

- Land ASSETIO-001's texture payload type first (or a focused Slice A of
  ASSETIO-001 limited to defining the payload type), then promote
  RUNTIME-080 with a slice plan whose Slice A consumes that type; or
- Add a non-goal here explicitly recording that the bridge defines a
  runtime-local placeholder payload type (e.g.
  `Runtime::TextureSourcePayload`) and that ASSETIO-001 will reroute it to
  the canonical asset-owned type during ingest landing. The placeholder
  must not appear on any public `src/assets/` surface.

Either path is reasonable; the choice belongs to whoever next promotes
this task. Picker for the next session left this for the implementer rather
than guessing.

## Required changes
- [ ] Add `src/runtime/AssetBridges/Runtime.AssetBridges.Texture.cppm` exporting `Extrinsic.Runtime.AssetBridges.Texture` with:
  - `class TextureAssetBridge` initialized with `(AssetEventBus&, GpuAssetCache&, StreamingExecutor&)`,
  - `Initialize()` subscribes to texture-typed `AssetEvent::Ready` (filtered via the existing asset-type discriminator),
  - on event: enqueue a streaming task to decode the texture bytes into the canonical RGBA8/BCn buffer, then on main-thread completion build `GpuTextureRequest{ Format, Extent, MipCount, SamplerDesc, … }` and call `cache.RequestUpload(req)`,
  - `Shutdown()` unsubscribes and drains pending streaming tasks.
- [ ] Wire `TextureAssetBridge` ownership into `Engine::Initialize()` after `GpuAssetCache` and `StreamingExecutor` exist; tear down before `AssetService` in `Engine::Shutdown()`.
- [ ] Add `TextureAssetBridgeDiagnostics` (counters: `EventsObserved`, `UploadsRequested`, `DecodeFailures`, `MissingFormatRejections`, `MalformedAssetRejections`).

## Tests
- [ ] `contract;runtime` test: an `AssetEvent::Ready` for a texture-typed asset triggers exactly one `cache.RequestUpload` call with the expected `GpuTextureRequest`.
- [ ] `contract;runtime` test: a non-texture asset event is ignored (no `RequestUpload`, no diagnostic increments).
- [ ] `contract;runtime` test: a malformed texture asset (invalid format/extent) increments `MalformedAssetRejections` and does not call `RequestUpload`.
- [ ] `contract;runtime` test: shutdown drains all in-flight decode tasks.
- [ ] No `gpu`/`vulkan` test in this slice.

## Docs
- [ ] Update `src/runtime/README.md` to flip the planned `AssetBridges.Texture` umbrella row to current state.
- [ ] Update `src/graphics/assets/README.md` to record that runtime is now the live texture upload scheduler.
- [ ] Refresh `docs/api/generated/module_inventory.md` after adding the module.

## Acceptance criteria
- [ ] Texture-typed asset events produce deterministic `GpuAssetCache::RequestUpload` calls.
- [ ] Non-texture events and malformed assets are filtered without surprise.
- [ ] No new graphics import; no `AssetService`/`AssetEventBus` import in `src/graphics/*`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Importing `Extrinsic.Asset.*` from `src/graphics/*`.
- Issuing `RequestUpload` from a streaming task thread (must hop to main).
- Mutating cache eviction policy.

## Next verification step
- Land the bridge module + Engine wiring + diagnostics counters, exercise the contract tests above.
