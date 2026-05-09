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

## Recorded design decisions

Each decision below is locked for downstream implementation children. Trade-offs are recorded so reviewers can see what was rejected and why.

1. **Descriptor shape.**
   - Decision: a closed `enum class ProceduralGeometryKind : std::uint8_t { Triangle };` paired with a small POD `struct ProceduralGeometryParams { std::uint32_t VertexCount = 0; std::uint32_t IndexCount = 0; std::array<float, 8> Payload{}; };` exported from a new module `Extrinsic.Runtime.ProceduralGeometry` at `src/runtime/Runtime.ProceduralGeometry.cppm`. Triangle is the only initial enumerator; future primitives extend the enum and the per-kind packer table without changing the descriptor surface. The payload is intentionally bounded so `ProceduralGeometryParams` stays trivially copyable, hashable, and stable in component value storage.
   - Rejected alternatives: (i) an open virtual `IProceduralGeometrySource` interface with per-kind subclasses — rejected because it forces heap-owned vtables in component storage, breaks deterministic hashing for cache identity (Decision 2), and pushes ownership/lifetime questions into ECS that we explicitly forbid in GRAPHICS-028; (ii) `std::variant<TriangleParams, CubeParams, …>` over per-kind structs — rejected because the variant tag duplicates the enum, and the type set must grow whenever a new packer is added, which defeats the goal of an additive packer table; (iii) free-form `std::span<const std::byte> EncodedParams` — rejected because hashing/identity becomes an opaque-bytes contract that hides packer-version skew.

2. **Cache key and identity.**
   - Decision: `struct ProceduralGeometryKey { ProceduralGeometryKind Kind; std::uint64_t ParamsHash; };` with value-equality and a deterministic stable hash (`Core::HashCombine` over byte-image of `ProceduralGeometryParams`, byte-image stable because `Params` is trivially copyable POD and uninitialized payload bytes are zero-initialized at construction). Two procedural sources are "the same allocation" iff `(Kind, ParamsHash)` are equal. Explicit exclusions (locked): debug name, source entity ID, owning provider identity, frame index, and `MetaData::EntityName` are **not** part of the key. Multiple entities sharing the same `(Kind, ParamsHash)` share one `GpuGeometryHandle`.
   - Rejected alternatives: (i) keying on the entity stable ID — rejected because it eliminates the dedup property and forces N uploads for N instances of the same procedural source; (ii) keying on a packer-emitted byte hash of `GeometryUploadDesc` — rejected because it requires running the packer before lookup, defeating the steady-state "no upload work for already-resident geometry" budget recorded in Decision 11.

3. **Cache placement and ownership.**
   - Decision: a separate `Runtime::ProceduralGeometryCache` value type lives in `Extrinsic.Runtime.ProceduralGeometry` and is owned as a member of `Runtime::RenderExtractionCache` (see `src/runtime/Runtime.RenderExtraction.cppm:98`). Lifetime, locking discipline, and reset semantics match the existing extraction cache: single-threaded extraction tick, constructed when `RenderExtractionCache` is constructed, drained when `RenderExtractionCache::Shutdown()` runs.
   - Rejected alternative: a methods-on-`RenderExtractionCache` extension (no separate type) — rejected because mixing the procedural-cache state machine into `m_Renderables` would force every contract test to spin up a full extraction tick to cover refcount/free behavior, and would prevent unit-testing the cache without wiring `IRenderer`.

