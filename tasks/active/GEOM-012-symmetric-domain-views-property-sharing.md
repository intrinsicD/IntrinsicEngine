# GEOM-012 — Symmetric mesh, graph, and point-cloud domain views

## Status
- Status: in-progress (Slice A landed)
- Owner: unassigned
- Branch: Slice A on `claude/funny-pascal-kTHxz`
- Promoted from `tasks/backlog/geometry/` on 2026-05-28 as the next unblocked geometry task; the GEOM-008 foundation it depends on (Geometry.Linalg / Geometry.Sparse) is retired and the existing mesh-backed graph helper in `tests/unit/geometry/Test_ShortestPath.cpp` is the seed pattern for the public adaptor.
- Next verification step after Slice A: `ctest --test-dir build/ci --output-on-failure -R 'SubmeshView|ShortestPath|PointCloud|RuntimeGraph|MeshOperations' --timeout 60`.

## Slice plan
- **Slice A — Mesh-backed graph borrow adaptor (this slice, landed).** Adds the new `Geometry.DomainViews` module exposing `Geometry::DomainViews::BorrowMeshAsGraph(HalfedgeMesh::Mesh&)` (mutable borrow) and `BorrowMeshAsGraph(const HalfedgeMesh::Mesh&)` (read-only intent via a mutable-storage borrow wrapper documented as const-callsite). The factory constructs a `Graph::Graph` that shares the source mesh's vertex/halfedge/edge `PropertySet`s and deletion counters; `Graph::EnsureProperties` reuses the mesh's existing `v:point`, `v:connectivity`, `h:connectivity`, `v:deleted`, `e:deleted` slots and does not allocate compatibility-copy properties. Promotes the `MakeMeshBackedGraphView` test helper out of `Test_ShortestPath.cpp` and updates all callers to use the public API. New `Test_SubmeshViewDomainBorrows.cpp` proves shared property identity, live mesh→view edits, and view→mesh edit visibility, plus a fail-fast assertion when the source mesh outlives the view incorrectly is documented but not enforced at runtime (lifetime is the caller's responsibility, mirroring `Mesh::CreateView`). Defers point-cloud direction (Slices B/C), distinct const-view types (Slice D), and hard-copy conversion seams (Slice E).
- **Slice B — Mesh-backed point-cloud borrow adaptor.** Adds `BorrowMeshAsCloud(HalfedgeMesh::Mesh&)` returning a `PointCloud::Cloud` sharing the mesh's vertex `PropertySet` and `DeletedVertexCount`. Reuses canonical `v:point`; does not allocate `p:position`. Tests prove identity of position-property handles, attribute reuse (`v:normal` if present on mesh), and that adding a point through the view appears on the source mesh.
- **Slice C — Graph-backed point-cloud borrow adaptor.** Adds `BorrowGraphAsCloud(Graph::Graph&)` with the symmetric contract; tests prove `v:point` sharing and edge/halfedge data are not exposed through the cloud view.
- **Slice D — Read-only borrow surface.** Introduces `ConstMeshBackedGraphView` / `ConstMeshBackedCloudView` / `ConstGraphBackedCloudView` wrappers whose only accessors are `const`-returning and do not expose `Add*`/`Delete*`/`GetOrAdd*Property`. Compile-time tests assert mutation through the const view is ill-formed. Promotes the mutable-borrow rule from documentation to type.
- **Slice E — Conversion/move/consume policy.** Documents and pins the hard-copy seam (`Geometry::Mesh::Conversion` / `Geometry::PointCloud::Conversion` already cover their pairs) and the explicit ownership-transfer pattern. No new APIs unless review finds a missing pair; closes the task at `CPUContracted`.

## Goal
- Define and implement symmetric, no-copy domain views between `Geometry::HalfedgeMesh::Mesh`, `Geometry::Graph::Graph`, and `Geometry::PointCloud::Cloud` where their required property sets are semantically compatible.

## Non-goals
- No broad replacement of existing `Mesh`, `Graph`, or `Cloud` containers.
- No renderer/runtime/ECS/assets/platform/app integration.
- No forced hard-copy conversion as the default for read-only algorithms.
- No semantic algorithm changes beyond view/adaptor contracts and tests.

## Context
- Owning subsystem/layer: `geometry` (`geometry -> core` only).
- Seeded by [`docs/reviews/2026-05-12-src-geometry-gap-analysis.md`](../../../docs/reviews/2026-05-12-src-geometry-gap-analysis.md) and the follow-up domain ownership review on 2026-05-12.
- Current mesh-backed graph usage can share vertex, halfedge, and edge property sets because `HalfedgeMesh::VertexConnectivity` and `HalfedgeMesh::HalfedgeConnectivity` alias the canonical `Geometry::Graph` connectivity records.
- Point-cloud positions are standardized with mesh/graph positions through the canonical `v:point` property. Remaining domain-view work must preserve this invariant and handle any legacy `p:position` data only through explicit compatibility conversions.
- Future graph, point-cloud, and mesh algorithms should accept the minimal domain they need and should not force data copies when a borrowed view is valid.

## Required changes
- [x] Define a domain-view policy for `Mesh`, `Graph`, and `Cloud`: borrowed view, owning copy, and move/consume semantics. (Slice A documents the mutable-borrow contract in `docs/architecture/geometry.md`; const-view types are owned by Slice D; conversion/move policy is reviewed in Slice E.)
- [x] Add explicit view/adaptor APIs for mesh-backed graph input that preserve the existing shared `v:point`, `v:connectivity`, `h:connectivity`, deletion-count, and edge-property behavior. (Slice A — `Geometry::DomainViews::BorrowMeshAsGraph`.)
- [ ] Add explicit view/adaptor APIs for mesh-backed and graph-backed point-cloud input that map semantic point attributes without duplicating position data. (Slices B + C.)
- [ ] Add const/read-only view types where algorithms should not mutate borrowed storage. (Slice D.)
- [x] Define mutable-borrow rules: algorithms may mutate borrowed storage only when mutation is the documented primary effect; otherwise they must return new owned results or require an explicit copy. (Slice A — recorded in `docs/architecture/geometry.md`; type-level enforcement is Slice D.)
- [ ] Define hard-copy conversion helpers for algorithms that change topology/cardinality or need independent lifetime. (Slice E — `Geometry::Mesh::Conversion` + `Geometry::PointCloud::Conversion` already cover the existing pairs; Slice E reviews coverage and pins missing seams if any.)
- [ ] Define move/consume helpers only for ownership transfer into a new result container, never for temporary algorithm adaptation. (Slice E.)
- [x] Update `src/geometry/CMakeLists.txt`, `Geometry.cppm`, and generated module inventory if new module surfaces are introduced. (Slice A adds `Geometry.DomainViews` and bumps the module inventory.)

## Tests
- [x] Add or update tests proving mesh-backed graph views share property storage and do not create compatibility-copy properties. (Slice A — new `Test_SubmeshViewDomainBorrows.cpp` covers shared-property identity, live edit propagation in both directions, and absence of `v:graph_connectivity`/`h:graph_connectivity` compatibility-copy slots; `Test_ShortestPath.cpp` now calls the public API instead of an in-test helper.)
- [ ] Add tests proving mesh-backed point-cloud views reuse source `v:point` data and do not create independent `p:position` data unless an explicit compatibility conversion requests it. (Slice B.)
- [ ] Add tests for graph-backed point-cloud views where graph vertex positions are reused as point positions. (Slice C.)
- [ ] Add const-view tests proving read-only algorithms cannot mutate borrowed storage through the view type. (Slice D.)
- [ ] Add hard-copy conversion tests proving copied outputs remain valid after the source container is destroyed or mutated. (Slice E.)
- [ ] Add move/consume tests only for explicit ownership-transfer APIs. (Slice E.)

## Docs
- [x] Update `docs/architecture/geometry.md` with symmetric `Mesh` / `Graph` / `PointCloud` domain-view semantics. (Slice A names the `Geometry.DomainViews` module + `BorrowMeshAsGraph` factory; Slices B–E will extend with cloud-direction names and const-view types as those slices land.)
- [ ] Update `docs/reviews/2026-05-12-src-geometry-gap-analysis.md` if this policy materially changes the review conclusions. (Deferred until Slice E closes the policy; Slice A does not change the review's conclusions.)
- [x] Update `docs/api/generated/module_inventory.md` after module surface changes. (Slice A regenerates the inventory.)

## Acceptance criteria
- [ ] Mesh, graph, and point-cloud algorithms can request the minimal domain they need without forcing unnecessary data copies.
- [ ] No-copy borrowed views are explicit and tested for all valid mesh/graph/point-cloud directions.
- [ ] Point-cloud views over mesh/graph data reuse semantic position attributes rather than silently allocating duplicate defaults.
- [ ] Hard-copy and move semantics are explicit API choices, not accidental side effects of adaptation.
- [ ] The implementation preserves `geometry -> core` layering.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure -R 'SubmeshView|ShortestPath|PointCloud|RuntimeGraph|MeshOperations' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not introduce higher-layer dependencies.
- Do not silently copy data in APIs advertised as views.
- Do not let point-cloud adaptation create unrelated default `p:position` storage when the source already has canonical `v:point` positions.
- Do not mix this semantic interoperability work with unrelated module renames.


