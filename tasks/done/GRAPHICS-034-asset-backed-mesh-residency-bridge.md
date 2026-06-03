# GRAPHICS-034 — Asset-backed mesh residency from AssetInstance::Source to GpuWorld (planning)

- Status: completed (2026-06-03; planning-only; `Scaffolded`).
- Owner / agent: runtime-owned render extraction / graphics residency planning.
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired. Implementation children stay unopened.

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
- No implementation child task is opened by this planning slice.

## Context
- Owner layer: `runtime` (residency-bridge writer); `assets` and `graphics/assets` for the data flow; final boundaries follow AGENTS.md §4 and GRAPHICS-019 IO boundary planning.
- `src/ecs/Components/ECS.Component.AssetInstance.cppm` exposes `AssetInstance::Source { std::uint32_t AssetId }`. Runtime normalizes the CPU asset identifier to `Assets::AssetId` consumed by `GpuAssetCache`.
- `src/graphics/assets/Graphics.GpuAssetCache.cppm` implements `Assets::AssetId → GpuAssetView` with `NotRequested → CpuPending → GpuUploading → Ready/Failed`, generation counter, retire queue. GRAPHICS-023A/B/C/D already wire generation tracking and acknowledgment.
- `Extrinsic::Graphics::Components::GpuSceneSlot` already carries `SourceAsset`, `LastSeenAssetGeneration`, and slot/generation fields needed by this bridge (landed in GRAPHICS-023A).
- The 2026-05-08 review (sections "Asset loading is not yet a renderable geometry residency bridge" and "minimal milestone plan / 5") records this as the asset-backed follow-up to GRAPHICS-030.
- `tasks/done/ASSETIO-001-asset-model-texture-ingest-ownership.md` covers asset-side ingest ownership and runtime model/texture handoff at `CPUContracted`; this task consumes that promoted boundary for asset-backed mesh residency.

## Recorded decisions
1. **AssetId normalization placement.** `Runtime::NormalizeAssetInstanceId(ECS::Components::AssetInstance::Source)` is the future named seam that converts the ECS `std::uint32_t` source field into `Assets::AssetId`. It lives in `runtime`, returns a fail-closed optional/expected promoted `Assets::AssetId` value, and rejects sentinel or otherwise invalid IDs through a diagnostic counter. Extraction code must call the helper rather than embedding casts inline, so normalization is unit-testable and neither ECS nor graphics imports the asset registry service.
2. **Cache placement.** Asset-backed geometry uses a separate `Runtime::AssetGeometryCache`, not `Runtime::ProceduralGeometryCache`. A small shared internal `RuntimeGeometryHandle` value type is acceptable, but cache ownership, keys, state machine, refcounts, failure diagnostics, and generation handling remain independent. The unified-cache alternative was rejected because procedural keys are content-addressed descriptors while asset keys are stable asset identities with reload generations.
3. **Cache key.** The asset-backed key is `Assets::AssetId`, never entity ID. All renderables that reference the same normalized asset share one resident geometry handle after `Ready`. A generation bump invalidates the ready binding and schedules a rebind while preserving the stable asset-id key and refcount so entity sharing remains correct through hot reload.
4. **Refcount and sharing.** `Runtime::AssetGeometryCache` uses a `std::uint32_t` refcount. `EnsureResident(assetId)` increments the count or inserts a new entry; entity destruction, disqualification, or source replacement calls `Release(assetId)`. Refcount saturation is fail-closed and counted. When the count reaches zero the entry queues a deferred retire and `GpuWorld::FreeGeometry()` is not called inline; free happens only after the `framesInFlight` window, matching `GpuAssetCache::Tick(currentFrame, framesInFlight)` safety.
5. **State machine.** Per-renderable observation consumes the GRAPHICS-023C classifier output and follows this table:

   | State | Runtime action | Binding rule | Diagnostics |
   | --- | --- | --- | --- |
   | `NotRequested` | Normalize `AssetInstance::Source`, insert/cache-miss, request CPU payload through `Asset.Service`, and schedule upload work once payload exists. | Entity is observed but no asset geometry is bound yet. | Count cache miss and request attempt. |
   | `CpuPending` | Keep waiting for CPU payload or asset service result; increment the consecutive pending-frame counter. | Do not bind stale or guessed geometry. | Count stuck-pending entries once the budget is exceeded. |
   | `GpuUploading` | Upload has been submitted through public graphics/RHI seams and runtime waits for cache readiness. | Not yet bindable. | Upload remains visible to cache diagnostics, not per-frame string logs. |
   | `Ready` | Bind the shared `GpuGeometryHandle` with `GpuWorld::SetInstanceGeometry()` and record `LastSeenAssetGeneration`. | All renderables for the same asset share the same handle. | Count cache hits and generation rebinds. |
   | `Failed` | Use the visible failure fallback from decision 10 and keep the failure count deterministic. | The renderable must not silently disappear. | Count failed assets and placeholder fallback uses. |

