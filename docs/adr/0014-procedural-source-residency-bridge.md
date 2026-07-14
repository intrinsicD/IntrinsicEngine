# ADR 0014 — Procedural-Source Residency Bridge

- **Status:** Accepted
- **Date:** 2026-05-17
- **Owners:** Runtime extraction (`Runtime::ProceduralGeometryCache`, packers), ECS (CPU-only `ProceduralGeometryRef` component), Graphics (`GpuWorld` upload seam only)
- **Related tasks:** [`tasks/done/GRAPHICS-030`](../../tasks/archive/GRAPHICS-030-runtime-geometry-residency-bridge.md), [`GRAPHICS-030A`](../../tasks/archive/GRAPHICS-030A-procedural-geometry-descriptor-cache.md), [`GRAPHICS-030B`](../../tasks/archive/GRAPHICS-030B-extraction-procedural-geometry-binding.md), [`GRAPHICS-030C`](../../tasks/archive/GRAPHICS-030C-procedural-geometry-retire-ordering.md)
- **Related docs:** [`docs/architecture/graphics.md`](../architecture/graphics.md), [`src/runtime/README.md`](../../src/runtime/README.md)
- **Supersedes:** none. Extracted from the `## Procedural-source residency bridge` section in `docs/architecture/graphics.md` per [`DOCS-001`](../../tasks/archive/DOCS-001-reduce-graphics-architecture-prose.md).
- **Related ADRs:** [ADR-0013](0013-ecs-renderable-residency-bridge.md) records the parent ECS renderable residency bridge contract; this ADR records the procedural-geometry first slice that lands ahead of the asset-backed mesh residency path.

## Context

The parent `GRAPHICS-028` residency bridge ([ADR-0013](0013-ecs-renderable-residency-bridge.md)) covers all renderable entities, but its first delivered slice is procedural geometry (initially a `Triangle` primitive) — chosen because procedural sources have no asset-cache generation tracking, no file-watching, and no external dependencies, so they exercise the bridge skeleton cleanly without coupling to `GpuAssetCache` lifecycle.

`GRAPHICS-030` records the planning contract for that procedural first slice. `GRAPHICS-030A` / `030B` / `030C` are the implementation children:

- **`030A`** lands the `Runtime::ProceduralGeometryCache` value type, the `ProceduralGeometryKey` / `ProceduralGeometryParams` POD surface, and the `Triangle` packer.
- **`030B`** wires `RenderExtractionCache::ExtractAndSubmit()` to detect `ProceduralGeometryRef` entities, ensure-resident, allocate the instance, refresh `GpuSceneSlot`, bind via `GpuWorld::SetInstanceGeometry()`, and consume `DirtyTransform`.
- **`030C`** lands the deferred-retire queue mirroring the `GpuAssetCache::Tick(currentFrame, framesInFlight)` deferred-free contract.

This ADR captures the planning contract as the canonical durable home. `docs/architecture/graphics.md` keeps a short canonical pointer to this ADR; the closed descriptor surface, cache shape, lifecycle ordering, failure-mode counters, performance characteristics, and the GRAPHICS-034 deferral for asset-backed mesh residency live here.

## Decision

### 1. Closed descriptor surface

The procedural descriptor surface is **closed**, exported from a planned module `Extrinsic.Runtime.ProceduralGeometry` at `src/runtime/Runtime.ProceduralGeometry.cppm`:

- `ProceduralGeometryKind` — append-only enum, initial enumerator `Triangle`.
- `ProceduralGeometryParams` — small POD: vertex / index counts plus a fixed-size float payload.

Two sources are the **same allocation** iff `ProceduralGeometryKey = (Kind, Hash(Params))` is equal. The following fields are **explicitly excluded** from the key so N entities sharing the same key share one `Graphics::GpuGeometryHandle`:

- Debug name.
- Source entity ID.
- Owning provider identity.
- Frame index.
- `MetaData::EntityName`.

### 2. Cache shape and refcount lifecycle

`Runtime::ProceduralGeometryCache` is a value type owned as a member of `Runtime::RenderExtractionCache`. Its lifetime is tied to the existing extraction tick.

- `EnsureResident(key, params)`:
  - **Miss:** insert an entry and run the per-kind packer + `GpuWorld::UploadGeometry()` **once**.
  - **Hit:** return the existing entry and increment its `std::uint32_t` refcount.
- `Release(key)` decrements the refcount.
- **On transition to zero:** the entry is moved to a deferred-retire list. `GpuWorld::FreeGeometry()` is issued only after `framesInFlight` ticks have elapsed, mirroring the deferred-free contract that `Graphics::GpuAssetCache::Tick(currentFrame, framesInFlight)` enforces for asset leases.