4. **Refcount semantics.**
   - Decision: the cache stores one `Entry { ProceduralGeometryKey Key; Graphics::GpuGeometryHandle Handle; std::uint32_t RefCount; }` per resident geometry. `EnsureResident(key, params)` increments `RefCount` (or inserts a new entry with `RefCount = 1` and uploads), returning `Entry::Handle`. `Release(key)` decrements `RefCount`. When `RefCount` reaches zero the entry is **not** freed inline; instead it is moved into a runtime-side retire list and the underlying `GpuGeometryHandle` is freed by the next call to `RenderExtractionCache::ExtractAndSubmit()` after `framesInFlight` ticks have elapsed since release, mirroring the deferred-free contract that `Graphics::GpuAssetCache::Tick(currentFrame, framesInFlight)` already enforces for asset leases (`src/graphics/assets/Graphics.GpuAssetCache.cppm:211`). `RefCount` type is `std::uint32_t`; saturation at `UINT32_MAX` is a hard error breadcrumb (`ProceduralGeometryRefCountSaturated`) and the increment is rejected — there is no scenario in this milestone where a single procedural key has billions of references, so saturation is treated as logic error.
   - Rejected alternative: immediate `GpuWorld::FreeGeometry` on `RefCount → 0` — rejected because GRAPHICS-028 already records that geometry handles can be in flight on the GPU command buffer when an entity is dropped; immediate free races against in-flight rebinds and the existing `GpuWorld` retire window.

5. **Generation tracking and asset-cache interaction.**
   - Decision: procedural sources do not participate in `Graphics::GpuAssetCache` generation comparison. The renderable's `GpuSceneSlot::SourceAsset` is set to a sentinel value `Assets::AssetId{}` (default-constructed strong handle, `IsRegistered()` returns `false`) and `GpuSceneSlot::LastSeenAssetGeneration` is left at zero. The GRAPHICS-023C observer (`Runtime::ObserveRenderableAssetGeneration`) already short-circuits on `!slot.HasSourceAsset()` and reports `SourceAssetCacheUnavailableCount` / `SourceAssetViewUnavailableCount` instead of `RebindRequired`; the GRAPHICS-023D acknowledgment helper similarly does nothing on a slot without a registered source asset. No new sentinel value or branch is added to `GpuSceneSlot`; the existing `HasSourceAsset()` check is the discriminator.
   - Rejected alternatives: (i) introducing a magic non-default `Assets::AssetId` value to mark "procedural" — rejected because it pollutes the `Asset.Registry` strong-handle namespace and forces every observer to special-case the value; (ii) adding a `bool IsProcedural` field to `GpuSceneSlot` — rejected because it adds renderer-component surface that GRAPHICS-023A/B/C/D already cover via `HasSourceAsset()` and would require touching `Graphics.Component.GpuSceneSlot.cppm`, which is out of scope.

6. **`GeometryUploadDesc` packing.**
   - Decision: per-kind packers live in a sibling module `Extrinsic.Runtime.ProceduralGeometryPacker` at `src/runtime/Runtime.ProceduralGeometryPacker.cppm`. The packer surface is a free function `Pack(ProceduralGeometryKind kind, const ProceduralGeometryParams& params, ProceduralGeometryPackBuffer& outBuffer) -> std::optional<Graphics::GeometryUploadDesc>`. `ProceduralGeometryPackBuffer` is a runtime-owned scratch struct that owns the backing `std::vector<std::byte>` (packed vertex bytes), `std::vector<std::uint32_t>` (surface indices), and `std::vector<std::uint32_t>` (line indices) which `GeometryUploadDesc` references via `std::span` (`src/graphics/renderer/Graphics.GpuWorld.cppm:128`). Returned `GeometryUploadDesc::DebugName` is a `const char*` produced from a fixed-storage table (`"Procedural.Triangle"`). `Triangle` is the only in-scope packer; planned but unopened: `Cube`, `Quad`, `Sphere`, `LineStrip`. `GpuWorld` stays domain-agnostic: it never imports `Extrinsic.Runtime.ProceduralGeometry*`.
   - Rejected alternative: per-entity scratch buffers (allocated inside `EnsureResident` and freed at the end of each call) — rejected because a single per-cache scratch is reused across the tick, eliminating per-call allocation churn on the steady-state path.