6. **Ordering inside extraction tick.** The future extraction order is: (1) classify per-entity source priority and current asset geometry state, (2) request CPU/upload work for new `NotRequested` entries, (3) bind `Ready` entries through `SetInstanceGeometry`, (4) acknowledge GRAPHICS-023D generation rebinds only after the replacement geometry binding succeeds, (5) release or retire entries whose entity disappeared or no longer qualifies, and (6) close the frame through the existing maintenance phase: `GpuAssetCache::Tick`, the future `AssetGeometryCache::Tick`, and the existing `GpuWorld`/renderer frame synchronization points. The order keeps upload requests before binding, binding before acknowledgment, and freeing after the in-flight window.
7. **Generation tracking integration.** GRAPHICS-034 consumes GRAPHICS-023A/B/C/D exactly as-is and adds no new acknowledgment surface. A generation rebind reuses the same `GpuInstanceHandle`, replaces only the geometry binding, updates `GpuSceneSlot::LastSeenAssetGeneration` after success, and retires the old geometry through the deferred-free path.
8. **Sharing fairness.** N renderables that reference the same `Assets::AssetId` share one `GpuGeometryHandle` after `Ready`; per-renderable copies are forbidden. Per-frame diagnostics count asset-geometry cache hits and misses so pathological repeated upload attempts or accidental per-entity duplication are visible in CPU tests.
9. **Stuck-pending policy.** `AssetGeometryStuckPendingFrameBudget` is a runtime-configurable budget with a default of 300 consecutive extraction frames. Exceeding the budget emits a diagnostic warning and increments `AssetGeometryStuckPendingCount`, but it does not hard-fail the engine; the renderable stays unbound or on the visible fallback until the cache resolves.
10. **Failure-mode fallback.** Failed assets use option (a): a visible missing-mesh cube placeholder rendered with the GRAPHICS-031 default debug surface material. The placeholder geometry is sourced from a built-in procedural descriptor/packer path derived from GRAPHICS-030, not from asset IO, so failure handling cannot depend on the failed asset. Until that placeholder implementation child exists, implementation slices must fail closed with diagnostics rather than silently dropping the renderable.
11. **Diagnostics.** The required counters live on the runtime extraction diagnostics surface (`RuntimeRenderExtractionStats` or its implementation-child successor): `AssetGeometryCacheHits`, `AssetGeometryCacheMisses`, `AssetGeometryRebinds`, `AssetGeometryFailedAssets`, and `AssetGeometryStuckPendingCount`. Implementation children may add adjacent counters such as normalization failures, placeholder fallback uses, releases, and free retires, but the five named counters are mandatory.
12. **Performance characteristics.** Observation is O(1) per renderable; resident assets do not upload per frame; requests are deduplicated by `Assets::AssetId`; CPU payload request is one-shot per cache miss/generation transition, not a polling loop; and the hot path does not allocate per-frame strings.
13. **Extensibility surface.** Mesh is the first asset-backed geometry domain. Graph, point-cloud, and primitive-set assets plug in later by adding per-domain packers that emit the same `GpuWorld::GeometryUploadDesc` shape and reuse the same cache/state machine. Those packers are gated by their own readiness tasks and are not in scope for GRAPHICS-034.
14. **Layering audit.** `src/ecs/*` adds no graphics or assets imports; `src/graphics/*` does not access live ECS, `Asset.Service`, or runtime sidecars; `src/runtime/*` is the only writer of the asset-geometry cache and the only owner of source-priority decisions; `src/graphics/assets/Graphics.GpuAssetCache` is consumed only through its public surface. No canonical ECS component stores GPU handles, slots, leases, or backend resources.

