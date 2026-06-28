---
id: RUNTIME-135
theme: F
depends_on: [GEOM-039]
maturity_target: CPUContracted
completed_on: 2026-06-28
---
# RUNTIME-135 — SpatialDebug closest-face picking via accelerated mesh query

## Goal
- Wire the GEOM-039 accelerated mesh closest-face query into the runtime SpatialDebug path so a world-space cursor point (or pick-ray hit point) resolves and highlights the nearest mesh face of the active mesh entity for inspection/picking.
- Compose, in the runtime layer, an exact-nearest-face consumer that builds/refreshes the per-face spatial index for the active mesh entity, resolves `{FaceHandle, closest point, distance}`, and emits a SpatialDebug overlay highlighting that face and reporting the distance, while the geometry layer continues to own the query itself.

## Non-goals
- No geometry kernel implementation; GEOM-039 owns the accelerated nearest-face query, the per-face AABB index build, and the exact branch-and-bound traversal.
- No GPU picking, GPU readback, render-target ID buffers, or shader work; this is a CPU-resolved overlay path.
- No editor-window UI; this is the SpatialDebug overlay path and is distinct from the retired UI-024 denoise window, the retired UI-025 geometry-method editor windows, and the retired UI-026 curvature window.
- No new persistent or generated asset, and no new serialized scene state.
- No async/streaming index build unless a later value-gated runtime task accepts it.
- No `Runtime.Engine.cppm` public API expansion unless the existing SpatialDebug composition seams cannot express the workflow.
- No new geometric primitive types and no changes to the existing point kNN query surface.

## Context
- Status: completed 2026-06-28 by Codex. Commit: this commit (`Add runtime SpatialDebug closest-face overlay`).
- Owning subsystem/layer: `src/runtime/SpatialDebug/*` plus the runtime composition root that owns mesh `GeometrySources` entities. Runtime composes the geometry query; geometry owns the query. Runtime depends on geometry; geometry never depends on runtime.
- SpatialDebug already exists under `src/runtime/SpatialDebug` as `Extrinsic.Runtime.SpatialDebugAdapters` (`src/runtime/SpatialDebug/Runtime.SpatialDebugAdapters.cppm` / `.cpp`). It already exposes adapter wrappers over `Geometry::BVH` / `Geometry::KDTree` / `Geometry::Octree`, a `SpatialDebugSnapshotBatch` data-only batch, `SpatialDebugAdapterOptions`/`SpatialDebugAdapterStats`, and a `SpatialDebugAdapterRegistry` keyed by an opaque `std::uint64_t`. This task adds an exact-nearest-face consumer alongside those adapters, not in place of them.
- GEOM-039 exports the packaged nearest-face query in `src/geometry/Geometry.MeshClosestFace.cppm` / `.cpp` returning `MeshClosestFaceResult` with the closest `FaceHandle`, closest `glm::vec3` point, normal, primitive index, exact squared distance, status, and diagnostics, built over per-face AABBs. The mesh source is a `Geometry::HalfedgeMesh::Mesh` (`FaceHandle` declared in `src/geometry/Geometry.HalfedgeMesh.cppm`).
- The active mesh entity is composed from ECS `GeometrySources` (`src/ecs/Components/ECS.Component.GeometrySources.cppm`); the runtime composition root resolves the active mesh, supplies its `HalfedgeMesh::Mesh` to the geometry query, and stamps overlay state.
- GEOM-039 is retired and exports the query in `Geometry.MeshClosestFace`; the runtime consumer must fail closed with deterministic diagnostics when no mesh entity is active, when the mesh has no usable finite non-degenerate face triangles, or when the cursor point is non-finite.
- Overlay rendering follows the existing SpatialDebug data-only batch convention: the consumer publishes overlay state (a highlighted `FaceHandle`, its closest point, and a distance) into a data-only structure; it must not call renderer/RHI/Vulkan upload APIs directly.
- Slice A stopped at the CPU-contracted runtime seam: callers resolve the active mesh into a `SpatialDebugClosestFaceMeshSource` carrying a non-owning `HalfedgeMesh::Mesh*`, stable mesh key, mesh revision, and active flag. This avoided `Runtime.Engine.cppm` public API growth and left live cursor/ray/editor input wiring out of scope.

## Slice plan
- [x] Slice A (CPUContracted composition): add the runtime closest-face SpatialDebug consumer (index build/refresh over the active mesh descriptor, point-driven resolution via the GEOM-039 query, data-only overlay state), with a runtime contract test proving parity with the direct geometry query and a valid overlay reference. This is the maturity stop-state.
- Slice B (Operational, not opened): live interactive cursor/ray-driven picking wired into the editor input loop with on-screen highlight rendering was not in scope for this CPU-contracted slice. No follow-up is owed by this task unless a future value-gated task opens that workflow.