7. **Renderable sidecar wiring.**
   - Decision: option (a). A new CPU-only ECS component `ECS::Components::ProceduralGeometryRef { ProceduralGeometryKind Kind; ProceduralGeometryParams Params; }` lives at `src/ecs/Components/ECS.Component.ProceduralGeometryRef.cppm`, module `Extrinsic.ECS.Component.ProceduralGeometryRef`. The component contains only POD fields and imports nothing under `src/graphics/*` or `src/runtime/*`. `Runtime.RenderExtraction` reads it during candidate qualification and derives a `ProceduralGeometryKey` on the spot; the runtime sidecar continues to store the `Graphics::Components::GpuSceneSlot` returned from `EnsureResident` plus a copy of the `ProceduralGeometryKey` so `Release(key)` can be issued at sidecar teardown without re-reading the destroyed entity.
   - Rejected alternative: a runtime-side map from stable entity ID to `ProceduralGeometryKey` with no ECS component (option (b)) — rejected because it is not discoverable from the entity, makes contract tests more cumbersome (a test would have to call back into runtime to assert the entity is procedural), and creates a second authoring surface alongside `Graphics::Components::RenderSurface` that the user cannot inspect on the entity. Option (a) is layer-clean (`ecs → core` only) and self-describing for tests.

8. **Lifecycle ordering inside the extraction tick.**
   - Decision: the procedural-source path runs strictly in this order inside `RenderExtractionCache::ExtractAndSubmit()`:
     1. **Detect.** Iterate renderable candidates that already pass the GRAPHICS-016 / 028 qualification and additionally carry `ECS::Components::ProceduralGeometryRef`.
     2. **EnsureResident.** Compute `key = { Kind, Hash(Params) }`. Call `ProceduralGeometryCache::EnsureResident(key, params)`; on insert it invokes the per-kind packer and `GpuWorld::UploadGeometry(desc)` once.
     3. **AllocateInstance.** If the entity has no sidecar, call `Renderer::AllocateInstance(stableEntityId)` and seed the sidecar's `GpuSceneSlot::InstanceSlot/Generation` from the returned handle.
     4. **Refresh GpuSceneSlot.** Set `slot.GeometrySlot/Generation` from the geometry handle, leave `SourceAsset` empty per Decision 5, store the resolved `ProceduralGeometryKey` next to the slot in the sidecar.
     5. **Bind.** Call `GpuWorld::SetInstanceGeometry(instance, geometry)` exactly once per (instance, geometry) pair; subsequent ticks skip when the sidecar already records the same `(InstanceSlot, GeometrySlot)`.
     6. **Transform drain.** Consume `DirtyTransform` → `GpuWorld::SetInstanceTransform(instance, world, prevWorld)` and clear the tag, mirroring the existing GRAPHICS-016 ordering.
   - Tags are consumed only after the relevant call succeeds. The procedural step **runs before** the GRAPHICS-023C `Runtime::ObserveRenderableAssetGeneration()` call so an entity that simultaneously carries `AssetInstance::Source` would have its asset-source observation reported separately; in practice such mixed-source entities are out of scope for this slice (Decision 13) and the runtime breadcrumb `ProceduralAndAssetSourceConflict` is incremented and the asset-side path skipped.

