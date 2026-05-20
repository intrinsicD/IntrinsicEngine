# GEOM-012 — Symmetric mesh, graph, and point-cloud domain views

## Goal
- Define and implement symmetric, no-copy domain views between `Geometry::HalfedgeMesh::Mesh`, `Geometry::Graph::Graph`, and `Geometry::PointCloud::Cloud` where their required property sets are semantically compatible.

## Non-goals
- No broad replacement of existing `Mesh`, `Graph`, or `Cloud` containers.
- No renderer/runtime/ECS/assets/platform/app integration.
- No forced hard-copy conversion as the default for read-only algorithms.
- No semantic algorithm changes beyond view/adaptor contracts and tests.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` (`geometry -> core` only).
- Seeded by [`docs/reviews/2026-05-12-src-geometry-gap-analysis.md`](../../../docs/reviews/2026-05-12-src-geometry-gap-analysis.md) and the follow-up domain ownership review on 2026-05-12.
- Current mesh-backed graph usage can share vertex, halfedge, and edge property sets because `HalfedgeMesh::VertexConnectivity` and `HalfedgeMesh::HalfedgeConnectivity` alias the canonical `Geometry::Graph` connectivity records.
- Point-cloud positions are standardized with mesh/graph positions through the canonical `v:point` property. Remaining domain-view work must preserve this invariant and handle any legacy `p:position` data only through explicit compatibility conversions.
- Future graph, point-cloud, and mesh algorithms should accept the minimal domain they need and should not force data copies when a borrowed view is valid.

## Required changes
- [ ] Define a domain-view policy for `Mesh`, `Graph`, and `Cloud`: borrowed view, owning copy, and move/consume semantics.
- [ ] Add explicit view/adaptor APIs for mesh-backed graph input that preserve the existing shared `v:point`, `v:connectivity`, `h:connectivity`, deletion-count, and edge-property behavior.
- [ ] Add explicit view/adaptor APIs for mesh-backed and graph-backed point-cloud input that map semantic point attributes without duplicating position data.
- [ ] Add const/read-only view types where algorithms should not mutate borrowed storage.
- [ ] Define mutable-borrow rules: algorithms may mutate borrowed storage only when mutation is the documented primary effect; otherwise they must return new owned results or require an explicit copy.
- [ ] Define hard-copy conversion helpers for algorithms that change topology/cardinality or need independent lifetime.
- [ ] Define move/consume helpers only for ownership transfer into a new result container, never for temporary algorithm adaptation.
- [ ] Update `src/geometry/CMakeLists.txt`, `Geometry.cppm`, and generated module inventory if new module surfaces are introduced.

## Tests
- [ ] Add or update tests proving mesh-backed graph views share property storage and do not create compatibility-copy properties.
- [ ] Add tests proving mesh-backed point-cloud views reuse source `v:point` data and do not create independent `p:position` data unless an explicit compatibility conversion requests it.
- [ ] Add tests for graph-backed point-cloud views where graph vertex positions are reused as point positions.
- [ ] Add const-view tests proving read-only algorithms cannot mutate borrowed storage through the view type.
- [ ] Add hard-copy conversion tests proving copied outputs remain valid after the source container is destroyed or mutated.
- [ ] Add move/consume tests only for explicit ownership-transfer APIs.

## Docs
- [ ] Update `docs/architecture/geometry.md` with symmetric `Mesh` / `Graph` / `PointCloud` domain-view semantics.
- [ ] Update `docs/reviews/2026-05-12-src-geometry-gap-analysis.md` if this policy materially changes the review conclusions.
- [ ] Update `docs/api/generated/module_inventory.md` after module surface changes.

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


