# GRAPHICS-030 — Procedural-source geometry residency bridge (planning)

## Goal
Lock down the runtime-owned residency-bridge contract for procedural geometry sources — descriptor shape, cache identity and refcounting, generation tracking, failure modes, performance characteristics, and extensibility to additional procedural primitives — before any code lands. This is the first concrete implementation slice of the GRAPHICS-028 planning contract; this task records its design only.

## Non-goals
- No implementation, no new build modules, no new runtime extraction behavior in this slice.
- No asset-backed mesh residency (`AssetInstance::Source` → `GpuAssetCache` → `GpuWorld`); that lives in GRAPHICS-034.
- No new render passes, shader changes, material registry expansion, or pipeline selection (GRAPHICS-031 / GRAPHICS-032).
- No live ECS access from `src/graphics/*`; bridge stays in `runtime` per AGENTS.md §2.
- No GPU-typed ECS components; residency state is runtime-owned sidecar/cache data per GRAPHICS-028.
- No expansion of `GpuWorld` / `GpuAssetCache` capacity, compaction, or eviction policy (GRAPHICS-004 / 005 / 015 own those).
- No procedural primitive expansion beyond the minimal descriptor surface needed for the test seam.

## Context
- Owner layer: `runtime`. Implementation home is `Runtime.RenderExtraction` (or a sibling `Runtime.RenderResidency`) — the only writer of the renderable sidecar/cache per GRAPHICS-028.
- `src/graphics/renderer/Graphics.GpuWorld.cppm` already exposes `UploadGeometry(const GeometryUploadDesc&)`, `FreeGeometry(GpuGeometryHandle)`, and `SetInstanceGeometry(GpuInstanceHandle, GpuGeometryHandle)`.
- `tests/contract/graphics/Test.MinimalTriangleAcceptance.cpp` proves the CPU-side renderer world hosts a triangle; the path is invoked manually and is not wired from runtime extraction.
- `Extrinsic::Graphics::Components::GpuSceneSlot` already carries `InstanceSlot/Generation`, `GeometrySlot/Generation`, `SourceAsset`, and `LastSeenAssetGeneration` (landed in GRAPHICS-023A); this task uses that field set without expanding it.
- The 2026-05-08 review (sections "Exact missing pieces / 2 + 3" and "minimal milestone plan / 2") records this gap.

## Design decisions to record
1. **Descriptor shape.** Decide the procedural geometry descriptor type. Recommend a closed `enum class ProceduralGeometryKind { Triangle, … }` paired with a small POD `ProceduralGeometryParams` (vertex count, index count, tightly bounded payload) under `src/runtime/Runtime.ProceduralGeometry.cppm`. Forbid an open virtual interface here — extension to new primitive kinds happens by extending the enum and the per-kind packer, keeping cache keys hashable and dedup deterministic. Record the alternative considered (open `IProceduralGeometrySource` interface) and why it was rejected for the first milestone.
2. **Cache key and identity.** Decide what makes two procedural sources "the same allocation" in the cache. Recommend a value-equal `ProceduralGeometryKey { ProceduralGeometryKind, ProceduralGeometryParamsHash }`; deterministic stable hash; enumerate exclusions (debug name, source entity ID) explicitly so identity is not over-constrained.
3. **Cache placement and ownership.** Decide whether the procedural cache is a new runtime-owned struct (`Runtime::ProceduralGeometryCache`) or a method-set on the existing `RenderExtractionCache`. Recommend a separate struct owned by `Runtime.RenderExtraction`'s context so its lifetime, locking discipline (single-threaded extraction tick), and test seams are explicit.
4. **Refcount semantics.** Decide refcount type (`std::uint32_t`), incremented at `EnsureResident`, decremented at `Release`, freeing the underlying `GpuGeometryHandle` only when the count hits zero. Record the deferred-free interaction: free goes through the existing `GpuWorld` retire queue and `GpuAssetCache::Tick(currentFrame, framesInFlight)` ordering — never an immediate `FreeGeometry` while the geometry can still be in flight.
5. **Generation tracking.** Decide that procedural sources do not participate in `GpuAssetCache` generation comparison (no asset ID); record the sentinel value used in `GpuSceneSlot::SourceAsset` for procedural-source residency and how observers (GRAPHICS-023C) skip rebind classification for that sentinel without false positives.
6. **`GeometryUploadDesc` packing.** Decide the per-kind packer location (suggested `Runtime.ProceduralGeometryPacker.cppm`), its required outputs (`GeometryUploadDesc` plus diagnostic name), and the rule that `GpuWorld` stays domain-agnostic. Triangle packer is the single in-scope packer; identify but do not open packers for cube/quad/sphere/line-strip.
7. **Renderable sidecar wiring.** Decide where the procedural-source link lives on the renderable. Two options: (a) a small CPU-only `ECS::Components::ProceduralGeometryRef { ProceduralGeometryKey }` (core-only, no GPU types); or (b) a runtime-side map from stable entity ID to procedural key, with no ECS component. Record the trade-off (ECS-side option is more discoverable; runtime-side map keeps ECS smaller). Recommend (a) because it is layer-clean and self-describing for tests.
8. **Lifecycle ordering inside extraction tick.** Lock ordering: (1) detect new candidates with `ProceduralGeometryRef`, (2) `EnsureResident(key)` → upload-or-reuse, (3) `AllocateInstance` if no existing slot, (4) refresh `GpuSceneSlot`, (5) `SetInstanceGeometry(instance, geometry)`, (6) consume `DirtyTransform` → `SetInstanceTransform`. Tags consumed only after the relevant call succeeds.
9. **Failure modes.** Enumerate: missing packer for the kind (diagnostic counter + skip with breadcrumb), `UploadGeometry` failure (counter + retry policy = "no retry this frame, retry next tick"), allocator exhaustion (counter + skip; no crash). Forbid throwing from extraction-tick paths.
10. **Diagnostics.** Name the runtime-side counters explicitly (e.g. `ProceduralGeometryUploads`, `ProceduralGeometryReuseHits`, `ProceduralGeometryFreeRetires`, `ProceduralGeometryFailedUploads`); decide where they surface (`RenderExtractionDiagnostics`).
11. **Performance characteristics.** Record: O(1) lookup by key, no per-frame upload for already-resident geometry, no per-instance allocations on the steady-state path, dedupe across instances of the same key. Forbid per-frame full re-upload for static geometry.
12. **Test seams.** Decide the API surface used by contract tests: a runtime entry point that returns `GpuSceneSlot` for a stable entity ID, plus introspection on `GpuWorld` for instance/geometry binding (reusing what `Test.MinimalTriangleAcceptance.cpp` exercises).
13. **Extensibility forecast.** Enumerate the work required to add cube / quad / sphere / line-strip: (i) extend `ProceduralGeometryKind`, (ii) add per-kind packer, (iii) add contract test. Confirm none of those need cache or extraction changes.
14. **Layering audit.** Record that `src/ecs/*` adds at most a CPU-only `ProceduralGeometryRef` (no graphics imports), `src/graphics/*` is unchanged, `src/runtime/*` owns the cache and packers. No `assets` dependency.

