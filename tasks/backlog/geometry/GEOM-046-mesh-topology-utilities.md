---
id: GEOM-046
theme: none
depends_on: []
maturity_target: CPUContracted
---

# GEOM-046 — Mesh topology utilities — components, dual, triangulate, adjacency

## Goal

- [ ] Add the half-edge mesh topology utilities the engine currently lacks: connected-component labeling plus split/keep-largest, a general `Mesh -> Mesh` dual operator, core mesh ops (polygon `Triangulate`, `IsRemovalOk`, and an in-circle / Delaunay edge-flip test), a triangle-adjacency index builder, a mesh nearest-face convenience that delegates to the GEOM-039 accelerated query, and a cached bulk `e:length` edge-length property.
- [ ] Land the work as three reviewable slices (A: components, B: dual, C: core ops + adjacency + edge length), each deterministic and fail-closed, closing at `CPUContracted`.

## Non-goals

- No remeshing or simplification behavior changes (`Geometry.HalfedgeMesh.Remeshing`, `Geometry.HalfedgeMesh.AdaptiveRemeshing`, `Geometry.HalfedgeMesh.Simplification` are untouched).
- No GPU backend, no Vulkan/RHI work, no shaders.
- No UI, editor, or visualization work.
- The nearest-face spatial acceleration structure itself is owned by GEOM-039; this task only adds a thin mesh-side convenience that delegates to it.
- No new public maturity beyond `CPUContracted` (single-threaded CPU reference correctness only; no performance claims).

## Context

- The half-edge mesh core lives in `src/geometry/Geometry.HalfedgeMesh.cppm` / `.cpp` (`Geometry::HalfedgeMesh::Mesh`), which already exposes `Split`, `Collapse`, `Flip`, `IsCollapseOk`, `IsFlipOk`, and a PMP-style property system (`VertexProperty`/`EdgeProperty`/`FaceProperty`, `VertexProperties()`/`EdgeProperties()`/`FaceProperties()`/`HalfedgeProperties()`).
- Connected components are only *counted* today: `Geometry::MeshRepair::OrientationResult::ComponentCount` in `src/geometry/Geometry.HalfedgeMesh.Repair.cppm`. There is no labeling, split, or keep-largest API.
- The mesh dual is currently inlined only inside Platonic-solid builders in `src/geometry/Geometry.HalfedgeMesh.Builder.cppm` (e.g. `MakeMeshOctahedron`); there is no reusable `Mesh -> Mesh` dual operator.
- Mesh convenience utilities live in `src/geometry/Geometry.HalfedgeMesh.Utils.cppm` (`Geometry::MeshUtils`), which already has `TriangleFaceView`, `TriangleArea`, and triangle-soup conversion helpers but no triangle-adjacency builder and no nearest-face convenience.
- The accelerated nearest-face query is owned by GEOM-039 (mesh closest-point/BVH query surface under `Geometry::` spatial queries, e.g. `Geometry.SpatialQueries` / `Geometry.Queries`); this task delegates rather than re-implements.
- These are standard half-edge utilities that are partially missing or inlined; adding them unblocks mesh cleanup/segmentation and CDT-style (constrained-Delaunay) workflows.
- Ported reference behaviors mirror `bcg_mesh_connected_components`, `bcg_mesh_dual`, `bcg_mesh`, `bcg_mesh_utils`, and the mesh side of `bcg_graph_edge_length`.
- Layering: this is geometry-only work. `src/geometry/*` may depend on `core` only and must not import assets/runtime/graphics/rhi/ecs/app. All new code follows GEOM-005 (API/numeric policy) and GEOM-007 (robust-predicate/tolerance policy).

## Slice plan

- [ ] Slice A — Connected-component labeling: add per-vertex and per-face component-id properties, `SplitIntoComponents` (split-into-N-meshes), and `KeepLargestComponent`, in `Geometry.HalfedgeMesh.Repair`. Closes at `CPUContracted`.
- [ ] Slice B — General mesh dual: add a reusable `Mesh -> Mesh` dual operator (faces -> vertices, vertices -> faces) in `Geometry.HalfedgeMesh.Utils`, and refactor the Platonic-solid builders to consume it without changing their outputs. Closes at `CPUContracted`.
- [ ] Slice C — Core ops + adjacency + edge length: add `Triangulate(face)`, `IsRemovalOk(vertex)`, and `DelaunayFlip`/in-circle test to `Geometry.HalfedgeMesh`; add triangle-adjacency index construction and the nearest-face convenience to `Geometry.HalfedgeMesh.Utils`; add cached `e:length` to the mesh core. Closes at `CPUContracted`.

