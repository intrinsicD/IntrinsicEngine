# Assets

`src/assets` contains the asset subsystem built on top of `Extrinsic.Core`.
`AssetService` is the main entry point; it wires together the registry, payload
store, load pipeline, event bus, and path index behind a single façade.

The [generated module inventory](../../docs/api/generated/module_inventory.md)
lists the public modules; [CMakeLists.txt](CMakeLists.txt) defines their build units.

## How the subsystem fits together

- `AssetService` is the front door for typed load/read/reload/destroy calls.
- `AssetRegistry` owns the generational `AssetId` handles and per-asset metadata.
- `AssetPayloadStore` stores typed payloads behind stable tickets and returns
  borrowed `std::span<const T>` views for reads.
- `Asset.ImportRouter` resolves file extensions plus optional payload/domain
  hints to CPU-only import/export routes for mesh, point-cloud, graph, model
  scene, and texture payloads. It does not import geometry, runtime, graphics,
  or decoder code. Geometry capability rows come from the core-owned
  `Core.GeometryFormatCatalog.inc`; see the
  [capability contract](../../docs/architecture/assets.md#geometry-format-capability-authority).
- `Asset.GeometryPayload` is the CPU-only, type-tagged geometry payload record
  used by model-scene payloads. It owns no codec registry or IO dispatch;
  runtime calls the promoted geometry loaders selected by `Asset.ImportRouter`
  directly, while geometry modules retain their ordinary exporters.
- `Asset.ImportValidation` defines per-request validation options and reports
  shared by CPU payloads and Runtime imports, including optional byte budgets.
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
  calls the concrete tinygltf/stb decoders and separately owns
  texture Ready-event upload requests into `GpuAssetCache` and model-scene
  ECS/material handoff. Embedded images remain CPU payload records in the model
  scene until runtime mints deterministic child texture assets for GPU
  residency.
- KTX/KTX2 extensions are recognized by `Asset.ImportRouter` only so import
  attempts fail with deterministic `AssetUnsupportedFormat` diagnostics.
  Current promoted workflows have no checked-in KTX assets or compressed/mip
  texture requirement, so `Asset.ModelTexturePayload` does not accept KTX CPU
  payloads and runtime registers no KTX decoder.
- `AssetLoadPipeline` tracks in-flight CPU requests and completion transitions. Reload requests can queue a `Reloaded`
  event immediately after entering `QueuedIO`, so main-thread subscribers see
  `Reloaded` before the subsequent `Ready` event. Scheduled CPU transitions
  capture a shared invalidation gate rather than the pipeline object: pipeline
  destruction makes queued callbacks no-op and waits only when that exact
  callback is already executing. It does not fence unrelated scheduler work,
  and the event bus and registry remain alive until the gate is invalidated.
- `AssetEventBus` batches `Ready`, `Failed`, `Reloaded`, and `Destroyed`
  notifications for main-thread fanout to global listeners registered with
  `SubscribeAll`/`UnsubscribeAll`. `Flush(id)` drains pending events for a
  single asset to those listeners while preserving unrelated pending events,
  which `AssetService` uses during destroy.
- `AssetPathIndex` resolves absolute paths to live assets.
- `TypePools<Key>` in `Asset.TypePool.cppm` provides process-local type tokens
  without RTTI, including the normalized 32-bit fold used by asset metadata.

## Build wiring

- `CMakeLists.txt` builds `ExtrinsicAssets` and links it publicly against
  `ExtrinsicCore`.
- There is no separate `.cpp` implementation file for `Asset.TypePool.cppm`;
  it is an interface-only module surface.

## Dependency note

`Assets` depends on `Core`, but `Core` does not depend on `Assets`.

## Error Reporting And Reload/Destroy Contract

Asset decoders and `AssetService` report failures using `Core::ErrorCode`.

`Load<T>` and `Reload<T>` decode through the caller's loader for that call;
`AssetService` does not retain loaders or their captures. Publication, path
interning, metadata changes and read lookup are serialized by the service.
Decoders execute outside that transaction, so they may call back into the service.

Reload is transactional:

- Failed decoders leave the last good payload, payload ticket generation,
  registry payload slot and `Ready` state intact.
- Successful reload advances the payload ticket and queues `Reloaded` before
  `Ready`. If another reload has already published, the older decode returns
  `ResourceBusy`; if destroy won, it returns `ResourceNotFound`.
- Failed commit transitions restore the checkpoint captured within the same
  transaction. A losing load for an interned path returns the winning ID only
  when its requested type matches.

`Read<T>` returns a borrowed span. The lookup itself is synchronized, but using
its span must be ordered against subsequent reload or destroy of the same ID.
Concurrent callers do not gain a payload lease.

Destroy joins pipeline completion, dispatches already queued events without the
service lock, then joins again before removing path, payload and registry state.
Callbacks can re-enter the service; nested retirement of the same ID counts as
success for the outer destroy and queues `Destroyed` only once. Before each
nonterminal callback (`Ready`, `Reloaded`, `Failed`), the service checks that the
ID is still alive. Thus one callback's destroy suppresses later stale callbacks.
Events describe history, not a retained payload generation: a newer reload may
already be visible, and concurrent destroy after the live check can make a
callback's read fail. Callers that dispatch concurrently must synchronize their
own listener state. The runtime dispatches on its main thread.

## Assets ↔ Graphics boundary

`Assets` is **CPU-only and GPU-agnostic**. It does not link against `RHI`,
`Graphics`, or any Vulkan type. `AssetRegistry` stores CPU payload authority
only — never a `BufferView`, `TextureId`, bindless slot, or any other GPU
handle.

GPU-side state lives in a Graphics-owned side table (`GpuAssetCache`), keyed
by `AssetId`. The bridge is event-driven: `AssetEventBus` publishes `Ready`,
`Reloaded`, `Failed`, and `Destroyed`; runtime's sole published
`AssetWorkflowModule` wires the cache listener and keeps decode,
model-materialization, and texture-residency helpers private. Its texture stage
reads CPU texture payloads and requests `GpuAssetCache` uploads. Its model-scene
stage reads CPU model payloads, mints child `AssetTexture2DPayload` assets at
stable synthetic paths (`<model-path>.embedded-texture-<image-index>.<ext>`),
and creates ECS/material records that reference those child texture assets by
`AssetId`. Runtime is the only layer that names both sides.

See `AGENTS.md` → "Assets ↔ Graphics boundary" for the full contract
(per-asset state machine, `TransferManager` reuse, `AssetId`-in-components
rule, hot-reload atomicity).
