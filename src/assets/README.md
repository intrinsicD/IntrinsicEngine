# Assets

`src/assets` contains the asset subsystem built on top of `Extrinsic.Core`.
`AssetService` is the main entry point; it wires together the registry, payload
store, load pipeline, event bus, and path index behind a single façade.

## Public module surface

- `Extrinsic.Asset.Service`
- `Extrinsic.Asset.Registry`
- `Extrinsic.Asset.PayloadStore`
- `Extrinsic.Asset.ImportRouter`
- `Extrinsic.Asset.GeometryIOBridge`
- `Extrinsic.Asset.ModelTextureIOBridge`
- `Extrinsic.Asset.ModelTexturePayload`
- `Extrinsic.Asset.OperationStatus`
- `Extrinsic.Asset.LoadPipeline`
- `Extrinsic.Asset.EventBus`
- `Extrinsic.Asset.PathIndex`
- `Extrinsic.Asset.TypePool`

## How the subsystem fits together

- `AssetService` is the front door for typed load/read/reload/destroy calls.
- `AssetRegistry` owns the generational `AssetId` handles and per-asset metadata.
- `AssetPayloadStore` stores typed payloads behind stable tickets and returns
  borrowed `std::span<const T>` views for reads.
- `Asset.ImportRouter` resolves file extensions plus optional payload/domain
  hints to CPU-only import/export routes for mesh, point-cloud, graph, model
  scene, and texture payloads. It does not import geometry, runtime, graphics,
  or decoder code.
- `Asset.GeometryIOBridge` stores asset-owned geometry import/export callback
  registrations keyed by resolved route format and payload kind. Runtime
  registers the promoted geometry codecs; `src/assets` dispatches callbacks and
  verifies typed payloads without importing geometry, runtime, graphics, or RHI.
- `Asset.ModelTextureIOBridge` stores asset-owned model-scene and texture
  decoder callback registrations keyed by resolved route format. Assets own
  primary file byte transport through `Extrinsic.Core.IOBackend`, relative
  external-resource reads, callback dispatch, decode-error propagation, and
  payload validation without importing decoder, geometry, runtime, graphics, or
  RHI code.
- `Asset.ModelTexturePayload` defines CPU-only model-scene and texture payload
  records, validation helpers, material texture references, and external
  resource diagnostics for GLTF/GLB and image ingest. Model primitives point at
  typed `AssetGeometryPayload` records so decoded geometry can remain CPU-owned
  without importing geometry into `src/assets`. The model-scene payload retains
  the active scene's root indices and a deterministic pre-order node array.
  Each node carries its parent/child indices, a column-major local transform,
  and indices into shared primitive prototypes; multiple nodes can therefore
  instance one decoded geometry/material prototype without duplicating CPU
  geometry. It stores bytes, indices, transforms, and metadata only; runtime
  registers the concrete tinygltf/stb decoder callbacks and separately owns
  texture Ready-event upload requests into `GpuAssetCache` and model-scene
  ECS/material handoff. Embedded images remain CPU payload records in the model
  scene until runtime mints deterministic child texture assets for GPU
  residency.
- KTX/KTX2 extensions are recognized by `Asset.ImportRouter` only so import
  attempts fail with deterministic `AssetUnsupportedFormat` diagnostics.
  Current promoted workflows have no checked-in KTX assets or compressed/mip
  texture requirement, so `Asset.ModelTexturePayload` does not accept KTX CPU
  payloads and runtime registers no KTX decoder.
- `Asset.OperationStatus` classifies promoted asset operation failures into a
  narrow CPU-side taxonomy: invalid argument, missing resource, invalid state,
  type mismatch, loader missing, callback failure, validation failure,
  unsupported format, IO failure, upload handoff failure, resource busy, and
  unknown failure.
- `AssetLoadPipeline` tracks load stages, in-flight requests, GPU fence waits,
  and failure / completion transitions. Reload requests can queue a `Reloaded`
  event immediately after entering `QueuedIO`, so main-thread subscribers see
  `Reloaded` before the subsequent `Ready` event.
