# GRAPHICS-030B — Wire RenderExtraction to the procedural geometry residency bridge

## Goal
- Implement the runtime extraction wiring declared by `GRAPHICS-030` Decision 8: `RenderExtractionCache::ExtractAndSubmit()` detects renderable candidates carrying `ECS::Components::ProceduralGeometryRef`, drives `ProceduralGeometryCache::EnsureResident(...)` (which uploads through the per-kind packer + `GpuWorld::UploadGeometry()` once or hits the existing entry), allocates the renderable instance through the renderer, calls `GpuWorld::SetInstanceGeometry(instance, geometry)`, refreshes `GpuSceneSlot`, and consumes `DirtyTransform`.

## Non-goals
- No refcount/free retire-queue ordering coverage (that is `GRAPHICS-030C`).
- No additional packers (`Cube`/`Quad`/`Sphere`/`LineStrip` are `GRAPHICS-030D`).
- No asset-source residency path (that is `GRAPHICS-034` and gates on `ASSETIO-001`).
- No renderer pass body or pipeline change; geometry binding alone produces no visible pixels until `GRAPHICS-031A`/`031B` and `GRAPHICS-032A`/`B`/`C` land.
- No live ECS access from `src/graphics/*`.

## Context
- Status: not started.
- Owner/layer: `runtime`.
- Planning parent: [`tasks/done/GRAPHICS-030-runtime-geometry-residency-bridge.md`](../../done/GRAPHICS-030-runtime-geometry-residency-bridge.md), Decision 8 (detect / EnsureResident / AllocateInstance / refresh / SetInstanceGeometry / transform-drain ordering) and Decision 10 (counters).
- Upstream gates: `GRAPHICS-030A` (active) — provides `ProceduralGeometryRef`, `Runtime.ProceduralGeometry`, `Runtime.ProceduralGeometryPacker`. `GRAPHICS-029A`/`029B` are required for the bootstrap entity to exist.
- This is the first task that actually calls `GpuWorld::UploadGeometry()` and `GpuWorld::SetInstanceGeometry()` from runtime extraction; today both call counts are zero.

## Required changes
- [ ] Extend `Runtime.RenderExtraction` candidate qualification to also accept `ProceduralGeometryRef` as a valid render-source link, derive the `ProceduralGeometryKey` from `(Kind, Hash(Params))`.
- [ ] Drive the recorded ordering inside `ExtractAndSubmit()`:
  1. detect `ProceduralGeometryRef` on the renderable,
  2. `cache.EnsureResident(key, params)` (returns `(GpuGeometryHandle, bool isFreshUpload)`),
  3. `renderer.AllocateInstance(...)` if the renderable has no live `GpuInstanceHandle` in its sidecar,
  4. refresh `GpuSceneSlot` (set `SourceAsset = Assets::AssetId{}` per the procedural-sentinel rule, mark `LastSeenAssetGeneration` per Decision 7),
  5. `gpuWorld.SetInstanceGeometry(instance, geometry)`,
  6. drain `DirtyTransform` for the entity into the existing transform record path.
- [ ] Append the `RuntimeRenderExtractionStats` counters listed in `GRAPHICS-030` Decision 10: `ProceduralGeometryUploads`, `ProceduralGeometryReuseHits`, `ProceduralAndAssetSourceConflict`, `ProceduralGeometryFailedPack`, `ProceduralGeometryMissingPacker`, `ProceduralGeometryInvalidParams`, `ProceduralAndRenderableSourceConflict`, plus `ProceduralRenderablesEnumerated`.
- [ ] Detect and reject the GRAPHICS-023C invariant violation: if a renderable carries both `AssetInstance::Source` (non-default `AssetId`) and `ProceduralGeometryRef`, increment `ProceduralAndAssetSourceConflict`, skip the procedural path for that entity, and emit a once-per-entity warn breadcrumb.

## Tests
- [ ] `contract;runtime` test: one renderable with `ProceduralGeometryRef{ Triangle }` produces exactly one `GpuInstanceHandle`, exactly one `GpuGeometryHandle`, and `GpuWorld::GetInstanceGeometry(instance) == geometry`. `ProceduralGeometryUploads == 1`, `ProceduralGeometryReuseHits == 0`.
- [ ] `contract;runtime` test: two renderables sharing the same `(Kind, ParamsHash)` key produce one geometry handle and two instance handles. `ProceduralGeometryUploads == 1`, `ProceduralGeometryReuseHits == 1`.
- [ ] `contract;runtime` test: the conflict path increments `ProceduralAndAssetSourceConflict`.
- [ ] `contract;graphics` test (extraction integration): after `ExtractAndSubmit`, the next `IRenderer::PrepareFrame()` sees a `RenderWorld::Renderables` snapshot whose `GpuSceneSlot::SourceAsset` is the procedural sentinel for procedural-source entities.
- [ ] No `gpu`/`vulkan` tests in this slice.

## Docs
- [ ] Update `src/runtime/README.md` to record extraction's procedural-source detection and the new counters.
- [ ] Update `src/graphics/renderer/README.md` if `GpuSceneSlot` consumption assumptions change (per GRAPHICS-023C/D the procedural-sentinel rule is already documented).
- [ ] Refresh `docs/api/generated/module_inventory.md` only if module surfaces changed.

## Acceptance criteria
- [ ] After this task, with `CreateReferenceEngineConfig()` and an operational device gate (Null device is fine), the reference triangle entity reaches the renderer with a valid `(GpuInstanceHandle, GpuGeometryHandle)` pair bound in `GpuWorld`.
- [ ] All new counters reach the values asserted in tests.
- [ ] No new graphics/RHI imports in `Runtime.RenderExtraction` beyond the existing `Extrinsic.Graphics.GpuWorld` value-type edge.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeTests IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Adding asset-backed mesh residency code (reserved for `GRAPHICS-034`).
- Storing GPU handles in canonical ECS components.
- Adding any new packer beyond `Triangle` (reserved for `GRAPHICS-030D`).
- Adding renderer pass bodies, shaders, or pipeline state changes.

## Next verification step
- Implement the wiring + counters, then exercise the contract tests above.
