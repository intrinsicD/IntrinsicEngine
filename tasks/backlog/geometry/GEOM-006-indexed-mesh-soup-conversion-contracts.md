# GEOM-006 — Indexed mesh/soup container and conversion contracts

## Goal
- Add a lightweight indexed mesh/polygon-soup data model and conversion contracts that bridge import, reconstruction, halfedge topology, point clouds, and renderer upload staging without requiring halfedge connectivity for every algorithm.

## Non-goals
- No renderer or GPU residency implementation.
- No asset-service integration.
- No broad replacement of `Geometry::HalfedgeMesh::Mesh`, `Geometry::PointCloud::Cloud`, or `Geometry::Graph::Graph`.
- No advanced repair/remeshing algorithms beyond validation/conversion needed by this container.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` (`geometry -> core` only).
- Seeded by [`docs/reviews/2026-05-12-src-geometry-gap-analysis.md`](../../../docs/reviews/2026-05-12-src-geometry-gap-analysis.md).
- Many paper methods operate on indexed triangle soups or polygon soups before topology is built, after reconstruction, or during GPU staging.
- Existing IO code can load/export meshes and point clouds, but there is no canonical lightweight soup container with validation and conversion diagnostics.
- This task should align with GEOM-012 domain-view semantics so conversion APIs distinguish borrowed views from owning hard copies.

## Required changes
- [ ] Define an indexed mesh/soup module in `src/geometry` with positions, face/index buffers, optional polygon support, and attribute domains.
- [ ] Provide validation diagnostics for duplicate vertices, invalid indices, degenerate faces, non-manifold edges, inconsistent winding, and attribute arity mismatches.
- [ ] Add conversion from soup to `Geometry::HalfedgeMesh::Mesh` with explicit failure diagnostics for unsupported topology.
- [ ] Add conversion from `Geometry::HalfedgeMesh::Mesh` to indexed triangle/polygon soup while preserving supported vertex/face attributes.
- [ ] Add conversion between point-cloud positions and soup vertices where appropriate.
- [ ] Ensure conversion APIs are named to distinguish no-copy views from hard-copy owning conversions.
- [ ] Document renderer upload staging as a data-shape compatibility goal without importing graphics or runtime layers.
- [ ] Update `src/geometry/CMakeLists.txt`, `Geometry.cppm`, and generated module inventory if a new module surface is added.

## Tests
- [ ] Add `tests/unit/geometry/Test.MeshSoup.cpp` using the `Test.<Name>.cpp` naming style.
- [ ] Cover empty input, valid triangle soup, polygon soup, duplicate vertices, invalid indices, degenerate faces, non-manifold edge detection, winding diagnostics, and attribute size mismatches.
- [ ] Cover round-trip conversions for simple halfedge meshes and point-cloud-derived vertices.
- [ ] Run focused geometry tests.

## Docs
- [ ] Update `docs/architecture/geometry.md` with the soup/container role and conversion boundaries.
- [ ] Update `docs/api/generated/module_inventory.md` after module surface changes.
- [ ] Reference this container from future IO/reconstruction/render-staging tasks where relevant.

## Acceptance criteria
- [ ] Algorithms that do not require halfedge connectivity can use a canonical lightweight geometry container.
- [ ] Conversion failures return structured diagnostics rather than silent `bool`/`std::optional` failures.
- [ ] The implementation preserves `geometry -> core` layering and does not import assets, graphics, runtime, ECS, platform, or app.
- [ ] Focused tests and structural checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure -R 'MeshSoup|GeometryIO|MeshBuilder|HalfedgeMesh' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not add renderer, runtime, ECS, assets, platform, or app dependencies.
- Do not mix this semantic container addition with mechanical module renames.
- Do not claim performance improvements without benchmark evidence.