## Required changes

- [ ] `src/geometry/Geometry.HalfedgeMesh.Repair.cppm` — declare component-labeling API in `Geometry::MeshRepair`: `ComputeConnectedComponents(const Mesh&)` returning per-vertex and per-face component-id labels plus the component count; `SplitIntoComponents(const Mesh&)` returning `std::vector<Mesh>` (one mesh per component); `KeepLargestComponent(Mesh&)` (largest by face count, deterministic tie-break by lowest first-face index). Use `std::optional`/explicit diagnostics for failure; no asserts.
- [ ] `src/geometry/Geometry.HalfedgeMesh.Repair.cpp` — implement labeling via deterministic flood-fill over half-edge adjacency; write component ids into `v:component` (`VertexProperty<int>`) and `f:component` (`FaceProperty<int>`); fail closed on empty/degenerate input (no faces, or non-finite positions) with an explicit diagnostic and no partial mutation.
- [ ] `src/geometry/Geometry.HalfedgeMesh.Utils.cppm` — declare in `Geometry::MeshUtils`: `Dual(const Mesh&)` returning `std::optional<Mesh>` (faces -> vertices using face centroids, vertices -> faces); `BuildTriangleAdjacencyIndices(const Mesh&)` returning the GL_TRIANGLES_ADJACENCY index buffer (6 indices per triangle); `NearestFace(const Mesh&, glm::vec3)` convenience delegating to the GEOM-039 accelerated query.
- [ ] `src/geometry/Geometry.HalfedgeMesh.Utils.cpp` — implement `Dual` (fail closed on non-manifold/boundary/empty input where the dual is undefined), `BuildTriangleAdjacencyIndices` (boundary neighbor encoding documented and deterministic; reuse existing `TriangleFaceView`/`TryGetTriangleFaceView`), and `NearestFace` delegating to GEOM-039 (no re-implementation of acceleration).
- [ ] `src/geometry/Geometry.HalfedgeMesh.Builder.cpp` — refactor the inlined dual logic in the Platonic-solid builders (e.g. `MakeMeshOctahedron`) to call `MeshUtils::Dual`, with byte-for-byte identical output; mechanical-only, no behavior change.
- [ ] `src/geometry/Geometry.HalfedgeMesh.cppm` — declare new core mesh ops on `Geometry::HalfedgeMesh::Mesh`: `Triangulate(FaceHandle)` (fan/ear triangulation of a simple polygon face), `IsRemovalOk(VertexHandle) const` (vertex-removal validity test), and `DelaunayFlip(EdgeHandle)` plus a const in-circle/Delaunay predicate `IsDelaunay(EdgeHandle) const`; declare a cached `EdgeLength(EdgeHandle) const` accessor backed by `e:length`.
- [ ] `src/geometry/Geometry.HalfedgeMesh.cpp` — implement `Triangulate`, `IsRemovalOk`, the in-circle predicate + `DelaunayFlip` (built on existing `Flip`/`IsFlipOk`), and `e:length` caching (`EdgeProperty<double>` named `e:length`, computed lazily/bulk and validated against direct edge-vector length). All predicates use the GEOM-007 robust tolerance policy and fail closed on degenerate (zero-area, collinear, non-finite) input.
- [ ] `tests/CMakeLists.txt` — register the new test source(s) under the existing `unit;geometry` labeled target (do NOT introduce a new CTest label).

## Tests

