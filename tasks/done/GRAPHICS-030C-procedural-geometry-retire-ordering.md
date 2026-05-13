# GRAPHICS-030C — Procedural geometry refcount/free retire ordering

## Goal
- Cover the deferred-retire / refcount-cancellation path of `ProceduralGeometryCache` declared by `GRAPHICS-030` Decisions 4 and 10: removing the last referencing entity does not free the geometry handle until `framesInFlight` ticks have elapsed (matched to `Graphics::GpuAssetCache::Tick(currentFrame, framesInFlight)`); resurrecting the same key inside the retire window cancels the retirement; `ProceduralGeometryFreeRetires` increments exactly once per actual `GpuWorld::FreeGeometry()` call.

## Non-goals
- No additional packers (`GRAPHICS-030D`).
- No asset-source cache integration.
- No renderer pass body, no shader/material change.
- No exposure of `GpuWorld` retire diagnostics; this task is bounded by `ProceduralGeometryCache` refcount/free internal state.

## Context
- Status: done (2026-05-13).
- Owner/layer: `runtime`.
- Branch: `claude/setup-agentic-workflow-B2joE`.
- Planning parent: [`tasks/done/GRAPHICS-030-runtime-geometry-residency-bridge.md`](GRAPHICS-030-runtime-geometry-residency-bridge.md), Decisions 4, 10. Recorded as Impl-C in the parent's Required changes.
- Upstream gates: `GRAPHICS-030B`.
- The cache must be ticked from the runtime side using `Device::GetGlobalFrameNumber()` and `Device::GetFramesInFlight()` (the same inputs Engine already passes to `GpuAssetCache::Tick`).

## Slice plan
1. Extend `ProceduralGeometryCache` with a deferred retire queue (`RetireRecord{ Key, DeadlineSet, Deadline }`), a `FreeFn` callable type, `Tick(currentFrame, framesInFlight, const FreeFn&)`, and new `RetireCancellations`/`FreeRetires` counters. `Release` enqueues only on the refcount-zero transition; `EnsureResident` resurrects entries in the retire window and increments `RetireCancellations`. Refcount saturation already rejects increments — verify via a test seam that primes refcount to `UINT32_MAX` without 2^32 calls.
2. Surface the four new counters (`ProceduralGeometryReleases`, `ProceduralGeometryFreeRetires`, `ProceduralGeometryRetireCancellations`, `ProceduralGeometryRefCountSaturated`) on `RuntimeRenderExtractionStats` as per-tick deltas captured around the cache's `Stats()` reads, matching the existing `Uploads`/`ReuseHits` pattern.
3. Wire `Engine::RunFrame()` maintenance phase (alongside `GpuAssetCache::Tick`) to call into the procedural cache via a `RenderExtractionCache::TickProceduralGeometry(currentFrame, framesInFlight, renderer)` wrapper that closes over `GpuWorld::FreeGeometry`. This keeps the cache opaque to Engine.
4. Add `contract;runtime` tests in `tests/contract/runtime/Test.ProceduralGeometryCache.cpp` for the deferred-retire window, resurrection cancellation (same handle reused), and refcount saturation via the test seam.
5. Add a `contract;runtime` test in `tests/contract/runtime/Test.ProceduralGeometryExtraction.cpp` that exercises the runtime wiring: destroy the procedural entity, call `TickProceduralGeometry` enough times to retire, observe `GpuWorld::GetLiveGeometryCount` falling to zero and `RuntimeRenderExtractionStats.ProceduralGeometryFreeRetires` incrementing.
6. Update `src/runtime/README.md` with the maintenance-phase Tick placement and retire-window semantics.