9. **Failure modes.**
   - Decision: forbidden to throw from any extraction-tick code path. Every failure increments a counter, emits one breadcrumb per fail-closed cycle (rate-limited by the GRAPHICS-018Q pattern), and skips the entity for this tick:
     - **Missing packer for the kind:** `ProceduralGeometryMissingPacker`. Skip; do not allocate an instance; retry next tick is a no-op (kind is static), so the sidecar records the failure and stops querying.
     - **`UploadGeometry` returned an invalid handle:** `ProceduralGeometryFailedUploads`. Retry policy: no retry this frame; clear the cache entry insertion attempt so the next tick re-runs `EnsureResident` from scratch. No partial sidecar state.
     - **`AllocateInstance` returned an invalid handle:** `ProceduralGeometryFailedInstanceAlloc`. Skip; do not call `SetInstanceGeometry` against a null instance; no leak (the upload succeeded but is held by the cache entry's refcount, which other entities can still share).
     - **`SetInstanceGeometry` rejected (stale geometry slot):** `ProceduralGeometryFailedBinds`. Skip; on the next tick the sidecar's geometry-slot generation comparison in `RenderExtractionCache` will already mark a rebind required.
     - **Refcount saturation (Decision 4):** `ProceduralGeometryRefCountSaturated`. Reject the increment; the entity is treated as if `EnsureResident` failed.
     - **`ProceduralGeometryRef` present but `Params` packer-rejected (e.g. `VertexCount == 0` for a Triangle expected to have 3):** `ProceduralGeometryInvalidParams`. Skip; record once per (Kind, ParamsHash).
     - **Mixed source conflict (Decision 8):** `ProceduralAndAssetSourceConflict`. Asset-source observation is suppressed for that entity this tick; procedural path proceeds.

10. **Diagnostics.**
    - Decision: the runtime-side counters listed in Decision 9 are added to a new field group on `Runtime::RuntimeRenderExtractionStats` (struct already at `src/runtime/Runtime.RenderExtraction.cppm:70`). The proposed names, exactly:
      - `ProceduralGeometryUploads` — count of new `EnsureResident` cache inserts that drove `GpuWorld::UploadGeometry`.
      - `ProceduralGeometryReuseHits` — count of `EnsureResident` calls that hit an existing entry and incremented refcount.
      - `ProceduralGeometryReleases` — count of `Release` calls (refcount decrements).
      - `ProceduralGeometryFreeRetires` — count of underlying `GpuWorld::FreeGeometry` calls actually issued after the deferred-free window elapsed.
      - `ProceduralGeometryFailedUploads`, `ProceduralGeometryFailedInstanceAlloc`, `ProceduralGeometryFailedBinds`, `ProceduralGeometryMissingPacker`, `ProceduralGeometryInvalidParams`, `ProceduralGeometryRefCountSaturated`, `ProceduralAndAssetSourceConflict` — failure counters from Decision 9.
    - Counters surface through the existing `RuntimeRenderExtractionStats` return value of `ExtractAndSubmit()`; no new diagnostic struct is introduced. The fields are appended to the struct (additive) so existing test assertions continue to compile.

11. **Performance characteristics.**
    - Decision (locked):
      - **O(1) lookup** by `ProceduralGeometryKey` using `std::unordered_map<ProceduralGeometryKey, Entry, ProceduralGeometryKeyHash>` with a custom hash that combines `Kind` and `ParamsHash` deterministically.
      - **No per-frame upload** for already-resident geometry. `EnsureResident` on a hit is one map lookup plus a refcount increment; no scratch buffer touched, no packer invoked.
      - **No per-instance allocations** on the steady-state path. The per-cache `ProceduralGeometryPackBuffer` is reused across ticks; sidecars are re-used across ticks; the deferred-retire list is a `std::vector` with stable amortized-O(1) push.
      - **Dedup across instances.** N entities sharing the same `(Kind, ParamsHash)` produce one `GpuGeometryHandle` and N `GpuInstanceHandle`s.
      - **Forbidden:** per-frame full re-upload for static geometry; per-instance heap allocation in the procedural path; resizing the pack buffer mid-tick (it is sized at first use to the worst case among supported kinds — for the `Triangle` packer, that is a fixed-size 3-vertex / 3-index payload).

12. **Test seams.**
    - Decision: contract tests under `tests/contract/runtime/` (and `tests/contract/graphics/` when the bind path is exercised) use the existing test-only entry points already exposed by `RenderExtractionCache::ExtractAndSubmit()` (returning `RuntimeRenderExtractionStats`) plus `GpuWorld` introspection used by `tests/contract/graphics/Test.MinimalTriangleAcceptance.cpp`. The implementation children add exactly one new test seam: a `RenderExtractionCache::FindRenderableSidecarForTest(stableEntityId) -> std::optional<RenderableSidecarView>` accessor that returns a value-typed view of the private `RenderableSidecar` struct (currently at `src/runtime/Runtime.RenderExtraction.cppm:108`). The view exposes `GpuSceneSlot`, the resolved `ProceduralGeometryKey`, and the cache-recorded refcount; it remains gated to test builds (`#if INTRINSIC_TESTING`-style) so production callers cannot reach into the sidecar. Tests assert on:
      - the diagnostics counters defined in Decision 10,
      - the sidecar's `(InstanceSlot, GeometrySlot)` populated correctly,
      - `GpuWorld` reports the expected geometry binding for the instance,
      - and (refcount/free path) that two consecutive teardowns retire one geometry handle exactly once after `framesInFlight` ticks.

13. **Extensibility forecast.**
    - Decision: adding a new procedural primitive (`Cube`, `Quad`, `Sphere`, `LineStrip`) requires exactly three changes and **no** runtime cache or extraction lifecycle changes:
      1. extend `ProceduralGeometryKind` with the new enumerator;
      2. add the per-kind packer in `Extrinsic.Runtime.ProceduralGeometryPacker`;
      3. add a `tests/contract/runtime/` test asserting the packer's `GeometryUploadDesc` shape and a dedup test against an existing kind.
    - Out of scope and explicitly **not** opened in this slice: the `Cube`/`Quad`/`Sphere`/`LineStrip` packers themselves and any policy for parameterized payload growth beyond the eight-float `Payload` array. If a future primitive needs more payload bytes, the descriptor surface (Decision 1) is widened in a follow-up planning slice, **not** by ad-hoc in-tree growth. Mixed source entities (procedural + asset on the same renderable) are deferred to GRAPHICS-034 follow-up.

14. **Layering audit.**
    - Decision: the new dependency edges introduced by GRAPHICS-030 implementation children are exactly:
      - `src/ecs/Components/ECS.Component.ProceduralGeometryRef.cppm` imports `Extrinsic.Core.*` only (POD enum + POD params struct). It does **not** import `Extrinsic.Graphics.*`, `Extrinsic.RHI.*`, `Extrinsic.Asset.*`, or `Extrinsic.Runtime.*`. This satisfies AGENTS.md §2 `ecs → core` and the GRAPHICS-028 prohibition on GPU-typed ECS components.
      - `src/runtime/Runtime.ProceduralGeometry.cppm` imports `Extrinsic.Core.*`, `Extrinsic.ECS.Component.ProceduralGeometryRef`, and `Extrinsic.Graphics.GpuWorld` (for `GpuGeometryHandle` and `GeometryUploadDesc` value types — already imported by `Runtime.RenderExtraction`, see `src/runtime/Runtime.RenderExtraction.cppm:30`). No new graphics edge is introduced.
      - `src/runtime/Runtime.ProceduralGeometryPacker.cppm` imports `Extrinsic.Core.*`, `Extrinsic.Runtime.ProceduralGeometry`, and `Extrinsic.Graphics.GpuWorld` (for `GeometryUploadDesc`). Same edge as above; no new graphics dependency.
      - `src/runtime/Runtime.RenderExtraction.cppm` adds imports of `Extrinsic.ECS.Component.ProceduralGeometryRef` and the two new runtime modules. No new edge into graphics, RHI, or assets.
    - `src/graphics/*` is unchanged — `GpuWorld` continues to expose only its existing surface (Decision 6); no graphics module imports `Extrinsic.Runtime.*`. `assets` is untouched. The promoted layering invariants `ecs → core`, `runtime → core, ecs, graphics-value-types`, and "no live ECS in graphics" all hold.

## Required changes
- ✅ Recorded the fourteen design decisions above with explicit answers, rejected-alternative rationale, and exact identifier references to the current codebase.
- ✅ Cross-linked decisions with GRAPHICS-004 (GpuWorld allocation/lifetime), GRAPHICS-016 (extraction handoff), GRAPHICS-023A/B/C/D (asset-generation observation/rebind/acknowledgment, and the Decision 5 procedural-sentinel rule), and GRAPHICS-028 (residency planning).
- ✅ Identified follow-up implementation children (do **not** open them here):
  - **GRAPHICS-030-Impl-A** — author `Extrinsic.ECS.Component.ProceduralGeometryRef`, `Extrinsic.Runtime.ProceduralGeometry` (descriptor types, key+hash, `ProceduralGeometryCache` value type with `EnsureResident`/`Release` and the deferred-retire list), and `Extrinsic.Runtime.ProceduralGeometryPacker` (Triangle packer only, sized scratch buffer). No extraction wiring lands here. Tests: `contract;runtime` covering descriptor identity, dedup hits, refcount-only behavior driven directly against `ProceduralGeometryCache` (no `IRenderer`).
  - **GRAPHICS-030-Impl-B** — wire `RenderExtractionCache::ExtractAndSubmit()` to detect `ProceduralGeometryRef`, drive `EnsureResident` → `AllocateInstance` → `SetInstanceGeometry` per Decision 8, refresh `GpuSceneSlot`, and consume `DirtyTransform`. Tests: `contract;runtime` (and `contract;graphics` for the bind path) — exactly one renderable produces an instance bound to a triangle geometry handle, two renderables sharing the same key produce one geometry handle and two instance handles, and the new diagnostics counters (Decision 10) reach the values asserted.
  - **GRAPHICS-030-Impl-C** — refcount/free path + retire-queue ordering test. Tests: removing the last referencing entity does not free the geometry handle until `framesInFlight` ticks have elapsed; resurrecting the same key inside the retire window cancels the retirement; `ProceduralGeometryFreeRetires` increments exactly once per actual `GpuWorld::FreeGeometry` call.
  - **GRAPHICS-030-Impl-D** *(optional, gated)* — open one additional packer (Cube or Quad) once Impl-A/B/C land. Gated behind a follow-up review per Decision 13.

## Tests
- Planning slice (this task): task-policy, doc-link, and layering validators only — no engine behavior or test-source changes land here.
- Future implementation children must add `contract;runtime` (and `contract;graphics` where they cross into renderer state) tests covering: descriptor identity, dedup hits, instance/geometry binding, refcount free under retire-queue rules, failure-mode counters.
- GPU coverage stays opt-in `gpu;vulkan` and outside the default CPU gate.

## Docs
- Update `docs/architecture/graphics.md` "ECS renderable residency bridge" section with the procedural-source plan and the asset-source deferral to GRAPHICS-034.
- Update `src/runtime/README.md` Public-module-surface "Planned modules" table with rows for `Extrinsic.Runtime.ProceduralGeometry`, `Extrinsic.Runtime.ProceduralGeometryPacker`, and `Extrinsic.ECS.Component.ProceduralGeometryRef`.
- Update `src/graphics/renderer/README.md` cross-link to the new procedural-source path; no graphics-module surface change.
- Update `tasks/backlog/rendering/README.md` and `tasks/backlog/README.md` so the GRAPHICS-030 entry points to `tasks/done/`.

## Acceptance criteria
- All fourteen decisions are recorded with explicit answers and trade-off rationales. ✅
- Implementation children are identified with scope and dependency gates but not opened. ✅
- Layering invariants and source-tree placement are unambiguous. ✅
- No engine behavior or build changes land in this slice. ✅

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

## Completion
- Completed: 2026-05-09.
- Commit reference: planning-only slice on branch `claude/setup-agentic-workflow-P0rWw`; no code/test/CMake changes landed.
- Notes:
  - All fourteen design decisions are mirrored in `docs/architecture/graphics.md` (new "Procedural-source residency bridge" subsection adjacent to the ECS renderable residency bridge section), `src/runtime/README.md` (planned-module rows for the procedural cache, packer, and ECS component), and `src/graphics/renderer/README.md` cross-link.
  - GRAPHICS-030-Impl-A/B/C/D are identified but explicitly **not** opened. Impl-A is unblocked once this planning slice is approved; Impl-B depends on Impl-A; Impl-C depends on Impl-B; Impl-D is gated as recorded in Decision 13.
  - No implementation, shader, renderer pass, or ECS component changes landed in this slice.
