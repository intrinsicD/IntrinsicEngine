# GRAPHICS-030C — Procedural geometry refcount/free retire ordering

## Goal
- Cover the deferred-retire / refcount-cancellation path of `ProceduralGeometryCache` declared by `GRAPHICS-030` Decisions 4 and 10: removing the last referencing entity does not free the geometry handle until `framesInFlight` ticks have elapsed (matched to `Graphics::GpuAssetCache::Tick(currentFrame, framesInFlight)`); resurrecting the same key inside the retire window cancels the retirement; `ProceduralGeometryFreeRetires` increments exactly once per actual `GpuWorld::FreeGeometry()` call.

## Non-goals
- No additional packers (`GRAPHICS-030D`).
- No asset-source cache integration.
- No renderer pass body, no shader/material change.
- No exposure of `GpuWorld` retire diagnostics; this task is bounded by `ProceduralGeometryCache` refcount/free internal state.

## Context
- Status: not started.
- Owner/layer: `runtime`.
- Planning parent: [`tasks/done/GRAPHICS-030-runtime-geometry-residency-bridge.md`](../../done/GRAPHICS-030-runtime-geometry-residency-bridge.md), Decisions 4, 10. Recorded as Impl-C in the parent's Required changes.
- Upstream gates: `GRAPHICS-030B`.
- The cache must be ticked from the runtime side using `Device::GetGlobalFrameNumber()` and `Device::GetFramesInFlight()` (the same inputs Engine already passes to `GpuAssetCache::Tick`).

## Required changes
- [ ] Implement `ProceduralGeometryCache::Tick(currentFrame, framesInFlight)` matching the `GpuAssetCache::Tick` semantics (deferred-retire window keyed on `currentFrame + framesInFlight`).
- [ ] Implement refcount-cancellation: `EnsureResident` of a key currently in the retire queue cancels the queued `FreeGeometry` and increments `ProceduralGeometryRetireCancellations`.
- [ ] Wire `Engine::RunFrame()` (in the maintenance phase, alongside `GpuAssetCache::Tick`) to call `m_RenderExtraction.GetProceduralGeometryCache().Tick(...)`.
- [ ] Append the retire-related counters to `RuntimeRenderExtractionStats` per Decision 10 (`ProceduralGeometryFreeRetires`, `ProceduralGeometryRetireCancellations`, `ProceduralGeometryReleases`).
- [ ] Implement `ProceduralGeometryCache::Release(key)` decrement + retire-queue insertion at refcount-zero.

## Tests
- [ ] `contract;runtime` test: after the last referencing entity is destroyed, `ProceduralGeometryReleases` increments by 1 and `GpuWorld::FreeGeometry()` is **not** called for `framesInFlight - 1` ticks; on tick `framesInFlight`, `FreeGeometry` is called once and `ProceduralGeometryFreeRetires` increments by 1.
- [ ] `contract;runtime` test: re-attaching `ProceduralGeometryRef{ same key }` to a new entity within the retire window cancels the queued free; `ProceduralGeometryRetireCancellations == 1`, `ProceduralGeometryFreeRetires == 0`, the resurrected entity binds the same `GpuGeometryHandle` value as before.
- [ ] `contract;runtime` test: refcount saturation (≥ 2^32 - 1 increments) is fail-closed: increments past the cap reject and increment a `ProceduralGeometryRefCountSaturated` counter instead of overflowing.

## Docs
- [ ] Update `src/runtime/README.md` to document `ProceduralGeometryCache::Tick` placement in the maintenance phase and the retire-window semantics.

## Acceptance criteria
- [ ] No `GpuWorld::FreeGeometry()` call lands within the retire window.
- [ ] Counters reach the recorded values across the resurrection + retire scenarios.
- [ ] No regression in `GRAPHICS-030B` extraction counters.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Coupling the procedural retire window to the asset-cache retire window beyond sharing `framesInFlight`.
- Touching renderer pass bodies, shaders, or pipelines.

## Next verification step
- Implement `Tick` + `Release` + cancellation, exercise the new contract tests, run the verification commands above.
