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
- Status: done (2026-05-12).
- Owner/agent: claude.
- Branch: `claude/setup-agentic-workflow-ngYZM`.
- Owner/layer: `runtime`.
- Planning parent: [`tasks/done/GRAPHICS-030-runtime-geometry-residency-bridge.md`](GRAPHICS-030-runtime-geometry-residency-bridge.md), Decision 8 (detect / EnsureResident / AllocateInstance / refresh / SetInstanceGeometry / transform-drain ordering) and Decision 10 (counters).
- Upstream gates: `GRAPHICS-030A` (done) — provides `ProceduralGeometryRef`, `Runtime.ProceduralGeometry`, `Runtime.ProceduralGeometryPacker`. `GRAPHICS-029A`/`029B` are done; the reference triangle entity exists.
- This is the first task that actually calls `GpuWorld::UploadGeometry()` and `GpuWorld::SetInstanceGeometry()` from runtime extraction; today both call counts are zero.

## Required changes
- [x] Extend `Runtime.RenderExtraction` candidate qualification to also accept `ProceduralGeometryRef` as a valid render-source link, derive the `ProceduralGeometryKey` from `(Kind, Hash(Params))`.
- [x] Drive the recorded ordering inside `ExtractAndSubmit()`:
  1. detect `ProceduralGeometryRef` on the renderable,
  2. `cache.EnsureResident(key, params)` (returns `GpuGeometryHandle`; first-time inserts call the injected upload functor exactly once, subsequent hits return the cached handle with refcount increment),
  3. `renderer.AllocateInstance(...)` if the renderable has no live `GpuInstanceHandle` in its sidecar,
  4. refresh `GpuSceneSlot` (set `SourceAsset = Assets::AssetId{}` per the procedural-sentinel rule via `GpuSceneSlot::ClearSourceAsset()`, then `SetGeometryHandle(handle)` to advance the slot/generation pair),
  5. `gpuWorld.SetInstanceGeometry(instance, geometry)`,
  6. drain `DirtyTransform` for the entity into the existing transform record path.
- [x] Append the new counter fields to `RuntimeRenderExtractionStats`: `ProceduralRenderablesEnumerated`, `ProceduralGeometryUploads`, `ProceduralGeometryReuseHits`, `ProceduralGeometryFailedPack`, `ProceduralGeometryMissingPacker`, `ProceduralGeometryInvalidParams`, `ProceduralAndAssetSourceConflict`, `ProceduralAndRenderableSourceConflict` (declared; not yet fired — reserved for asset-backed renderable conflict detection in GRAPHICS-034).
- [x] Detect and reject the GRAPHICS-023C invariant violation: if a renderable carries both `AssetInstance::Source` (non-default `AssetId`) and `ProceduralGeometryRef`, increment `ProceduralAndAssetSourceConflict` and skip the procedural path for that entity (the asset-source observation block continues to run as before).

## Tests
- [x] `contract;runtime` test (`ProceduralGeometryExtraction.SingleRenderableProducesOneInstanceAndOneGeometry`): one renderable with `ProceduralGeometryRef{Triangle}` yields `AllocatedInstanceCount == 1`, `ProceduralGeometryUploads == 1`, `ProceduralGeometryReuseHits == 0`, `gpuWorld.GetLiveInstanceCount() == 1`, `gpuWorld.GetLiveGeometryCount() == 1`.
- [x] `contract;runtime` test (`ProceduralGeometryExtraction.TwoRenderablesSharingKeyShareGeometryAndDedup`): two renderables with the same key produce one geometry, two instances, `ProceduralGeometryUploads == 1`, `ProceduralGeometryReuseHits == 1`.
- [x] `contract;runtime` test (`ProceduralGeometryExtraction.GpuWorldReportsBoundGeometryForProceduralInstance`): asserts `GpuWorld::GetInstanceGeometry(instance) == geometry` via the new `RenderExtractionCache::FindRenderableSidecarForTest(stableId)` test seam.
- [x] `contract;runtime` test (`ProceduralGeometryExtraction.AssetAndProceduralSourcesOnSameEntityIncrementConflict`): conflict path increments `ProceduralAndAssetSourceConflict` and the asset observation still runs.
- [x] `contract;runtime` test (`ProceduralGeometryExtraction.ProceduralSourceClearsSlotSourceAssetSentinel`): no `SourceAssetObservationCount` / `SourceAssetRebindRequiredCount` ticks for procedural-only entities.
- [x] No `gpu`/`vulkan` tests in this slice.

