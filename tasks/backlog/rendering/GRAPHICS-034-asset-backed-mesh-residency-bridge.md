# GRAPHICS-034 — Asset-backed mesh residency from AssetInstance::Source to GpuWorld (planning)

## Goal
Lock down the design for the asset-source residency path that bridges `ECS::Components::AssetInstance::Source` through `Asset.Service` and `Graphics.GpuAssetCache` into `GpuWorld` geometry residency — including AssetId normalization placement, refcount and sharing semantics, pending/failure/ready/regeneration policy, ordering against `GpuAssetCache::Tick`, and extensibility to non-mesh asset types — before any code lands. This is a planning task; no engine behavior, no asset ingest, no `GpuWorld` upload from runtime extraction in this slice.

## Non-goals
- No implementation, no new build modules, no extraction wiring changes in this slice.
- No new asset format work (glTF/USD/etc. ingestion is `assets`/ASSETIO-001 territory).
- No texture or material asset residency work (covered by GRAPHICS-015/015Q and GRAPHICS-031).
- No live ECS access from `src/graphics/*`; bridge stays in `runtime`.
- No GPU-typed ECS components per AGENTS.md §2 and GRAPHICS-028.
- No new `GpuWorld` / `GpuAssetCache` capacity, eviction, or generation policy beyond what GRAPHICS-004 / 005 / 015 / 015Q / 023A/B/C/D establish.
- No expansion of hot-reload semantics beyond the existing GRAPHICS-023A/B/C/D acknowledgment loop.
- No procedural-source duplication; the procedural path stays as planned in GRAPHICS-030.

## Context
- Owner layer: `runtime` (residency-bridge writer); `assets` and `graphics/assets` for the data flow; final boundaries follow AGENTS.md §4 and GRAPHICS-019 IO boundary planning.
- `src/ecs/Components/ECS.Component.AssetInstance.cppm` exposes `AssetInstance::Source { std::uint32_t AssetId }`. Runtime normalizes the CPU asset identifier to `Assets::AssetId` consumed by `GpuAssetCache`.
- `src/graphics/assets/Graphics.GpuAssetCache.cppm` implements `Assets::AssetId → GpuAssetView` with `NotRequested → CpuPending → GpuUploading → Ready/Failed`, generation counter, retire queue. GRAPHICS-023A/B/C/D already wire generation tracking and acknowledgment.
- `Extrinsic::Graphics::Components::GpuSceneSlot` already carries `SourceAsset`, `LastSeenAssetGeneration`, and slot/generation fields needed by this bridge (landed in GRAPHICS-023A).
- The 2026-05-08 review (sections "Asset loading is not yet a renderable geometry residency bridge" and "minimal milestone plan / 5") records this as the asset-backed follow-up to GRAPHICS-030.
- `tasks/active/ASSETIO-001-asset-model-texture-ingest-ownership.md` covers asset-side ingest ownership; this task consumes whatever shape that boundary lands on.

## Design decisions to record
1. **AssetId normalization placement.** Decide the function signature and module that converts `AssetInstance::Source::AssetId (std::uint32_t)` to `Assets::AssetId` and where it lives. Recommend a runtime-side helper `Runtime::NormalizeAssetInstanceId(...)` so neither ECS nor graphics imports the asset registry; record the rule that runtime never embeds the normalization in extraction tick code (it goes through one named function for testability).
2. **Cache placement.** Decide whether asset-backed geometry residency reuses `Runtime::ProceduralGeometryCache` from GRAPHICS-030 or a separate `Runtime::AssetGeometryCache`. Recommend separate caches with a shared internal `RuntimeGeometryHandle` value type so deduplication, refcount semantics, and retire-queue interaction are independent and unit-testable. Record the trade-off vs a unified cache.
3. **Cache key.** For asset-backed geometry, the key is `Assets::AssetId` (not the entity ID); cache reuses one `GpuGeometryHandle` across all renderables sharing the asset. Record the rule that a generation bump invalidates the cache entry but keeps the asset-id key stable so refcount stays correct.
4. **Refcount and sharing.** Decide refcount type and lifecycle: increment on `EnsureResident(AssetId)`, decrement on entity destruction or candidate disqualification, retire through `GpuAssetCache::Tick(currentFrame, framesInFlight)` and `GpuWorld` deferred-free when the count hits zero. Forbid immediate `FreeGeometry` while in flight.
5. **State machine.** Lock the per-renderable observation state (consuming the GRAPHICS-023C classifier outputs):
   - `NotRequested` → request CPU payload via `Asset.Service`; renderable is observed but unbound.
   - `CpuPending` → still waiting; counter increments per stuck-frame budget; no binding.
   - `GpuUploading` → upload submitted; not yet bindable.
   - `Ready` → bind via `SetInstanceGeometry`; record `LastSeenAssetGeneration`.
   - `Failed` → fall back per decision (10) below; counter increments; the renderable does not silently disappear.