Per-kind packers live in a sibling module `Extrinsic.Runtime.ProceduralGeometryPacker` whose:

```
Pack(kind, params, scratch) -> std::optional<GeometryUploadDesc>
```

consumes a runtime-owned scratch buffer (packed vertex bytes + surface / line index vectors) reused across ticks.

`GpuWorld` stays domain-agnostic and never imports `Extrinsic.Runtime.ProceduralGeometry*`.

### 3. No GpuAssetCache generation tracking

Procedural sources do **not** participate in `Graphics::GpuAssetCache` generation tracking:

- `GpuSceneSlot::SourceAsset` is left at the default `Assets::AssetId{}` (`HasSourceAsset()` returns `false`).
- The `Runtime::ObserveRenderableAssetGeneration` helper from [ADR-0013](0013-ecs-renderable-residency-bridge.md) §2 step 3 already short-circuits in that case and reports `SourceAssetCacheUnavailableCount` / `SourceAssetViewUnavailableCount` instead of `RebindRequired`.
- **No** new sentinel value or `IsProcedural` flag is added to `GpuSceneSlot`.

The procedural-source link on the entity is a CPU-only ECS component:

```
ECS::Components::ProceduralGeometryRef { Kind, Params }
```

at `src/ecs/Components/ECS.Component.ProceduralGeometryRef.cppm`, importing `Extrinsic.Core.*` only — **no** graphics, RHI, asset, or runtime imports — so the GRAPHICS-028 / [ADR-0013](0013-ecs-renderable-residency-bridge.md) §1 prohibition on GPU-typed ECS components and the `AGENTS.md` §2 `ecs → core` invariant both hold.

### 4. Lifecycle ordering

Lifecycle ordering inside `RenderExtractionCache::ExtractAndSubmit()` is **locked**:

1. Detect candidates carrying `ProceduralGeometryRef`.
2. `EnsureResident(key, params)`.
3. `AllocateInstance` if no sidecar exists.
4. Refresh `GpuSceneSlot`.
5. `GpuWorld::SetInstanceGeometry()` **once per `(instance, geometry)` pair**.
6. Consume `DirtyTransform` → `SetInstanceTransform()` and clear the tag.

This ordering is the seam reviewers must enforce; a reordering that, for example, refreshes `GpuSceneSlot` before `EnsureResident` would let the slot reference a geometry handle that does not yet exist.

### 5. Failure modes and counters

Failures are **fail-closed** and **never throw**. Each failure mode increments a dedicated counter on `RuntimeRenderExtractionStats` and skips the entity for the tick:

| Failure                                | Counter                                            |
|----------------------------------------|----------------------------------------------------|
| Successful upload                      | `ProceduralGeometryUploads`                        |
| Reuse hit on a resident key            | `ProceduralGeometryReuseHits`                      |
| Refcount transition to zero (release)  | `ProceduralGeometryReleases`                       |
| Deferred-retire completes              | `ProceduralGeometryFreeRetires`                    |
| Upload failure                         | `ProceduralGeometryFailedUploads`                  |
| Allocate-instance failure              | `ProceduralGeometryFailedInstanceAlloc`            |
| `SetInstanceGeometry` rejected         | `ProceduralGeometryFailedBinds`                    |
| Missing packer for kind                | `ProceduralGeometryMissingPacker`                  |
| Invalid params (failed validation)     | `ProceduralGeometryInvalidParams`                  |
| Refcount saturation (overflow guard)   | `ProceduralGeometryRefCountSaturated`              |
| Entity has both procedural + asset src | `ProceduralAndAssetSourceConflict`                 |

### 6. Performance characteristics

Steady-state cost is **O(1) per renderable**:

- Already-resident geometry incurs one `unordered_map` lookup and a refcount increment.
- No scratch-buffer touch and no packer invocation in the resident-hit path.

### 7. Extensibility

Adding a new primitive (`Cube`, `Quad`, `Sphere`, `LineStrip`) requires only:

- Enum extension on `ProceduralGeometryKind`.
- Per-kind packer in `Extrinsic.Runtime.ProceduralGeometryPacker`.
- A contract test.

**No** cache or extraction lifecycle changes are required. The descriptor surface stays closed; new primitives append to the enum without renumbering existing values.

### 8. Asset-backed mesh residency deferred to GRAPHICS-034

Asset-backed mesh residency — the `AssetInstance::Source` → `GpuAssetCache` → `GpuWorld` path — and mixed-source entities (both procedural and asset-backed on the same entity, beyond the conflict counter) remain **deferred to `GRAPHICS-034`**. This ADR records the procedural slice only.