## Required changes
- Capture all fourteen decisions in this task body before any implementation child is opened.
- Cross-link decisions with GRAPHICS-004 (allocation/lifetime), GRAPHICS-016 (extraction handoff), GRAPHICS-023A/B/C/D (asset-generation observation/rebind, including the procedural-sentinel rule), and GRAPHICS-028 (residency planning).
- Identify follow-up implementation children (do **not** open them here):
  - **GRAPHICS-030-Impl-A** — `Runtime.ProceduralGeometry`/`ProceduralGeometryPacker` modules + cache struct + diagnostics; no extraction wiring yet.
  - **GRAPHICS-030-Impl-B** — extraction wiring + `GpuSceneSlot` refresh + `SetInstanceGeometry` call; contract test for the bind path.
  - **GRAPHICS-030-Impl-C** — refcount/free path + retire-queue ordering test.
  - **GRAPHICS-030-Impl-D** (optional) — second packer (cube or quad) gated behind a follow-up review.

## Tests
- Planning slice: validators only.
- Implementation children must add `contract;runtime` (and `contract;graphics` where they cross into renderer state) tests covering: descriptor identity, dedup hits, instance/geometry binding, refcount free under retire-queue rules, failure-mode counters.
- GPU coverage stays opt-in `gpu;vulkan` and outside the default CPU gate.

## Docs
- Update `docs/architecture/graphics.md` "ECS renderable residency bridge" section with the procedural-source plan and the asset-source deferral to GRAPHICS-034.
- Update `src/runtime/README.md` and `src/graphics/renderer/README.md` cross-links once decisions are recorded.
- Update `tasks/backlog/rendering/README.md` DAG; this task gates GRAPHICS-030-Impl-A/B/C/D.

## Acceptance criteria
- All fourteen decisions are recorded with explicit answers and trade-off rationales.
- Implementation children are identified with scope and dependency gates but not opened.
- Layering invariants and source-tree placement are unambiguous.
- No engine behavior or build changes land in this slice.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No implementation, no new build modules, no GPU-typed ECS components.
- No live ECS access from graphics layers.
- No asset-backed mesh residency in this slice.
- No renderer pass body or device backend changes.
- No `GpuWorld` / `GpuAssetCache` capacity or eviction expansions.
- No mixing of mechanical file moves with semantic refactors.
- No premature opening of implementation child tasks before this planning slice is approved.
