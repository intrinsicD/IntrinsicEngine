---
id: HARDEN-087
theme: D
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "Codex-HARDEN087"
branch: "ecs/harden-087-unified-geometry-sources"
worktree: "/tmp/intrinsic-harden087.0mo5us"
claimed_at: "2026-08-02T16:45:09Z"
contract_schema: 1
contracts: [repo.task-contract-discovery, geometry.element-domain-sources]
maturity_target: Operational
---
# HARDEN-087 — Unified geometry element-source components

## Status

- Completed on 2026-08-02 at `Operational`. Point clouds, graphs, and meshes
  now share the canonical physical `Vertices` source; graphs and meshes also
  expose `Halfedges` and `Edges`, while only meshes expose `Faces`.
  Provenance remains a separate marker/query from source capability.
- Graph materialization preserves the graph's real halfedge connectivity and
  all custom vertex/halfedge/edge properties. Runtime method bindings,
  availability, extraction, selection, editing/history, scene JSON v2, and
  Sandbox domain models consume the same sources without a converter or
  duplicate `Nodes` storage.
- The required garbage-compaction regression exposed a pre-existing
  `Geometry::Graph::GarbageCollection` double-swap of transient remap
  properties. The owning geometry implementation now lets those map rows move
  once with their `PropertySet`, keeping surviving vertex/halfedge handles
  in-range before ECS copies them.
- Verification passed 177/177 focused contracts, 3,996/3,996 default CPU
  cases, and 2,649/2,649 cases under each of ASan and UBSan. The first
  unchanged final UBSan attempt reproduced the already-open `BUG-123`
  retired-scene-save terminal-event race; that failed receipt remains archived,
  and the unchanged selector then passed without weakening or excluding the
  test. The promoted Vulkan selector was not rerun because no
  graphics/RHI/backend/shader or GPU
  byte-layout behavior changed; its graph fixture now materializes the same
  tested ECS sources before entering the unchanged extraction path.
- Commit: the implementation/retirement candidate plus its generated evidence
  bundle and accepted fixed-revision review provide the exact source binding.
- Architecture review:
  [`2026-08-02-harden-087-clean-workshop-review.md`](../../docs/reviews/2026-08-02-harden-087-clean-workshop-review.md).

## Goal

- Make ECS geometry materialization match the canonical element-domain matrix:
  point cloud, graph, and mesh use `Vertices`; graph and mesh use `Halfedges`
  and `Edges`; mesh additionally uses `Faces`, while provenance remains a
  separate marker/query.

## Non-goals

- No method algorithm, UI workflow, renderer, asset-import, or topology feature
  change. The only geometry-algorithm edit is the prerequisite correction that
  makes existing graph garbage collection honor its compact-connectivity
  contract before ECS materialization.
- No duplicate long-lived `Nodes`/`Vertices` storage, compatibility facade, or
  conversion between entity kinds.
- No claim that graph halfedges have faces; graph-specific topology remains
  represented by its real connectivity properties.

## Context

- At intake, the code contradicted the intended contract: `PopulateFromGraph`
  created
  `Nodes + Edges + HasGraphTopology` and explicitly omits `Halfedges`, while
  mesh creates `Vertices + Edges + Halfedges + Faces` and point cloud creates
  `Vertices`. `HARDEN-065` recorded this as an intentional bounded decision;
  this task is the explicit reviewed supersession.
- `Geometry::Graph::Graph` already owns vertex, halfedge, and edge
  `PropertySet`s. The migration should expose those existing domains through
  ECS rather than synthesize a point cloud or copy graph data twice.
- During verification, a graph with deleted vertices/edges proved that the
  existing graph garbage collector compacted rows but left connectivity at old
  indices. Fixing that owner-level remap defect is required for this task's
  promised valid post-compaction source matrix; no API or algorithm variant was
  added.
- Literature basis: Baumgart's original winged-edge representation established
  explicit oriented adjacency as topology rather than a face-only storage
  concern. Kettner's generic halfedge design and its CGAL realization make the
  more directly applicable extension explicit: a reduced halfedge data
  structure can represent an undirected graph without vertex or face records,
  while optional item records and properties remain orthogonal to connectivity.
  OpenMesh's later generic property model uses the same four element families
  (`Vertex`, `Halfedge`, `Edge`, `Face`) with per-element properties. These
  references support exposing the graph's real halfedge set and keeping graph
  provenance separate; they do not justify fabricating graph faces.
  - Bruce G. Baumgart, *Winged Edge Polyhedron Representation*, Stanford
    CS-TR-72-320 (1972):
    <https://infolab.stanford.edu/pub/cstr/reports/cs/tr/72/320/CS-TR-72-320.pdf>
  - Lutz Kettner, *Designing a data structure for polyhedral surfaces*, SCG
    1998, DOI 10.1145/276884.276901: <https://doi.org/10.1145/276884.276901>
  - Lutz Kettner, *Using generic programming for designing a data structure
    for polyhedral surfaces*, Computational Geometry 13(1), 1999, DOI
    10.1016/S0925-7721(99)00007-3:
    <https://doi.org/10.1016/S0925-7721(99)00007-3>
  - Mario Botsch et al., *OpenMesh — a generic and efficient polygon mesh data
    structure* (2002): <https://www.graphics.rwth-aachen.de/publication/03130/>
  - CGAL `HalfedgeDS` design documentation:
    <https://doc.cgal.org/Manual/3.3/doc_html/cgal_manual/HalfedgeDS/Chapter_main.html>