Implementation children `GRAPHICS-030-Impl-A` (component + cache + Triangle packer), `Impl-B` (extraction wiring + bind test), `Impl-C` (refcount / free + retire-queue test), and the optional `Impl-D` (second packer) are identified and tracked as `GRAPHICS-030A` / `030B` / `030C` retired tasks.

## Consequences

Positive:

- Procedural residency lands without coupling to `GpuAssetCache` generation tracking or file-watching, so the bridge skeleton is testable in isolation.
- The closed `(Kind, Hash(Params))` key gives content-addressed deduplication: N entities with the same procedural params share one `GpuGeometryHandle` and one upload.
- The deferred-retire queue mirrors the `GpuAssetCache` deferred-free contract, so reviewers have one shape to validate across both caches.
- The `ProceduralGeometryRef` ECS component imports `Extrinsic.Core.*` only, preserving `AGENTS.md` §2 `ecs → core`.
- Adding a new primitive is a 3-step append — enum + packer + test — with no cache or extraction lifecycle churn.
- Failure modes are exhaustively enumerated and counted; a reviewer can verify each `RuntimeRenderExtractionStats` counter has exactly one trigger site.

Trade-offs and risks:

- The `ProceduralGeometryKey` is `(Kind, Hash(Params))`. A collision in `Hash(Params)` would silently alias two distinct parameter sets to the same geometry handle. The hash must be deterministic and wide enough to make collisions practically impossible; reviewers must check that future packers do not hash subsets of `Params` that drop content.
- The deferred-retire queue holds geometry resident for `framesInFlight` ticks after the last release. A scene that thrashes a procedural key on / off every frame will keep up to `framesInFlight` copies alive — this is the intended safety property (prevents GPU-visible free during in-flight frames) but means memory pressure spikes when many distinct keys cycle.
- `ProceduralAndAssetSourceConflict` is reported as a counter and the entity is skipped. A future task that wants to mix procedural and asset sources on the same entity (`GRAPHICS-034` mixed-source path) must replace this hard skip; reviewers must check that the conflict counter is not silently dropped by such a change.
- The `EnsureResident` / `Release` refcount uses `std::uint32_t`. The `ProceduralGeometryRefCountSaturated` counter exists to surface overflow rather than wrap silently; a scene with > 2^32 references to the same procedural key will skip new entities until the count drops.

Follow-up tasks required: none from this ADR. `GRAPHICS-034` will replace §8's deferral when it lands; the optional `GRAPHICS-030-Impl-D` (second packer beyond `Triangle`) lands as needed.

## Alternatives Considered

- **Key on `(Kind, Hash(Params), debug_name, source_entity_id, …)`.** Rejected per §1: would break content-addressed deduplication; N entities with the same procedural params would each pay an upload.
- **`IsProcedural` flag or sentinel `Assets::AssetId` on `GpuSceneSlot`.** Rejected per §3: would grow the slot's surface and force every consumer to demultiplex against a procedural / asset bit. The `HasSourceAsset() == false` path already short-circuits cleanly.
- **`ProceduralGeometryRef` ECS component importing runtime or graphics types.** Rejected per §3: violates `AGENTS.md` §2 `ecs → core`. The component is CPU-only POD with `Core` imports only.
- **Immediate-free on refcount transition to zero.** Rejected per §2: races GPU-visible reads of the geometry within `framesInFlight` ticks. The deferred-retire queue mirrors `GpuAssetCache::Tick` exactly so the safety boundary is the same.
- **Throwing on packer failure or upload failure.** Rejected per §5: every failure must be fail-closed and counted so editors do not crash on a single bad procedural input; the counter set lets diagnostics surface the failure without aborting extraction.
- **Adding a new primitive by extending `ProceduralGeometryParams` with primitive-specific fields.** Rejected per §7: the POD payload is fixed-size; new primitives append to the enum and supply their own packer that reads the existing payload, not new fields.

## Validation

- [`tasks/done/GRAPHICS-030`](../../tasks/archive/GRAPHICS-030-runtime-geometry-residency-bridge.md) records the parent planning contract captured in §§1–8.
- [`tasks/done/GRAPHICS-030A`](../../tasks/archive/GRAPHICS-030A-procedural-geometry-descriptor-cache.md), [`030B`](../../tasks/archive/GRAPHICS-030B-extraction-procedural-geometry-binding.md), and [`030C`](../../tasks/archive/GRAPHICS-030C-procedural-geometry-retire-ordering.md) record the implementation children that land the descriptor surface + cache, the extraction wiring + bind test, and the refcount / free / retire-queue test respectively.
- The default CPU correctness gate (`ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`) exercises `Runtime::ProceduralGeometryCache`, the closed descriptor surface, the `ExtractAndSubmit` ordering, and the deferred-retire queue without requiring a Vulkan device.
