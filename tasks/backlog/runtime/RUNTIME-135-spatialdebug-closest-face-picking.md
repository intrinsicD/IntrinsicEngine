---
id: RUNTIME-135
theme: F
depends_on: [GEOM-039]
maturity_target: CPUContracted
---
# RUNTIME-135 — SpatialDebug closest-face picking via accelerated mesh query

## Goal
- Wire the GEOM-039 accelerated mesh closest-face query into the runtime SpatialDebug path so a world-space cursor point (or pick-ray hit point) resolves and highlights the nearest mesh face of the active mesh entity for inspection/picking.
- Compose, in the runtime layer, an exact-nearest-face consumer that builds/refreshes the per-face spatial index for the active mesh entity, resolves `{FaceHandle, closest point, distance}`, and emits a SpatialDebug overlay highlighting that face and reporting the distance, while the geometry layer continues to own the query itself.

## Non-goals
- No geometry kernel implementation; GEOM-039 owns the accelerated nearest-face query, the per-face AABB index build, and the exact branch-and-bound traversal.
- No GPU picking, GPU readback, render-target ID buffers, or shader work; this is a CPU-resolved overlay path.
- No editor-window UI; this is the SpatialDebug overlay path and is distinct from the UI-024/025/026 geometry-method editor windows.
- No new persistent or generated asset, and no new serialized scene state.
- No async/streaming index build unless a later value-gated runtime task accepts it.
- No `Runtime.Engine.cppm` public API expansion unless the existing SpatialDebug composition seams cannot express the workflow.
- No new geometric primitive types and no changes to the existing point kNN query surface.

## Context
- Status: backlog.
- Owning subsystem/layer: `src/runtime/SpatialDebug/*` plus the runtime composition root that owns mesh `GeometrySources` entities. Runtime composes the geometry query; geometry owns the query. Runtime depends on geometry; geometry never depends on runtime.
- SpatialDebug already exists under `src/runtime/SpatialDebug` as `Extrinsic.Runtime.SpatialDebugAdapters` (`src/runtime/SpatialDebug/Runtime.SpatialDebugAdapters.cppm` / `.cpp`). It already exposes adapter wrappers over `Geometry::BVH` / `Geometry::KDTree` / `Geometry::Octree`, a `SpatialDebugSnapshotBatch` data-only batch, `SpatialDebugAdapterOptions`/`SpatialDebugAdapterStats`, and a `SpatialDebugAdapterRegistry` keyed by an opaque `std::uint64_t`. This task adds an exact-nearest-face consumer alongside those adapters, not in place of them.
- GEOM-039 exports the packaged nearest-face query in `src/geometry/Geometry.SpatialQueries.cppm` / `Geometry.BVH.cppm` returning a result type (e.g. `MeshClosestFaceResult`) carrying the closest `FaceHandle`, the closest `glm::vec3` point, the exact squared distance, and a found/valid flag, built over per-face AABBs. The mesh source is a `Geometry::HalfedgeMesh::Mesh` (`FaceHandle` declared in `src/geometry/Geometry.HalfedgeMesh.cppm`).
- The active mesh entity is composed from ECS `GeometrySources` (`src/ecs/Components/ECS.Component.GeometrySources.cppm`); the runtime composition root resolves the active mesh, supplies its `HalfedgeMesh::Mesh` to the geometry query, and stamps overlay state.
- Because GEOM-039 is a dependency, the runtime consumer must be feature-gated (compile-time/no-op) until the query lands, and must fail closed with deterministic diagnostics when no mesh entity is active, when the mesh is empty/non-triangle, or when the cursor point is non-finite.
- Overlay rendering follows the existing SpatialDebug data-only batch convention: the consumer publishes overlay state (a highlighted `FaceHandle`, its closest point, and a distance) into a data-only structure; it must not call renderer/RHI/Vulkan upload APIs directly.

## Slice plan
- [ ] Slice A (CPUContracted composition): add the runtime closest-face SpatialDebug consumer (index build/refresh over the active mesh entity, point-driven resolution via the GEOM-039 query, data-only overlay state) behind a GEOM-039 feature gate, with a runtime contract test proving parity with the direct geometry query and a valid overlay reference. This is the maturity stop-state.
- [ ] Slice B (Operational, deferred): live interactive cursor/ray-driven picking wired into the editor input loop with on-screen highlight rendering. Deferred to a later value-gated follow-up; not in scope here.

