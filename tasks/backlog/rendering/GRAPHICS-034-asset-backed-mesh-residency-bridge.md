# GRAPHICS-034 — Asset-backed mesh residency from AssetInstance::Source to GpuWorld geometry

## Goal
Bridge `ECS::Components::AssetInstance::Source` through `Asset.Service` and `Graphics.GpuAssetCache` into `Graphics.GpuWorld` geometry residency, so that a renderable referencing a shared mesh asset participates in the same runtime-owned residency cache as the procedural-source path from GRAPHICS-030, with asset ID normalization, generation tracking, cache reuse, invalidation, and cleanup.

## Non-goals
- No new asset format work (glTF/USD/etc. ingestion lives with `assets`/ASSETIO-001 and downstream tasks).
- No texture or material asset residency work (covered by GRAPHICS-015/015Q and GRAPHICS-031).
- No live ECS access from `src/graphics/*`; bridge stays in `runtime`.
- No GPU-typed ECS components per AGENTS.md section 2 and GRAPHICS-028.
- No new `GpuWorld` / `GpuAssetCache` capacity or eviction policy beyond what GRAPHICS-004 / 005 / 015 / 015Q already establish.
- No expansion of hot-reload semantics beyond the existing GRAPHICS-023A/B/C/D acknowledgment loop.

## Context
- Owner layer: `runtime` (residency-bridge writer) plus `assets` and `graphics/assets` for the data flow; final boundaries follow AGENTS.md section 4 and the GRAPHICS-019 IO boundary planning.
- `src/ecs/Components/ECS.Component.AssetInstance.cppm` exposes `AssetInstance::Source { std::uint32_t AssetId }`. Runtime normalizes the CPU asset identifier to `Assets::AssetId` consumed by `GpuAssetCache`.
- `src/graphics/assets/Graphics.GpuAssetCache.cppm` implements `Assets::AssetId → GpuAssetView` with `NotRequested → CpuPending → GpuUploading → Ready/Failed`, generation counter, retire queue. GRAPHICS-023A/B/C/D already wire generation tracking and acknowledgment; this task layers actual upload/binding on top.
- `Extrinsic::Graphics::Components::GpuSceneSlot` already carries `SourceAsset`, `LastSeenAssetGeneration`, and `InstanceSlot/Generation`/`GeometrySlot/Generation` fields needed by this bridge (landed in GRAPHICS-023A).
- The 2026-05-08 review (`docs/reviews/2026-05-08-sandbox-geometry-rendering-gap-analysis.md`, section "Asset loading is not yet a renderable geometry residency bridge" and "minimal milestone plan / 5. Asset-backed geometry milestone") records this as the asset-backed follow-up to GRAPHICS-030.
- `docs/architecture/graphics.md` already notes that the full ECS renderable residency bridge from `AssetInstance::Source` to normalized `Assets::AssetId`, through GPU asset cache/world upload, and into instance geometry binding is follow-up work.
- `tasks/backlog/assets/ASSETIO-001-asset-model-texture-ingest-ownership.md` covers asset-side ingest ownership; this task consumes whatever shape that boundary lands on without redefining ingest.

## Required changes
- Implement asset-source residency in the runtime residency bridge from GRAPHICS-030:
  - Normalize `AssetInstance::Source::AssetId` to `Assets::AssetId` in runtime, not in graphics or ECS.
  - Request CPU payload through `Asset.Service`; once ready, drive `GpuAssetCache` upload, observe state transitions, and bind the resulting `GpuGeometryHandle` via `GpuWorld::SetInstanceGeometry()` only when the cache reports `Ready`.
  - Reuse a single `GpuGeometryHandle` across all renderables sharing the same `Assets::AssetId`; refcount in the runtime cache.
  - Free or retire the geometry when no live renderable references it, ordered correctly with `GpuAssetCache::Tick(currentFrame, framesInFlight)` and `GpuWorld` deferred-free semantics.
  - Use the existing GRAPHICS-023C observe / GRAPHICS-023D acknowledge loop for generation comparison; no new acknowledgment surface here.
- Define the failure modes:
  - Asset missing or `Failed` cache state → renderable falls back to the default debug material from GRAPHICS-031 (or a configurable missing-mesh sentinel) and increments a runtime diagnostic counter.
  - Pending uploads → the renderable is observed but not bound until `Ready`; tests must demonstrate this transition without renderer pass body soft-skip on the minimal recipe.
- Keep procedural-source residency from GRAPHICS-030 unchanged; the two paths share the runtime cache infrastructure but distinguish source identity.
- Cross-link decisions with ASSETIO-001, GRAPHICS-015 / 015Q (texture residency analogue), GRAPHICS-019 (IO boundaries), GRAPHICS-023A/B/C/D (generation tracking/rebind), GRAPHICS-028 (planning), GRAPHICS-029 (bootstrap), and GRAPHICS-030 (procedural source).

## Tests
- Add `contract;runtime` tests asserting the asset-source residency state machine:
  - Missing asset → fallback path, diagnostic counter increments, no `GpuGeometryHandle` bound.
  - Pending → no binding yet, snapshot reflects pending state, no extraction-time soft-skip.
  - Ready → exactly one `GpuGeometryHandle` allocated per `Assets::AssetId`, multiple renderables share it, refcount honored on destroy.
  - Generation advance → existing GRAPHICS-023C/D acknowledgment loop drives rebind without leaking the previous handle.
- Add a `contract;runtime` cleanup regression test that destroying all referencing renderables retires the geometry through `GpuAssetCache::Tick` and `GpuWorld` deferred-free without violating frame-in-flight semantics.
- Reuse the GRAPHICS-029 bootstrap fixture (extended with an asset-source variant) for end-to-end residency assertions.
- Verification gate: default CPU-supported correctness target.
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- Update `docs/architecture/graphics.md` "ECS renderable residency bridge" section to record that the asset-source path is implemented and how it composes with GRAPHICS-023A/B/C/D.
- Update `src/runtime/README.md`, `src/graphics/assets/README.md`, and `src/graphics/renderer/README.md` cross-links for the new asset-source residency seam.
- Update `docs/migration/nonlegacy-parity-matrix.md` rows for asset-backed renderable residency.
- Update `tasks/backlog/rendering/README.md` DAG to insert this task after GRAPHICS-030 (and after ASSETIO-001 ingest ownership lands).

## Acceptance criteria
- A renderable referencing an asset by `AssetInstance::Source::AssetId` ends up with a valid `GpuInstanceHandle` and `GpuGeometryHandle` once `GpuAssetCache` reports `Ready`, sharing geometry with other renderables on the same asset ID.
- Failure / pending / ready / regenerated states all have explicit, testable behavior; diagnostic counters surface the failure path.
- Layering invariants hold: ECS does not import graphics or assets internals; graphics does not access live ECS state; runtime is the only writer of the residency cache.
- All new tests pass under the default CPU gate.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- No new asset format ingestion work (defer to ASSETIO-001 and downstream tasks).
- No GPU-typed ECS components.
- No live ECS access from graphics or asset-cache layers.
- No new `GpuAssetCache` eviction or capacity policy.
- No new hot-reload acknowledgment surface beyond GRAPHICS-023A/B/C/D.
- No mixing of mechanical file moves with semantic refactors.
