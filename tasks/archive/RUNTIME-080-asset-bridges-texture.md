# RUNTIME-080 — `Extrinsic.Runtime.AssetBridges.Texture` umbrella

## Status

Superseded and retired. The texture-asset bridge capability this task scoped
shipped under [`ASSETIO-001`](ASSETIO-001-asset-model-texture-ingest-ownership.md)
as `Extrinsic.Runtime.AssetModelTextureHandoff`, not under the originally
planned `Extrinsic.Runtime.AssetBridges.Texture` name/path. The task file itself
recorded this supersession ("do not promote this task as written") and directed
future work to ASSETIO-001 unless narrowed to a mechanical namespace
consolidation or a genuinely new capability.

- Completed: 2026-06-03.
- Owner/agent: Claude.
- Branch: `claude/backlog-tasks-multi-loop-knIJr`.
- Final implementation commit: this retirement commit (no code change — the
  capability already shipped under ASSETIO-001).
- Maturity: `CPUContracted` (inherited from the shipped
  `AssetModelTextureHandoff` contract coverage).
  [`RUNTIME-095`](RUNTIME-095-working-sandbox-acceptance.md) closes the scoped
  operational sandbox proof; broad file-backed texture visual coverage remains
  future work.
- Superseded by: `tasks/archive/ASSETIO-001-asset-model-texture-ingest-ownership.md`
  (Slice D.1 / D.2).

### Why retired without re-implementation

`src/runtime/Runtime.AssetModelTextureHandoff.cppm` already provides everything
this task required, just under a different module name:

- Subscribes to texture-typed `AssetService::Ready` events and filters
  non-texture / model-scene events (`NonModelTextureReadyEvents`,
  `ModelSceneReadyEvents`).
- Reads canonical `AssetTexture2DPayload` records, builds the GPU texture
  descriptor (`BuildGpuTextureDesc`), and calls
  `Graphics::GpuAssetCache::RequestUpload(...)` (`RequestTextureAssetUpload`).
- Surfaces equivalent diagnostics counters
  (`AssetModelTextureHandoffDiagnostics`: `ReadyEventsObserved`,
  `TextureUploadRequests`, `TextureUploadFailures`, `TextureUnsupportedFormat`,
  `TextureInvalidPayloads`) covering this task's `EventsObserved`,
  `UploadsRequested`, `DecodeFailures`, `MissingFormatRejections`, and
  `MalformedAssetRejections`.
- Heavy CPU texture decoding flows through the asset/model-texture IO bridge and
  runtime decoder callbacks landed in ASSETIO-001; graphics never imports
  `AssetService` / `AssetEventBus`.

Re-opening this umbrella as written would have re-implemented an existing seam
under a parallel name, violating the no-duplicate-abstraction rule. The KTX
decoder remains a separate decoder gap (not this umbrella's responsibility).

## Goal
- Open the runtime-side texture-asset bridge declared by `GRAPHICS-015Q`: a new module umbrella `Extrinsic.Runtime.AssetBridges.Texture` (planned home: `src/runtime/AssetBridges/Runtime.AssetBridges.Texture.cppm`) that subscribes to texture-typed `AssetEvent::Ready` from `AssetEventBus`, builds `GpuTextureRequest` descriptors, and calls `GpuAssetCache::RequestUpload(req)` synchronously. Heavy CPU decoding is queued through `Extrinsic.Runtime.StreamingExecutor`.

## Non-goals
- No mesh-asset residency bridge (that is `GRAPHICS-034` and gates on `ASSETIO-001`).
- No graphics-side asset polling or file watching.
- No mutation of `GpuAssetCache` eviction policy.
- No `imgui.h` import.
- No new RHI surface.

## Context
- Status: superseded by [`ASSETIO-001`](ASSETIO-001-asset-model-texture-ingest-ownership.md);
  the task was retired without re-implementation per the Status block above.
- Owner/layer: `runtime`.
- Planning anchors: `tasks/archive/GRAPHICS-015Q-texture-residency-backend-clarifications.md` ("texture-typed asset bridges under the planned umbrella `Extrinsic.Runtime.AssetBridges.Texture` subscribe to texture-typed `AssetEvent::Ready`, build `GpuTextureRequest`, and call `cache.RequestUpload(req)` synchronously"); `src/graphics/renderer/README.md:385–395`.
- Current state: ASSETIO-001 Slice D.1 implemented
  `Extrinsic.Runtime.AssetModelTextureHandoff`, which subscribes to
  `AssetService::Ready`, reads canonical `AssetTexture2DPayload` records,
  builds `GpuTextureRequest` descriptors, and requests uploads through
  `Graphics::GpuAssetCache`. Slice D.2 made the helper idempotent for
  already-uploading or ready child texture assets.
- Streaming: heavy CPU texture decoding for promoted PNG/JPEG/TGA/BMP/HDR now
  happens through the asset/model-texture IO bridge and runtime decoder
  callbacks landed in ASSETIO-001. KTX remains a separate decoder gap, not a
  reason to reopen this umbrella as written.

## Superseded clarification

The earlier ambiguity about CPU texture payload ownership and asset-event type
discrimination is resolved by ASSETIO-001. `src/assets` owns
`AssetTexture2DPayload`; runtime filters event types by attempting typed reads
from `AssetService`; graphics never imports `AssetService` or `AssetEventBus`.

## Required changes
- [x] Texture-typed `AssetService::Ready` subscriber that builds GPU texture
  descriptors and calls `GpuAssetCache::RequestUpload(...)` — shipped as
  `Extrinsic.Runtime.AssetModelTextureHandoff` (`RequestTextureAssetUpload`,
  `BuildGpuTextureDesc`) under ASSETIO-001 Slice D.1.
- [x] Engine ownership/teardown ordering for the handoff relative to
  `GpuAssetCache` / `AssetService` — wired by ASSETIO-001.
- [x] Diagnostics counters for observed events, upload requests, decode
  failures, and malformed/unsupported rejections — shipped as
  `AssetModelTextureHandoffDiagnostics`.

## Tests
- [x] `contract;runtime` coverage for texture `Ready` → single `RequestUpload`,
  non-texture events ignored, malformed payloads rejected, and shutdown drain —
  covered by the ASSETIO-001 contract suite for `AssetModelTextureHandoff`.
- [x] No `gpu`/`vulkan` test in this slice.

## Docs
- [x] `src/runtime/README.md` records the live texture handoff via
  `Extrinsic.Runtime.AssetModelTextureHandoff` (ASSETIO-001).
- [x] `src/graphics/assets/README.md` records runtime as the live texture upload
  scheduler (ASSETIO-001).
- [x] `docs/api/generated/module_inventory.md` lists
  `Extrinsic.Runtime.AssetModelTextureHandoff`.

## Acceptance criteria
- [x] Texture-typed asset events produce deterministic `GpuAssetCache::RequestUpload` calls (via `AssetModelTextureHandoff`).
- [x] Non-texture events and malformed assets are filtered without surprise.
- [x] No new graphics import; no `AssetService`/`AssetEventBus` import in `src/graphics/*`.

## Verification
ASSETIO-001 carried the verification for the shipped capability; this
retirement is a task-state/docs-only change validated by:
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Importing `Extrinsic.Asset.*` from `src/graphics/*`.
- Issuing `RequestUpload` from a streaming task thread (must hop to main).
- Mutating cache eviction policy.
