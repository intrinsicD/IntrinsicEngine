---
id: HARDEN-087
theme: D
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [repo.task-contract-discovery, geometry.element-domain-sources]
maturity_target: Operational
---
# HARDEN-087 — Unified geometry element-source components

## Goal

- Make ECS geometry materialization match the canonical element-domain matrix:
  point cloud, graph, and mesh use `Vertices`; graph and mesh use `Halfedges`
  and `Edges`; mesh additionally uses `Faces`, while provenance remains a
  separate marker/query.

## Non-goals

- No geometry algorithm, runtime method, UI, renderer, asset-import, or topology
  behavior change beyond consuming the unified ECS source components.
- No duplicate long-lived `Nodes`/`Vertices` storage, compatibility facade, or
  conversion between entity kinds.
- No claim that graph halfedges have faces; graph-specific topology remains
  represented by its real connectivity properties.

## Context

- Current code contradicts the intended contract: `PopulateFromGraph` creates
  `Nodes + Edges + HasGraphTopology` and explicitly omits `Halfedges`, while
  mesh creates `Vertices + Edges + Halfedges + Faces` and point cloud creates
  `Vertices`. `HARDEN-065` recorded this as an intentional bounded decision;
  this task is the explicit reviewed supersession.
- `Geometry::Graph::Graph` already owns vertex, halfedge, and edge
  `PropertySet`s. The migration should expose those existing domains through
  ECS rather than synthesize a point cloud or copy graph data twice.

## Control surfaces

- Config: N/A; component materialization is an internal source contract.
- UI: Existing logical `GraphNode` labels remain; UI consumes runtime
  capability/readiness rather than physical component names.
- Agent/CLI: Geometry source/property queries see the same unified components.

## Required changes

- [ ] Change `PopulateFromGraph` to materialize graph vertex properties into
      `GeometrySources::Vertices`, graph halfedge properties into
      `GeometrySources::Halfedges`, and graph edge properties into
      `GeometrySources::Edges`, preserving canonical positions and graph
      connectivity after garbage collection.
- [ ] Migrate `ConstSourceView`/`MutableSourceView`, domain detection,
      availability, property resolution, serialization, extraction, and every
      current consumer from the physical `Nodes`/`NodePoints` split to unified
      `Vertices`/vertex capability plus separate graph provenance.
- [ ] Retire the `Nodes` component and `NodePoints` source capability after all
      production/test consumers migrate; retain logical graph-node UI/domain
      vocabulary where it describes semantics rather than storage.
- [ ] Define and document the graph-halfedge property contract by reusing the
      graph's existing `HalfedgeProperties`; do not invent mesh face adjacency
      or erase graph-specific connectivity.
- [ ] Preserve layer ownership (`ecs -> core`, explicitly required geometry
      types only), source generation/dirty behavior, custom properties, alive
      counts, and import/render/runtime behavior.
- [ ] Update the geometry-source compatibility table and remove the temporary
      physical-layout caveat only after executable parity proves the migration.

## Tests

- [ ] Extend `Test.ECS.GeometrySourcesPopulate` to assert the exact component
      matrix and copied custom properties for mesh, graph, and point cloud,
      including graph halfedges and absence of `Nodes`.
- [ ] Extend geometry availability/property resolution contracts so provenance
      and element capability remain independent after unification.
- [ ] Preserve focused K-Means, graph normals, graph extraction/presentation,
      scene serialization, and import/materialization contracts that currently
      consume `NodeSource`.
- [ ] Add a regression proving graph and mesh expose the same vertex/halfedge/
      edge physical component types without being misclassified by provenance.

## Docs

- [ ] Update ECS/component/runtime architecture docs and the `HARDEN-065`/
      `HARDEN-083` supersession trail; regenerate the module inventory if any
      public module surface changes.

## Acceptance criteria

- [ ] Freshly materialized entities have exactly the canonical component matrix
      and no graph-only duplicate vertex component.
- [ ] Existing graph, mesh, and point-cloud runtime/render/import consumers pass
      with provenance preserved separately from source capability.
- [ ] Graph halfedges are queryable as an ECS data source and no converter or
      duplicate storage is introduced.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicECSTests IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'GeometrySources|GeometryAvailability|GraphGeometry|Clustering|GraphVertexNormals' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- No retained `Nodes` mirror, graph-to-point-cloud conversion, fabricated face
  data, provenance conflation, new higher-layer ECS dependency, or unrelated
  geometry API cleanup.