- `AssetEventBus` batches `Ready`, `Failed`, `Reloaded`, and `Destroyed`
  notifications for main-thread fanout. It can also drain pending events for a
  single asset while preserving unrelated pending events, which `AssetService`
  uses during destroy.
- `AssetPathIndex` resolves absolute paths to live assets.
- `TypePools<Key>` in `Asset.TypePool.cppm` provides stable type IDs for
  payloads without requiring RTTI.

## Files in this directory

### Module interfaces (`.cppm`)

```text
Asset.EventBus.cppm
Asset.GeometryIOBridge.cppm
Asset.ImportRouter.cppm
Asset.LoadPipeline.cppm
Asset.ModelTextureIOBridge.cppm
Asset.ModelTexturePayload.cppm
Asset.OperationStatus.cppm
Asset.PathIndex.cppm
Asset.PayloadStore.cppm
Asset.Registry.cppm
Asset.Service.cppm
Asset.TypePool.cppm
```

### Private implementation units (`.cpp`)

```text
Asset.EventBus.cpp
Asset.GeometryIOBridge.cpp
Asset.ImportRouter.cpp
Asset.LoadPipeline.cpp
Asset.ModelTextureIOBridge.cpp
Asset.ModelTexturePayload.cpp
Asset.PathIndex.cpp
Asset.PayloadStore.cpp
Asset.Registry.cpp
Asset.Service.cpp
```

### Build wiring

- `CMakeLists.txt` builds `ExtrinsicAssets` and links it publicly against
  `ExtrinsicCore`.
- There is no separate `.cpp` implementation file for
  `Asset.OperationStatus.cppm` or `Asset.TypePool.cppm`; both are interface-only
  module surfaces.

## Dependency note

`Assets` depends on `Core`, but `Core` does not depend on `Assets`.

## Operation Status And Reload/Destroy Contract

Promoted asset errors reuse `Core::ErrorCode`; `Asset.OperationStatus` provides
the replacement for legacy `Asset.Errors` grouping. Import bridges and
`AssetService` preserve the original `Core::ErrorCode` while callers that need
coarser UI/status decisions can classify it with
`ClassifyAssetOperationStatus(...)` or `DiagnoseAssetOperation(...)`.

Reload is transactional through `AssetService`:

- Failed loader callbacks leave the last good payload, payload ticket
  generation, registry payload slot, and `Ready` state intact.
- Successful reload publishes a new payload ticket generation, then queues
  `Reloaded` followed by `Ready` for same-asset subscribers.
- Failed pre-commit transitions restore the previous payload checkpoint and do
  not queue successful reload events.

Destroy first cancels in-flight pipeline bookkeeping and drains queued events
for the asset while its payload is still readable. It then unregisters reload
callbacks, removes path-index state, retires the payload, destroys the registry
entry, and queues `Destroyed`. Runtime/graphics subscribers therefore never
receive a same-asset `Ready` event after `AssetService` has retired that
asset's CPU payload.

## Assets ↔ Graphics boundary

`Assets` is **CPU-only and GPU-agnostic**. It does not link against `RHI`,
`Graphics`, or any Vulkan type. `AssetRegistry` stores CPU payload authority
only — never a `BufferView`, `TextureId`, bindless slot, or any other GPU
handle.

GPU-side state lives in a Graphics-owned side table (`GpuAssetCache`), keyed
by `AssetId`. The bridge is event-driven: `AssetEventBus` publishes `Ready`,
`Reloaded`, `Failed`, and `Destroyed`; `Runtime` wires the graphics cache
listener and type-specific handoff objects. `AssetModelTextureHandoff` handles
texture `Ready` events by reading CPU texture payloads and requesting
`GpuAssetCache` uploads. `AssetModelSceneHandoff` handles model-scene `Ready`
events by reading CPU model-scene payloads, minting child `AssetTexture2DPayload`
assets at stable synthetic paths (`<model-path>.embedded-texture-<image-index>.<ext>`),
and creating ECS/material records that reference those child texture assets by
`AssetId`. Runtime is the only layer that names both sides.

See `AGENTS.md` → "Assets ↔ Graphics boundary" for the full contract
(per-asset state machine, `TransferManager` reuse, `AssetId`-in-components
rule, hot-reload atomicity).