6. **Ordering inside extraction tick.** Lock the order: (1) classify per-entity asset state, (2) request uploads for new `NotRequested` entries, (3) bind `Ready` entries via `SetInstanceGeometry`, (4) drive the GRAPHICS-023D acknowledgment loop for generation rebinds, (5) release/retire entries whose entity disappeared, (6) call `GpuAssetCache::Tick` and `GpuWorld::SyncFrame` to close the frame.
7. **Generation tracking integration.** Decide that this task only consumes the GRAPHICS-023A/B/C/D loop without adding new acknowledgment surfaces. Record the precise call sequence and the rule that generation rebinds reuse the same `GpuInstanceHandle` while replacing the geometry binding; previous geometry is retired through the deferred-free path.
8. **Sharing fairness.** Lock the rule that all renderables sharing an `AssetId` see the same `GpuGeometryHandle` after `Ready`; no per-renderable copies. Record diagnostics that count asset-cache hits vs misses per frame.
9. **Stuck-pending policy.** Decide the upper bound on consecutive frames an asset may be in `CpuPending` before runtime emits a diagnostic warning (recommend a configurable budget with a reasonable default). Forbid hard-failing the engine on a slow asset; the renderable stays unbound until the cache resolves.
10. **Failure-mode fallback.** Decide what the renderable does when `GpuAssetCache` reports `Failed`. Two options: (a) substitute the GRAPHICS-031 default debug material with a sentinel "missing-mesh" geometry (cube placeholder), or (b) leave the renderable unbound and visible only via debug-overlay diagnostic. Recommend (a) so the missing-asset condition is visibly testable; record the placeholder geometry source. The placeholder must itself be a procedural source from GRAPHICS-030 (no chicken-and-egg).
11. **Diagnostics.** Name explicitly: `AssetGeometryCacheHits`, `AssetGeometryCacheMisses`, `AssetGeometryRebinds`, `AssetGeometryFailedAssets`, `AssetGeometryStuckPendingCount`. Decide their location on the runtime extraction diagnostics surface.
12. **Performance characteristics.** Record: O(1) per-renderable observation; no per-frame upload for resident assets; deduplication across renderables; no per-frame string allocations; the CPU payload request is a one-shot, not a polling loop.
13. **Extensibility surface.** Identify how non-mesh asset-backed geometry (graph asset, point-cloud asset, primitive-set asset) plugs in: each adds a per-domain packer and reuses the same cache + state machine. None are in scope here; enumerate to confirm the design generalizes.
14. **Layering audit.** Confirm: `src/ecs/*` adds no graphics or assets imports; `src/graphics/*` does not access live ECS or `Asset.Service`; `src/runtime/*` is the only writer of the asset-geometry cache; `src/graphics/assets/Graphics.GpuAssetCache` is consumed through its public surface only.

## Required changes
- [ ] Capture all fourteen decisions as explicit recorded answers, including the state-machine table in (5), the ordering in (6), and the fallback rule in (10).
- [ ] Cross-link with ASSETIO-001 (asset ingest ownership), GRAPHICS-015 / 015Q (texture residency analogue), GRAPHICS-019 (legacy IO boundaries), GRAPHICS-023A/B/C/D (generation tracking + acknowledgment), GRAPHICS-028 (residency planning), GRAPHICS-029 (bootstrap), GRAPHICS-030 (procedural source + placeholder geometry source), GRAPHICS-031 (default debug material for failure fallback).
- [ ] Identify follow-up implementation children (do **not** open here):
  - [ ] **GRAPHICS-034-Impl-A** — `Runtime::NormalizeAssetInstanceId` + `Runtime::AssetGeometryCache` skeleton + diagnostics; no extraction wiring yet.
  - [ ] **GRAPHICS-034-Impl-B** — extraction wiring for `NotRequested → CpuPending → GpuUploading → Ready` happy path; contract tests.
  - [ ] **GRAPHICS-034-Impl-C** — `Failed` fallback path + stuck-pending diagnostic + cleanup regression tests.
  - [ ] **GRAPHICS-034-Impl-D** — generation rebind via GRAPHICS-023D acknowledgment integration.
  - [ ] **GRAPHICS-034-Impl-E** (optional) — non-mesh asset-backed geometry packers (graph, point cloud) gated behind GRAPHICS-014 / 011 readiness.

## Tests
- [ ] Planning slice: validators only.
- [ ] Implementation children must add `contract;runtime` tests covering the full state machine (including `Failed` fallback and stuck-pending diagnostics), refcount cleanup, generation rebinds, and asset-id normalization unit coverage.
- [ ] GPU coverage stays opt-in `gpu;vulkan` and outside the default CPU gate.

## Docs
- [ ] Update `docs/architecture/graphics.md` "ECS renderable residency bridge" section with the asset-source plan and its composition with GRAPHICS-023A/B/C/D and GRAPHICS-030.
- [ ] Update `src/runtime/README.md`, `src/graphics/assets/README.md`, and `src/graphics/renderer/README.md` cross-links once decisions are recorded.
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` rows for asset-backed renderable residency.
- [ ] Update `tasks/backlog/rendering/README.md` DAG after GRAPHICS-030 (and after ASSETIO-001 ingest ownership lands).

## Acceptance criteria
- [ ] All fourteen decisions recorded with explicit answers and trade-off rationales; state-machine table and ordering are fully enumerated.
- [ ] Implementation children identified with scope and dependency gates but not opened.
- [ ] No new ingest / loader / format work scheduled here; ASSETIO-001 retains ownership of asset ingest.
- [ ] Layering invariants hold; no engine behavior changes land in this slice.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No new asset format ingestion work (defer to ASSETIO-001 and downstream tasks).
- No GPU-typed ECS components.
- No live ECS or `Asset.Service` access from graphics layers.
- No new `GpuAssetCache` eviction or capacity policy.
- No new hot-reload acknowledgment surface beyond GRAPHICS-023A/B/C/D.
- No silent disappearance of renderables on `Failed` assets; fallback must be visible and counted.
- No mixing of mechanical file moves with semantic refactors.
- No premature opening of implementation child tasks before this planning slice is approved.