## Required changes
- [x] Implement `ProceduralGeometryCache::Tick(currentFrame, framesInFlight, freeFn)` matching the `GpuAssetCache::Tick` semantics (deferred-retire window keyed on `currentFrame + framesInFlight`). Implemented in `src/runtime/Runtime.ProceduralGeometry.{cppm,cpp}`.
- [x] Implement refcount-cancellation: `EnsureResident` of a key currently in the retire queue cancels the queued `FreeGeometry` and increments `ProceduralGeometryRetireCancellations`. Implemented via `CancelPendingRetire` in `Runtime.ProceduralGeometry.cpp`; resurrection returns the bit-identical `GpuGeometryHandle`.
- [x] Wire `Engine::RunFrame()` (in the maintenance phase, alongside `GpuAssetCache::Tick`) to drive the cache. Implemented via the new `RenderExtractionCache::TickProceduralGeometry(currentFrame, framesInFlight, renderer)` wrapper called from the `AssetHooks::TickAssets` maintenance hook in `src/runtime/Runtime.Engine.cpp`. The wrapper closes over `GpuWorld::FreeGeometry` so the cache stays opaque to `Engine`.
- [x] Append the retire-related counters to `RuntimeRenderExtractionStats` per Decision 10 (`ProceduralGeometryReleases`, `ProceduralGeometryFreeRetires`, `ProceduralGeometryRetireCancellations`, `ProceduralGeometryRefCountSaturated`). Deltas are computed against a `m_PrevProceduralStats` snapshot so out-of-extraction `Tick` increments surface in the next `ExtractAndSubmit`.
- [x] Implement `ProceduralGeometryCache::Release(key)` decrement + retire-queue insertion at refcount-zero (only on the zero transition; redundant releases against an already-zero entry are noops).

## Tests
- [x] `contract;runtime` test `ProceduralGeometryCacheTest.ReleaseAtZeroDoesNotFreeUntilRetireWindowElapses`: after the last referencing entity is destroyed, `Stats().Releases` increments by 1 and the free callback is not invoked for `framesInFlight - 1` post-anchor ticks; on the `framesInFlight`-th tick, the free fires once and `Stats().FreeRetires` increments by 1.
- [x] `contract;runtime` test `ProceduralGeometryCacheTest.ResurrectInRetireWindowCancelsFreeAndReusesHandle`: re-attaching the same key within the retire window cancels the queued free; `Stats().RetireCancellations == 1`, `Stats().FreeRetires == 0`, and the resurrected handle equals the original `GpuGeometryHandle` bit-for-bit.
- [x] `contract;runtime` test `ProceduralGeometryCacheTest.RefCountSaturationRejectsFurtherIncrements`: refcount saturation is fail-closed via the `PrimeRefCountForTest` seam; increments past the cap reject and bump `Stats().RefCountSaturated` instead of overflowing.
- [x] `contract;runtime` runtime-wiring tests `ProceduralGeometryExtraction.EntityDestructionRetiresGeometryAfterDeferredWindow` and `ProceduralGeometryExtraction.RecreateProceduralEntityCancelsRetireAndKeepsHandle` exercise the full `RenderExtractionCache → ProceduralGeometryCache → GpuWorld` path through `TickProceduralGeometry`.

## Docs
- [x] `src/runtime/README.md` updated: the `Extrinsic.Runtime.ProceduralGeometry` module table entry now describes `Tick`/`Release`/resurrection semantics, the maintenance-phase frame-loop step lists `TickProceduralGeometry`, and the `RenderExtractionCache` paragraph documents the deferred-retire policy, the four new counters, and the `PrimeRefCountForTest` seam.

## Acceptance criteria
- [x] No `GpuWorld::FreeGeometry()` call lands within the retire window (verified by the runtime-wiring test's `GetLiveGeometryCount` assertions during the framesInFlight loop).
- [x] Counters reach the recorded values across the resurrection + retire scenarios.
- [x] No regression in `GRAPHICS-030B` extraction counters: the prior 5 `ProceduralGeometryExtraction` tests pass unchanged.

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

## Completion record
- Completed: 2026-05-13.
- Branch: `claude/setup-agentic-workflow-B2joE`.
- Commit: `a895d08` ("GRAPHICS-030C: defer procedural geometry frees by framesInFlight ticks").
- PR: TBD.
- Verification run in this session:
  - `cmake --preset ci` — configure succeeds (clang-20 toolchain pinned by preset).
  - `cmake --build --preset ci --target IntrinsicTests` — builds clean.
  - `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` — 1836/1836 passed; the new procedural retire-ordering tests under the `contract;runtime` label all pass.
  - `python3 tools/repo/check_layering.py --root src --strict` — 0 layering violations.
  - `python3 tools/docs/check_doc_links.py --root .` — 0 broken links.
  - `python3 tools/agents/check_task_policy.py --root . --strict` — 0 findings.
  - `python3 tools/repo/check_test_layout.py --root . --strict` — 0 findings.
  - `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md` — no diff (no new module surface added; updates are to existing modules).