## Right-sizing decision

- Element under review: a `Nodes` alias, compatibility facade, duplicate
  graph-vertex component, or graph-to-point-cloud adapter could hide the
  mismatch while retaining two physical source vocabularies.
- Simpler alternative chosen: replace graph materialization with the existing
  shared `Vertices`, `Halfedges`, and `Edges` components, retain
  `HasGraphTopology` as provenance, and migrate consumers directly. Logical
  `GraphNode` vocabulary remains only where it describes graph semantics.
- Blast radius: ECS source modules; the graph garbage-collection implementation
  and its focused regression; runtime availability, property,
  extraction, serialization, history, and editor-model consumers; their tests
  and architecture/task documentation. No new dependency edge or subsystem is
  introduced.
- Reintroduction trigger: add a distinct storage abstraction only if a real
  second storage semantic cannot satisfy the existing component/`PropertySet`
  contract, under a new reviewed task with at least one present consumer.

## Control surfaces

- Config: N/A; component materialization is an internal source contract.
- UI: Existing logical `GraphNode` labels remain; UI consumes runtime
  capability/readiness rather than physical component names.
- Agent/CLI: Geometry source/property queries see the same unified components.

## Required changes

- [x] Change `PopulateFromGraph` to materialize graph vertex properties into
      `GeometrySources::Vertices`, graph halfedge properties into
      `GeometrySources::Halfedges`, and graph edge properties into
      `GeometrySources::Edges`, preserving canonical positions and graph
      connectivity after garbage collection.
- [x] Migrate `ConstSourceView`/`MutableSourceView`, domain detection,
      availability, property resolution, serialization, extraction, and every
      current consumer from the physical `Nodes`/`NodePoints` split to unified
      `Vertices`/vertex capability plus separate graph provenance.
- [x] Retire the `Nodes` component and `NodePoints` source capability after all
      production/test consumers migrate; retain logical graph-node UI/domain
      vocabulary where it describes semantics rather than storage.
- [x] Define and document the graph-halfedge property contract by reusing the
      graph's existing `HalfedgeProperties`; do not invent mesh face adjacency
      or erase graph-specific connectivity.
- [x] Preserve layer ownership (`ecs -> core`, explicitly required geometry
      types only), source generation/dirty behavior, custom properties, alive
      counts, and import/render/runtime behavior.
- [x] Correct the graph garbage-collection remap double-swap so a populate call
      after deletions publishes compact, in-range vertex and halfedge handles.
- [x] Update the geometry-source compatibility table and remove the temporary
      physical-layout caveat only after executable parity proves the migration.

## Tests

- [x] Extend `Test.ECS.GeometrySourcesPopulate` to assert the exact component
      matrix and copied custom properties for mesh, graph, and point cloud,
      including graph halfedges and absence of `Nodes`.
- [x] Extend geometry availability/property resolution contracts so provenance
      and element capability remain independent after unification.
- [x] Preserve focused K-Means, graph normals, graph extraction/presentation,
      scene serialization, and import/materialization contracts while migrating
      them from the former physical `NodeSource` to `VertexSource`.
- [x] Add a regression proving graph and mesh expose the same vertex/halfedge/
      edge physical component types without being misclassified by provenance.
- [x] Add graph- and ECS-level regressions proving garbage collection preserves
      surviving custom properties and remaps all connectivity into compact
      ranges before materialization.

## Docs

- [x] Update ECS/component/runtime architecture docs and the `HARDEN-065`/
      `HARDEN-083` supersession trail; regenerate the module inventory if any
      public module surface changes.

## Acceptance criteria

- [x] Freshly materialized entities have exactly the canonical component matrix
      and no graph-only duplicate vertex component.
- [x] Existing graph, mesh, and point-cloud runtime/render/import consumers pass
      with provenance preserved separately from source capability.
- [x] Graph halfedges are queryable as an ECS data source and no converter or
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
