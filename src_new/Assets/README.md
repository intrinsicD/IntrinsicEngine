# Assets

`src_new/Assets` contains the asset subsystem built on top of `Extrinsic.Core`.
`AssetService` is the main entry point; it wires together the registry, payload
store, load pipeline, event bus, and path index behind a single façade.

## Public module surface

- `Extrinsic.Asset.Service`
- `Extrinsic.Asset.Registry`
- `Extrinsic.Asset.PayloadStore`
- `Extrinsic.Asset.LoadPipeline`
- `Extrinsic.Asset.EventBus`
- `Extrinsic.Asset.PathIndex`
- `Extrinsic.Asset.TypePool`

## How the subsystem fits together

- `AssetService` is the front door for typed load/read/reload/destroy calls.
- `AssetRegistry` owns the generational `AssetId` handles and per-asset metadata.
- `AssetPayloadStore` stores typed payloads behind stable tickets and returns
  borrowed `std::span<const T>` views for reads.
- `AssetLoadPipeline` tracks load stages, in-flight requests, GPU fence waits,
  and failure / completion transitions.
- `AssetEventBus` batches `Ready`, `Failed`, `Reloaded`, and `Destroyed`
  notifications for main-thread fanout.
- `AssetPathIndex` resolves absolute paths to live assets.
- `TypePools<Key>` in `Asset.TypePool.cppm` provides stable type IDs for
  payloads without requiring RTTI.

## Files in this directory

### Module interfaces (`.cppm`)

```text
Asset.EventBus.cppm
Asset.LoadPipeline.cppm
Asset.PathIndex.cppm
Asset.PayloadStore.cppm
Asset.Registry.cppm
Asset.Service.cppm
Asset.TypePool.cppm
```

### Private implementation units (`.cpp`)

```text
Asset.EventBus.cpp
Asset.LoadPipeline.cpp
Asset.PathIndex.cpp
Asset.PayloadStore.cpp
Asset.Registry.cpp
Asset.Service.cpp
```

### Build wiring

- `CMakeLists.txt` builds `ExtrinsicAssets` and links it publicly against
  `ExtrinsicCore`.
- There is no separate `.cpp` implementation file for `Asset.TypePool.cppm`;
  it is a header-only module interface.

## Dependency note

`Assets` depends on `Core`, but `Core` does not depend on `Assets`.