## Follow-up implementation children
- [x] **GRAPHICS-034-Impl-A** — `Runtime::NormalizeAssetInstanceId` + `Runtime::AssetGeometryCache` skeleton + diagnostics; no extraction wiring yet. Identified only; not opened.
- [x] **GRAPHICS-034-Impl-B** — extraction wiring for `NotRequested → CpuPending → GpuUploading → Ready` happy path; contract tests. Identified only; not opened.
- [x] **GRAPHICS-034-Impl-C** — `Failed` fallback path + stuck-pending diagnostic + cleanup regression tests. Identified only; not opened.
- [x] **GRAPHICS-034-Impl-D** — generation rebind via GRAPHICS-023D acknowledgment integration. Identified only; not opened.
- [x] **GRAPHICS-034-Impl-E** (optional) — non-mesh asset-backed geometry packers (graph, point cloud) gated behind GRAPHICS-014 / 011 readiness. Identified only; not opened.

## Required changes
- [x] Capture all fourteen decisions as explicit recorded answers, including the state-machine table in (5), the ordering in (6), and the fallback rule in (10).
- [x] Cross-link with ASSETIO-001 (asset ingest ownership), GRAPHICS-015 / 015Q (texture residency analogue), GRAPHICS-019 (legacy IO boundaries), GRAPHICS-023A/B/C/D (generation tracking + acknowledgment), GRAPHICS-028 (residency planning), GRAPHICS-029 (bootstrap), GRAPHICS-030 (procedural source + placeholder geometry source), GRAPHICS-031 (default debug material for failure fallback).
- [x] Identify follow-up implementation children (do **not** open here): `GRAPHICS-034-Impl-A/B/C/D/E`.

## Tests
- [x] Planning slice: validators only.
- [x] Implementation children must add `contract;runtime` tests covering the full state machine (including `Failed` fallback and stuck-pending diagnostics), refcount cleanup, generation rebinds, and asset-id normalization unit coverage.
- [x] GPU coverage stays opt-in `gpu;vulkan` and outside the default CPU gate.

## Docs
- [x] Update `docs/architecture/graphics.md` "ECS renderable residency bridge" section with the asset-source plan and its composition with GRAPHICS-023A/B/C/D and GRAPHICS-030.
- [x] Update `src/runtime/README.md`, `src/graphics/assets/README.md`, and `src/graphics/renderer/README.md` cross-links once decisions are recorded.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` rows for asset-backed renderable residency.
- [x] Update `tasks/backlog/rendering/README.md` DAG after GRAPHICS-030 (and after ASSETIO-001 ingest ownership lands).

## Acceptance criteria
- [x] All fourteen decisions recorded with explicit answers and trade-off rationales; state-machine table and ordering are fully enumerated.
- [x] Implementation children identified with scope and dependency gates but not opened.
- [x] No new ingest / loader / format work scheduled here; ASSETIO-001 retains ownership of asset ingest.
- [x] Layering invariants hold; no engine behavior changes land in this slice.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
git --no-pager diff --check
```

## Completion
Completed 2026-06-03 as a planning-only `Scaffolded` slice. The task records the runtime-owned asset-backed geometry residency contract, the future `AssetGeometryCache` and normalization seams, cache/refcount/state-machine semantics, frame ordering, fallback policy, diagnostics, performance requirements, extensibility gates, and layer audit. It intentionally opens no implementation children and changes no engine behavior.

## Forbidden changes
- No new asset format ingestion work (defer to ASSETIO-001 and downstream tasks).
- No GPU-typed ECS components.
- No live ECS or `Asset.Service` access from graphics layers.
- No new `GpuAssetCache` eviction or capacity policy.
- No new hot-reload acknowledgment surface beyond GRAPHICS-023A/B/C/D.
- No silent disappearance of renderables on `Failed` assets; fallback must be visible and counted.
- No mixing of mechanical file moves with semantic refactors.
- No premature opening of implementation child tasks before this planning slice is approved.