## Docs
- [x] Update `src/runtime/README.md` to record extraction's procedural-source detection, the new counters, the procedural sentinel rule, and the new test seams.
- [x] `src/graphics/renderer/README.md` unchanged — `GpuSceneSlot::SourceAsset` procedural-sentinel rule was already documented by GRAPHICS-023C/D; `GpuWorld::GetInstanceGeometry` is an additive read-only accessor.
- [x] `docs/api/generated/module_inventory.md` regenerated; the generator reports no public-module-surface diff (the added counter fields and `GetInstanceGeometry` accessor live inside existing modules).

## Acceptance criteria
- [x] After this task, with the `CreateReferenceEngineConfig()` pipeline and the Null device, the reference triangle entity reaches the renderer with a valid `(GpuInstanceHandle, GpuGeometryHandle)` pair bound in `GpuWorld` (verified by `ProceduralGeometryExtraction.GpuWorldReportsBoundGeometryForProceduralInstance` and by `gpuWorld.GetLiveGeometryCount() == 1` in the single- and dedup-renderable tests).
- [x] All new counters reach the values asserted in tests.
- [x] No new graphics/RHI imports in `Runtime.RenderExtraction` beyond the existing `Extrinsic.Graphics.GpuWorld` value-type edge (the only new imports are `Extrinsic.ECS.Component.ProceduralGeometryRef`, `Extrinsic.Runtime.ProceduralGeometry`, and `Extrinsic.Runtime.ProceduralGeometryPacker`; layering check passes with no new allowlist entries).

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

## Completion
- Completed: 2026-05-12.
- Commit reference: landed on branch `claude/setup-agentic-workflow-ngYZM`; the implementation commit's SHA is recorded in `git log` on that branch under the `GRAPHICS-030B` subject line.
- Verification (all run in this session against the configured `ci` preset):
  - `cmake --preset ci`
  - `cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicGraphicsContractTests IntrinsicTests`
  - `ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` — 64/64 contract tests pass, including the five new `ProceduralGeometryExtraction.*` cases.
  - `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` — 1830/1830 default CPU gate tests pass (no regressions).
  - `python3 tools/repo/check_layering.py --root src --strict` — clean (no new allowlist entries).
  - `python3 tools/docs/check_doc_links.py --root .` — clean.
  - `python3 tools/agents/check_task_policy.py --root . --strict` — 203 task files validated, 0 findings.
  - `python3 tools/repo/check_test_layout.py --root . --strict` — clean.
  - `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md` — no diff against the committed inventory.
- Notes:
  - `GpuWorld::GetInstanceGeometry(GpuInstanceHandle)` is the only new public method on `Extrinsic.Graphics.GpuWorld`; it is a read-only accessor used by the new contract test and is needed for `GRAPHICS-030C` retire-ordering coverage.
  - Procedural retire ordering, refcount → 0 free-window semantics, and the `ProceduralGeometryFreeRetires` counter live in `GRAPHICS-030C`. This slice releases the cached refcount when a renderable sidecar retires (or on `Shutdown()`) but does not call `GpuWorld::FreeGeometry` from the procedural cache — `GRAPHICS-030C` will add the deferred-free path.
  - `ProceduralAndRenderableSourceConflict` is declared on `RuntimeRenderExtractionStats` per the task contract but is never incremented in this slice. Asset-backed renderable conflict detection is `GRAPHICS-034`, which lands the second source path that can collide with a `ProceduralGeometryRef`.
