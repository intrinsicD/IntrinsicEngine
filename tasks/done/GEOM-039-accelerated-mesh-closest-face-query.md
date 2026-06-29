---
id: GEOM-039
theme: none
depends_on: []
maturity_target: CPUContracted
completed_on: 2026-06-28
---

# GEOM-039 — Accelerated mesh closest-face query and consumer adoption

## Goal

- [x] Provide a packaged, accelerated nearest-face spatial query over a triangle mesh that returns the closest `FaceHandle` together with the closest point on that face, using the exact point-to-triangle distance.
- [x] Drive the query with an existing acceleration structure (per-face AABBs indexed by `Geometry.BVH` / `Geometry.KDTree` / `Geometry.Octree`) and branch-and-bound pruning keyed on the *exact* triangle distance, not the AABB-centroid distance that current `Geometry.SpatialQueries` consumers approximate with.
- [x] Replace the three existing brute-force `ForEachFace`-style linear nearest-face scans in the geometry consumers with the new query, proving identical results to the prior brute-force path.

## Non-goals

- No GPU acceleration of the query; this is a CPU-only spatial query.
- No changes to the existing general kNN-over-points query surface; point queries already exist and are out of scope.
- No new geometric primitive types (no new triangle/AABB/mesh representations).
- No renderer/runtime/ECS/assets/platform integration; this is geometry-internal.
- No performance claims beyond functional parity; speedup measurement is deferred to a later benchmark task.

## Context

- Status: completed 2026-06-28 by Codex. Commit: this commit (`Complete accelerated mesh closest-face adoption`).
- All ingredients already exist: exact distance via `ClosestPoint(const Triangle&, const glm::vec3&)` in `src/geometry/Geometry.Triangle.cppm`, and three acceleration structures (`Geometry.BVH`, `Geometry.KDTree`, `Geometry.Octree`) sharing the `SpatialQueryShape` concept and diagnostics structs (`SpatialBuildResult`, `SpatialKNNResult`, `SpatialRadiusResult`) declared in `src/geometry/Geometry.SpatialQueries.cppm`.
- `Geometry.MeshClosestFace` is now the single packaged *exact* nearest-face entry point. It replaced the prior private nearest-face scans in `Geometry.HalfedgeMesh.Simplification` (the Hausdorff redistribution path around `Geometry.HalfedgeMesh.Simplification.cpp:~1230`), the `MeshClosestPointOracle` in `Geometry.ImplicitPlaneField`, and the `ReferenceProjector` in `Geometry.HalfedgeMesh.AdaptiveRemeshing`.
- This was the highest-leverage single port: one new query removed three independent brute-force scans.
- Layering: this is geometry-only (geometry -> core). The query must not import assets/runtime/graphics/rhi/ecs/app, and must follow GEOM-005 (API/numeric policy) and GEOM-007 (robust-predicate/tolerance policy): deterministic, fail-closed on degenerate/empty/non-finite input with explicit diagnostics, no asserts, no NaNs.

## Slice plan

- [x] Slice A (CPUContracted query): add the nearest-face query API, the per-face AABB index build, exact branch-and-bound pruning, and optional k-nearest-faces / radius variants. Add CPU correctness tests (parity vs brute-force, pruning correctness, degenerate fail-closed). Defers all consumer adoption.
- [x] Slice B (consumer adoption): adopt the query in `Geometry.HalfedgeMesh.Simplification`, `Geometry.ImplicitPlaneField` (`MeshClosestPointOracle`), and `Geometry.HalfedgeMesh.AdaptiveRemeshing` (`ReferenceProjector`); add parity tests asserting each consumer produces results identical to its prior brute-force path.

## Required changes

- [x] In `src/geometry/Geometry.MeshClosestFace.cppm`, export a mesh-nearest-face result type (`MeshClosestFaceResult` carrying the closest `FaceHandle`, closest `glm::vec3` point, normal, primitive index, exact squared distance, status, and diagnostics) plus reuse of the existing diagnostics structs for visited-node / distance-evaluation counts.
- [x] Add the nearest-face query entry point in `Geometry.MeshClosestFace`, with the non-trivial branch-and-bound body in the matching `.cpp` implementation unit.
- [x] Build the per-face AABB index from a mesh (`HalfedgeMesh::Mesh` faces), associating each leaf AABB with its `FaceHandle` and reusing `SpatialKNNResult` / `SpatialRadiusResult` diagnostics.
- [x] Implement exact nearest-face traversal: prune subtrees by squared point-to-AABB lower bound, but rank/accept candidates by exact `ClosestPoint(Triangle, point)` distance from `src/geometry/Geometry.Triangle.cppm`; never accept an AABB-centroid distance as the answer.
- [x] Add optional `k`-nearest-faces and radius (all faces within r) variants returning `FaceHandle`s ordered by exact distance, populating `SpatialKNNResult` / `SpatialRadiusResult` diagnostics.
- [x] Fail closed (GEOM-005 / GEOM-007): empty meshes, faces with no usable finite non-degenerate triangle, and non-finite query points or vertices return an explicit invalid result with a diagnostic and no NaNs; no asserts. Polygon faces are fan-triangulated.
- [x] Slice B — `src/geometry/Geometry.HalfedgeMesh.Simplification.cpp`: replace the linear nearest-face scan (around line ~1230, built on the `ForEachFace` helper) with a call to the new query, preserving the existing tie-break and result semantics.
- [x] Slice B — `src/geometry/Geometry.ImplicitPlaneField.cpp` / `.cppm`: route `MeshClosestPointOracle` through the new nearest-face query instead of its brute-force scan.
- [x] Slice B — `src/geometry/Geometry.HalfedgeMesh.AdaptiveRemeshing.cpp` / `.cppm`: route the `ReferenceProjector` nearest-face lookup through the new query.
- [x] Update the module wiring (`target_sources(... FILE_SET CXX_MODULES ...)` / `intrinsic_add_module_library`) in the geometry `CMakeLists.txt` only if a new translation unit is added; do not introduce new module libraries unnecessarily.