## Required changes
- [x] Add a runtime closest-face SpatialDebug consumer to `src/runtime/SpatialDebug/` (new `Runtime.SpatialDebugClosestFace.cppm` interface plus matching `Runtime.SpatialDebugClosestFace.cpp` implementation; non-trivial bodies in the `.cpp`). Export the consumer in the `Extrinsic::Runtime` namespace alongside the existing adapter surface.
- [x] Define a data-only overlay state struct (e.g. `SpatialDebugClosestFaceOverlay`) carrying a found/valid flag, the highlighted `Geometry::HalfedgeMesh::FaceHandle`, the closest `glm::vec3` point, the cursor/probe world point, and the resolved distance; plus a result/diagnostic enum reported on failure (no active mesh, empty mesh/no usable face triangles, non-finite probe).
- [x] Add an index lifecycle on the consumer: build/refresh the per-face nearest-face index for the active mesh descriptor by delegating to the GEOM-039 query/index build, and invalidate/rebuild it when the active mesh key or revision changes, rather than rebuilding every frame.
- [x] Add the resolve entry point: given the active mesh and a world-space probe point, call the GEOM-039 nearest-face query and populate the overlay state with the returned `{FaceHandle, closest point, exact distance}`; populate the failure diagnostic and clear the highlight on any fail-closed path.
- [x] Expose the runtime composition descriptor that callers use after resolving the active mesh entity's `HalfedgeMesh::Mesh`; the consumer itself stays ECS-free and does not reach back into geometry/runtime ownership state.
- [x] Compose the geometry-owned GEOM-039 call site directly; do not reimplement closest-face traversal or keep a stale query-unavailable lane now that the dependency is retired.
- [x] Update module wiring (`intrinsic_add_module_library` / `target_sources(... FILE_SET CXX_MODULES ...)`) in the runtime SpatialDebug `CMakeLists.txt` for the new translation unit; do not introduce a new module library if the existing SpatialDebug module library can own the unit.

## Tests
- [x] Add a runtime contract test (e.g. `tests/contract/runtime/Test.SpatialDebugClosestFace.cpp`, labeled `contract;runtime`) that, given a fixed triangle mesh and a probe point, asserts the SpatialDebug consumer returns the same nearest `FaceHandle` and closest point as a direct GEOM-039 query call. Register it on the existing `IntrinsicRuntimeContractTests` target in `tests/CMakeLists.txt` (which is declared with `SEARCH_DIRS contract/runtime` and `LABELS contract runtime`), so it inherits the `contract`/`runtime` labels and is selected by contract-test and touched-scope runs — do not give it a `unit` label or introduce a new CTest label.
- [x] Assert that after a successful resolve, the overlay state references a valid (in-mesh, non-deleted) `FaceHandle`, a finite closest point, and a non-negative distance consistent with the direct query.
- [x] Assert index rebuild on mesh change: after mutating the active mesh entity's mesh content (or swapping the active mesh entity), a subsequent resolve reflects the new mesh and returns the new nearest face (stale-index result is not returned).
- [x] Assert no-active-mesh behavior: with no active mesh entity, resolve returns a fail-closed diagnostic, leaves the overlay highlight cleared, and does not crash or assert.
- [x] Assert degenerate fail-closed: empty mesh, mesh with no usable finite non-degenerate face triangle, and a non-finite probe point each return an explicit diagnostic with no highlight and no NaNs.

## Docs
- [x] Update `src/runtime/README.md` (or the SpatialDebug section therein) describing the closest-face overlay consumer, the runtime-composes / geometry-owns split, the index invalidation contract, and the data-only overlay (no direct renderer/RHI calls).
- [x] Regenerate `docs/api/generated/module_inventory.md` to record the new runtime SpatialDebug module surface.
- [x] Update [`tasks/backlog/runtime/README.md`](../backlog/runtime/README.md) and this task if scope changes before promotion.

## Acceptance criteria
- [x] The SpatialDebug closest-face consumer resolves a world-space probe point to the same nearest `FaceHandle` and closest point as the direct GEOM-039 query on a fixed corpus.
- [x] On success the overlay state references a valid face, a finite closest point, and a reported distance; the consumer publishes only data-only overlay state and makes no renderer/RHI/Vulkan calls.
- [x] The index is built/refreshed for the active mesh descriptor and is invalidated/rebuilt on mesh key/revision change; a stale-index result is never returned.
- [x] All fail-closed paths (no active mesh, empty mesh/no usable face triangles, non-finite probe) return deterministic diagnostics with no highlight, no NaNs, and no asserts.
- [x] No geometry kernel for closest-face is implemented in runtime; the consumer only composes the GEOM-039 query.
- [x] Focused runtime contract tests and the structural/layering/doc checks below pass.

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
- Adding an editor-window UI here (the retired UI-025 method-window path, plus retired UI-024/UI-026 method-window paths) instead of the SpatialDebug overlay path.
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Claiming performance improvements without a baseline comparison.
- Introducing new CTest labels without updating `tests/README.md` and `tests/CMakeLists.txt` in the same change.

## Maturity
- Target: `CPUContracted`. The stop-state is Slice A: a CPU/null-safe runtime SpatialDebug closest-face consumer that composes the GEOM-039 query, maintains an invalidatable per-mesh index, and emits a data-only overlay, fully covered by the runtime contract test (parity vs direct query, valid overlay reference, index rebuild, no-active-mesh and degenerate fail-closed).
- `Operational` interactive cursor/ray-driven picking with live on-screen highlighting was not opened and is not owed by this task.

- Closure: no `Operational` follow-up is owed for this task.
