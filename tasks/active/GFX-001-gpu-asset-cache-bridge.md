# GFX-001 Graphics.GpuAssetCache bridge

## Goal

Introduce a graphics-owned side table that maps `Assets::AssetId` to GPU
resources without leaking GPU handles into `Asset.Registry`, so renderer
snapshots survive hot reloads for at least `framesInFlight` frames.

## Non-goals

- Asset-type-specific loaders (mesh import, texture decode).
- ECS component refactors. Tightening
  `ECS.Component.AssetInstance::Source::AssetId` from raw `uint32_t` to
  `Core::StrongHandle<AssetTag>` is tracked separately.
- Replacing `Renderer::GetBufferManager()` / `GetTextureManager()`
  ownership.
- Streaming mip uploads / partial uploads (`TextureManager::Reupload`).
- Removing `src/legacy/`.

## Context

New owning directory `src/graphics/assets/` (graphics layer):

- Module: `Extrinsic.Graphics.GpuAssetCache`.
- Library: `ExtrinsicGraphicsAssets`.
- Allowed dependencies (per AGENTS.md §2): `Core`, asset IDs (the
  `Asset.Registry` types only — no live `AssetService` traffic), and
  `RHI` (`Handles`, `Descriptors`, `BufferManager`, `TextureManager`,
  `TransferQueue`, `Bindless`).
- Disallowed: ECS, Runtime, Platform, Vulkan backend, AssetService.

A placeholder for this work already existed at
`src/runtime/Runtime.Engine.cpp:455` — `// placeholder until
GpuAssetCache / deferred-delete lands` — and `RHI.TransferQueue.cppm`'s
header rationale already names `GpuAssetCache` as the planned consumer.

Runtime is the only layer that wires `AssetEventBus` to the cache.

## Required changes

- New files
  - `src/graphics/assets/Graphics.GpuAssetCache.cppm` — public interface
    (state machine enum, view struct, request structs, class).
  - `src/graphics/assets/Graphics.GpuAssetCache.cpp` — Impl with locked
    `unordered_map<AssetId, Slot>` keyed side table, retire vector,
    monotonic generation counter.
  - `src/graphics/assets/CMakeLists.txt` — `ExtrinsicGraphicsAssets`
    module library.
  - `src/graphics/assets/README.md` — layer/dependency contract.
- Top-level `CMakeLists.txt` — `INTRINSIC_GRAPHICS_ASSETS_SOURCE_ROOT`
  variable + `add_subdirectory` between RHI and renderer.
- Runtime wiring
  - `src/runtime/Runtime.Engine.cppm` — `m_GpuAssetCache` member,
    listener token, `GetGpuAssetCache()` accessor, new imports.
  - `src/runtime/Runtime.Engine.cpp` — construct cache after renderer,
    `AssetService::SubscribeAll(...)` mapping events to the
    `Notify*` methods, extend `AssetHooks::TickAssets()` to call
    `cache.Tick(GlobalFrameNumber, FramesInFlight)`, unsubscribe and
    reset cache during `DestroyAssets` (before `ShutdownRenderer`).
  - `src/runtime/CMakeLists.txt` — link `ExtrinsicGraphicsAssets`.
- Tooling
  - `tools/repo/generate_module_inventory.py` — recognise `assets`
    sublayer under `src/graphics/`.
  - `tools/repo/check_layering.py` — classify any
    `src/graphics/<sub>/` path as `graphics` (or `graphics_rhi` for
    `<sub>=rhi`) before falling through to generic per-name detection,
    so the new directory is not misclassified as `assets`.

## Tests

`tests/unit/graphics/Test.GpuAssetCache.cpp` (new). Linked into a new
`IntrinsicGraphicsAssetsUnitTests` target with labels `unit graphics`
(no `gpu`/`vulkan`, so the default CPU gate runs it).

Cases:

1. Buffer request → Ready after Tick; generation > 0.
2. Texture request populates a non-zero `BindlessIdx` when a sampler is
   provided.
3. Allocation failure (`MockDevice::FailNextBufferCreate=true`) returns
   `OutOfDeviceMemory` and parks the entry in `Failed`.
4. `NotifyFailed` on an unknown id creates a `Failed` entry.
5. Hot reload: a new `RequestUpload` on a `Ready` entry produces a
   strictly larger generation; the old lease is moved to the retire
   queue and the old view remains visible until the new pending
   completes.
6. Old-view lifetime: `framesInFlight=2` keeps the retired buffer alive
   until the deadline tick; `MockDevice::DestroyBufferCount` increments
   exactly once on the deadline tick.
7. `NotifyReloaded` on a `Ready` entry transitions to `CpuPending`
   without retiring (the old lease is retained for the eventual
   `RequestUpload`).
8. `NotifyDestroyed` removes the entry, queues the lease, and the
   queue drains after `framesInFlight` ticks.
9. Concurrent overlapping `RequestUpload` while `GpuUploading` reports
   `ResourceBusy` (no in-flight cancellation).

## Docs

- `AGENTS.md` §3 source-tree map adds `src/graphics/assets/`.
- `docs/architecture/layering.md` adds the `graphics/assets` row
  (`graphics/assets -> core, asset IDs, graphics/rhi`).
- `docs/api/generated/module_inventory.md` regenerated via
  `python3 tools/repo/generate_module_inventory.py` — the new module
  appears under the `graphics/assets` sublayer.

## Acceptance criteria

- `IntrinsicGraphicsAssetsUnitTests` builds and runs all 9 cases
  green.
- The default CPU gate
  `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  reports the 9 new tests as part of its passing set (label
  inheritance from `IntrinsicGraphicsUnitTests` is avoided by the
  separate target).
- `python3 tools/repo/check_layering.py --root src --strict` exits 0
  with no new violations.
- `python3 tools/repo/generate_module_inventory.py --check` exits 0.
- AGENTS.md §3 source-tree map and `docs/architecture/layering.md`
  list the new directory.
- The `// placeholder until GpuAssetCache lands` line in
  `Runtime.Engine.cpp` is removed.

## Verification

```bash
cmake --preset ci
cmake --build build/ci --target IntrinsicGraphicsAssetsUnitTests
cmake --build build/ci --target ExtrinsicRuntime
ctest --test-dir build/ci --output-on-failure \
      -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --check
```

## Forbidden changes

- No edits to `src/legacy/`.
- No imports of `Extrinsic.ECS.*`, `Extrinsic.Runtime.*`,
  `Extrinsic.Backends.Vulkan.*`, `Extrinsic.Asset.Service`, or
  `Extrinsic.Asset.EventBus` from `src/graphics/assets/` —
  `AssetEventBus` subscription belongs to `Runtime`.
- No new GPU handle storage in `Asset.Registry` / `Asset.Service`.
- No mixing of mechanical moves with this semantic addition.

## Out of scope / follow-ups

- ECS asset components (`ECS.Component.AssetInstance::Source::AssetId`
  is a raw `uint32_t`); tightening to `Core::StrongHandle<AssetTag>`
  is a separate task.
- A type-specific `MeshAssetBridge` / `TextureAssetBridge` that
  converts a payload-store `Ready` event into
  `GpuAssetCache::RequestUpload(...)` requires per-type knowledge of
  the CPU layout (vertex format, mip count) the cache deliberately
  does not own.
- Tighter retirement keyed on the renderer's `completedGpuValue`
  rather than the CPU frame counter.
- HARDEN-041 follow-up to relabel the existing `IntrinsicGraphicsUnitTests`
  target so its mock-driven tests no longer carry `gpu`/`vulkan`.