- [ ] Component labeling: a mesh containing two disjoint spheres yields exactly 2 components; `ComputeConnectedComponents` assigns consistent per-vertex and per-face ids; `SplitIntoComponents` produces 2 watertight (closed, Euler-consistent) meshes.
- [ ] `KeepLargestComponent` on a two-component mesh (large + small) retains the larger component and drops the smaller; vertex/face counts match the larger component exactly.
- [ ] Dual: the dual of a cube is combinatorially an octahedron (8 faces -> 6 vertices, 6 faces, 12 edges); `Dual(Dual(M))` is combinatorially equal to `M` for a clean closed manifold mesh.
- [ ] `Triangulate` on a convex polygon face produces a valid fan/ear triangulation (n-2 triangles), all triangles consistently oriented, and total area preserved within GEOM-007 tolerance.
- [ ] Delaunay: `IsDelaunay` returns false for a non-Delaunay interior edge and `DelaunayFlip` flips it to the Delaunay configuration; `IsDelaunay` returns true and `DelaunayFlip` is a no-op for an already-Delaunay edge.
- [ ] `BuildTriangleAdjacencyIndices`: emits 6 indices per triangle; interior adjacency indices reference the correct opposite vertex of the neighbor across each edge; boundary edges use the documented boundary encoding consistently.
- [ ] Cached `e:length` (`EdgeLength`) matches the direct `length(p1 - p0)` computation for every edge within GEOM-007 tolerance.
- [ ] `NearestFace` returns the same face as the GEOM-039 reference query for a set of probe points (delegation parity, not a re-implementation).
- [ ] Degenerate inputs fail closed: empty mesh, mesh with no faces, non-finite positions, and (for `Dual`) non-manifold/boundary input return an explicit empty/diagnostic result and never NaN, assert, or partially mutate.

## Docs

- [ ] Update the module inventory via `tools/repo/generate_module_inventory.py` so the new exported symbols in `Geometry.HalfedgeMesh`, `Geometry.HalfedgeMesh.Repair`, and `Geometry.HalfedgeMesh.Utils` appear in `docs/api/generated/module_inventory.md`.
- [ ] Document the new utilities (component split/keep-largest, dual, triangulate, Delaunay-flip, triangle adjacency, cached `e:length`, nearest-face delegation) in the relevant `docs/architecture/*` half-edge mesh reference, including the boundary-encoding convention for triangle adjacency and the GEOM-039 delegation note for `NearestFace`.

## Acceptance criteria

- [ ] All three slices compile under the `ci` preset and the full set of new tests passes under `-LE 'gpu|vulkan|slow|flaky-quarantine'`.
- [ ] `ComputeConnectedComponents`, `SplitIntoComponents`, `KeepLargestComponent`, `MeshUtils::Dual`, `BuildTriangleAdjacencyIndices`, `MeshUtils::NearestFace`, `Mesh::Triangulate`, `Mesh::IsRemovalOk`, `Mesh::IsDelaunay`, `Mesh::DelaunayFlip`, and `Mesh::EdgeLength` are all exported and exercised by tests.
- [ ] Platonic-solid builders produce byte-for-byte identical meshes before and after the `Dual` refactor.
- [ ] Every new algorithm is deterministic (identical input -> identical output across runs) and fails closed on empty/degenerate/non-finite input with an explicit diagnostic, no asserts, and no NaNs (GEOM-005, GEOM-007).
- [ ] `check_layering.py --strict` reports no new dependencies out of geometry (no assets/runtime/graphics/rhi/ecs/app imports).
- [ ] `check_task_policy.py --strict` and `check_doc_links.py` pass; the module inventory is regenerated and committed.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'HalfedgeMesh.*(Component|Dual|Triangulate|Delaunay|Adjacency|EdgeLength|NearestFace)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Mixing the mechanical Platonic-builder `Dual` refactor with any semantic change to builder output in the same commit.
- Introducing unrelated feature work (remeshing, simplification, parameterization, or new mesh formats).
- Introducing renderer/runtime/ECS/assets/platform/app dependencies into `src/geometry/*`.
- Re-implementing or modifying the GEOM-039 nearest-face acceleration structure; `NearestFace` must only delegate.
- Introducing new CTest labels without updating `tests/README.md` and `tests/CMakeLists.txt` in the same change.
- Claiming any performance improvement without a recorded baseline comparison.

## Maturity

- Stop-state for this task is `CPUContracted`: single-threaded CPU reference implementations with deterministic, fail-closed contracts and full correctness tests. No parity backend, no performance optimization, and no GPU work is in scope; do not advance the maturity pin beyond `CPUContracted` in this task.

- Closure: no `Operational` follow-up is owed for this task.