## Required changes
- [ ] Add a runtime closest-face SpatialDebug consumer to `src/runtime/SpatialDebug/` (new `Runtime.SpatialDebugClosestFace.cppm` interface plus matching `Runtime.SpatialDebugClosestFace.cpp` implementation; non-trivial bodies in the `.cpp`). Export the consumer in the `Extrinsic::Runtime` namespace alongside the existing adapter surface.
- [ ] Define a data-only overlay state struct (e.g. `SpatialDebugClosestFaceOverlay`) carrying a found/valid flag, the highlighted `Geometry::HalfedgeMesh::FaceHandle`, the closest `glm::vec3` point, the cursor/probe world point, and the resolved distance; plus a result/diagnostic enum reported on failure (no active mesh, empty/non-triangle mesh, non-finite probe, GEOM-039 unavailable).
- [ ] Add an index lifecycle on the consumer: build/refresh the per-face nearest-face index for the active mesh entity by delegating to the GEOM-039 query/index build, and invalidate/rebuild it when the active mesh entity or its mesh content changes (track via a mesh revision/dirty signal already available on `GeometrySources`, rather than rebuilding every frame).
- [ ] Add the resolve entry point: given the active mesh and a world-space probe point, call the GEOM-039 nearest-face query and populate the overlay state with the returned `{FaceHandle, closest point, exact distance}`; populate the failure diagnostic and clear the highlight on any fail-closed path.
- [ ] Wire the consumer into the runtime composition that owns mesh `GeometrySources` (the SpatialDebug composition under `src/runtime/`), resolving the active mesh entity's `HalfedgeMesh::Mesh` and feeding it to the consumer; do not reach into geometry to access runtime/ECS state.
- [ ] Feature-gate the GEOM-039 call site so the consumer compiles and returns a deterministic "query unavailable" diagnostic (no highlight) until GEOM-039 is present; remove the gate when the dependency lands.
- [ ] Update module wiring (`intrinsic_add_module_library` / `target_sources(... FILE_SET CXX_MODULES ...)`) in the runtime SpatialDebug `CMakeLists.txt` for the new translation unit; do not introduce a new module library if the existing SpatialDebug module library can own the unit.

## Tests
- [ ] Add a runtime contract test (e.g. `tests/contract/runtime/Test.SpatialDebugClosestFace.cpp`, labeled `contract;runtime`) that, given a fixed triangle mesh and a probe point, asserts the SpatialDebug consumer returns the same nearest `FaceHandle` and closest point as a direct GEOM-039 query call. Register it on the existing `IntrinsicRuntimeContractTests` target in `tests/CMakeLists.txt` (which is declared with `SEARCH_DIRS contract/runtime` and `LABELS contract runtime`), so it inherits the `contract`/`runtime` labels and is selected by contract-test and touched-scope runs — do not give it a `unit` label or introduce a new CTest label.
- [ ] Assert that after a successful resolve, the overlay state references a valid (in-mesh, non-deleted) `FaceHandle`, a finite closest point, and a non-negative distance consistent with the direct query.
- [ ] Assert index rebuild on mesh change: after mutating the active mesh entity's mesh content (or swapping the active mesh entity), a subsequent resolve reflects the new mesh and returns the new nearest face (stale-index result is not returned).
- [ ] Assert no-active-mesh behavior: with no active mesh entity, resolve returns a fail-closed diagnostic, leaves the overlay highlight cleared, and does not crash or assert.
- [ ] Assert degenerate fail-closed: empty mesh, non-triangle mesh, and a non-finite probe point each return an explicit diagnostic with no highlight and no NaNs.
- [ ] Assert the GEOM-039-unavailable gate path returns the deterministic "query unavailable" diagnostic with no highlight.

## Docs
- [ ] Update `src/runtime/README.md` (or the SpatialDebug section therein) describing the closest-face overlay consumer, the runtime-composes / geometry-owns split, the index invalidation contract, and the data-only overlay (no direct renderer/RHI calls).
- [ ] Regenerate `docs/api/generated/module_inventory.md` to record the new runtime SpatialDebug module surface.
- [ ] Update [`tasks/backlog/runtime/README.md`](README.md) and this task if scope changes before promotion.

## Acceptance criteria
- [ ] The SpatialDebug closest-face consumer resolves a world-space probe point to the same nearest `FaceHandle` and closest point as the direct GEOM-039 query on a fixed corpus.
- [ ] On success the overlay state references a valid face, a finite closest point, and a reported distance; the consumer publishes only data-only overlay state and makes no renderer/RHI/Vulkan calls.
- [ ] The index is built/refreshed for the active mesh entity and is invalidated/rebuilt on mesh or active-entity change; a stale-index result is never returned.
- [ ] All fail-closed paths (no active mesh, empty/non-triangle mesh, non-finite probe, GEOM-039 unavailable) return deterministic diagnostics with no highlight, no NaNs, and no asserts.
- [ ] No geometry kernel for closest-face is implemented in runtime; the consumer only composes the GEOM-039 query.
- [ ] Focused runtime contract tests and the structural/layering/doc checks below pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SpatialDebug.*ClosestFace|Runtime\.SpatialDebug' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Implementing the closest-face geometry kernel, the per-face AABB index, or the branch-and-bound traversal in runtime/UI instead of consuming GEOM-039; this task only composes the geometry-owned query.
- Introducing any geometry -> runtime/ECS/assets/graphics/rhi/app dependency, or otherwise reaching from `src/geometry/*` into runtime to satisfy this workflow.
- Adding GPU picking, renderer/RHI/Vulkan upload calls, or shader work to the SpatialDebug overlay path.
- Adding an editor-window UI here (the UI-024/025/026 method-window path) instead of the SpatialDebug overlay path.
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Claiming performance improvements without a baseline comparison.
- Introducing new CTest labels without updating `tests/README.md` and `tests/CMakeLists.txt` in the same change.

## Maturity
- Target: `CPUContracted`. The stop-state is Slice A: a CPU/null-safe runtime SpatialDebug closest-face consumer that composes the GEOM-039 query, maintains an invalidatable per-mesh index, and emits a data-only overlay, fully covered by the runtime contract test (parity vs direct query, valid overlay reference, index rebuild, no-active-mesh and degenerate fail-closed).
- `Operational` interactive cursor/ray-driven picking with live on-screen highlighting (Slice B) is a deferred follow-up and is not owed by this task.

- Closure: no `Operational` follow-up is owed for this task.