## Tests

- [x] Parity-vs-brute-force: on random triangle meshes and random query points, the query's returned `FaceHandle` and closest point match an in-test brute-force exact scan (with deterministic tie-break) for every sample.
- [x] Pruning correctness: assert the accelerated result equals the exhaustive exact result while visiting strictly fewer than all faces on non-trivial meshes (use the `VisitedNodes` / `DistanceEvaluations` diagnostics to confirm pruning actually occurred).
- [x] k-nearest-faces: returned faces are exactly the k closest by exact distance and are ordered by non-decreasing exact distance.
- [x] Radius variant: returns exactly the set of faces whose exact distance is within r, matching a brute-force filter.
- [x] Degenerate fail-closed: empty mesh, mesh with no usable finite non-degenerate triangle, and non-finite query point each return an explicit invalid result (no NaNs, no asserts, diagnostic set).
- [x] Boundary cases: query point coincident with a vertex and query point lying on an edge return the correct adjacent face and an exact (zero or near-zero) distance consistent with brute force.
- [x] Slice B parity: each adopted consumer (`Simplification`, `ImplicitPlaneField` `MeshClosestPointOracle`, `AdaptiveRemeshing` `ReferenceProjector`) produces results identical to its prior brute-force path on a fixed seed corpus.
- [x] All new tests carry the `unit;geometry` label; no new CTest labels are introduced.

## Docs

- [x] Document the nearest-face query (semantics, exact-distance guarantee, fail-closed behavior, optional kNN/radius variants) in the relevant geometry API doc under `docs/` and reference GEOM-005 / GEOM-007.
- [x] Regenerate the module inventory (`docs/api/generated/module_inventory.md`) so the new exported symbols are recorded.
- [x] Note in the consumer-facing notes that `Simplification`, `ImplicitPlaneField`, and `AdaptiveRemeshing` now share the packaged query instead of brute-force scans.

## Acceptance criteria

- [x] A single exported nearest-face query returns `{FaceHandle, closest point, exact squared distance}` and is built on an existing acceleration structure over per-face AABBs.
- [x] On a fixed random corpus, accelerated results equal exhaustive exact results for nearest, k-nearest, and radius variants, with diagnostics showing strictly fewer than full-mesh visitation on non-trivial meshes.
- [x] All listed degenerate and boundary inputs return explicit invalid/correct results with no NaNs and no asserts.
- [x] All three consumers compile against the new query and their Slice B parity tests pass (identical to prior brute-force output).
- [x] `check_layering.py --strict` passes: no geometry -> assets/runtime/graphics/rhi/ecs/app dependency is introduced.
- [x] The full Verification block below passes locally.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Geometry\.(BVH|SpatialQueries|Triangle|HalfedgeMesh\.Simplification|ImplicitPlaneField|HalfedgeMesh\.AdaptiveRemeshing)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work outside the nearest-face query and its three named consumers.
- Introducing renderer/runtime/ECS/assets/platform/app dependencies into geometry.
- Claiming performance improvements without a baseline comparison (this task asserts parity only).
- Accepting AABB-centroid distance as the nearest-face answer; the returned distance must be the exact point-to-triangle distance.
- Introducing new CTest labels without updating `tests/README.md` and `tests/CMakeLists.txt` in the same change.
- Adding GPU paths, new primitive types, or changes to the existing point kNN surface.

## Maturity

- Stop-state for this task is `CPUContracted`: Slice A landed a contracted, CPU-correct nearest-face query with exact-distance guarantees and fail-closed degenerate handling, fully covered by parity/pruning/degenerate unit tests; Slice B adopts it in the three consumers with parity tests. No GPU/optimized backend or benchmark claim is in scope; reaching `Operational`/`ParityProven` is deferred to a later task.

- Closure: no `Operational` follow-up is owed for this task.
