---
id: GEOM-074
theme: I
depends_on: [HARDEN-087, GEOM-068]
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [geometry.element-domain-sources]
maturity_target: CPUContracted
---
# GEOM-074 — Graph property and adjacency contracts

## Goal

- Make existing graph algorithms consume the least graph-domain data they need
  (named node/edge/halfedge properties plus adjacency) so the same operations
  work directly on meshes satisfying that contract.

## Non-goals

- No mesh-to-graph conversion, topology rewrite, algorithm/default change, or
  universal graph interface/factory.
- No runtime/UI binding; this task fixes the geometry-layer public seams.

## Context

- `Geometry.Graph.ShortestPath` and most `Geometry.Graph.Utils` operations take
  the owning `Graph` container; `ShortestPathResult` additionally exposes
  `VertexProperty`. Their actual requirements are adjacency and specific typed
  properties, which mesh vertex/halfedge/edge sources can provide.
- Re-read Dijkstra's original shortest-path note (DOI
  `10.1007/BF01386390`), Fruchterman–Reingold layout (DOI
  `10.1002/spe.4380211102`), and relevant later weighted/A*/multilevel layout
  improvements before each migrated family. Preserve current formulations and
  keep `GEOM-068`/`GEOM-069` ownership distinct.

## Required changes

- [ ] Inventory graph algorithms by exact node, edge, halfedge, position/cost,
      and mutation requirements; distinguish construction from analysis.
- [ ] Introduce the smallest borrowed adjacency/property records or free
      overloads needed by current graph and mesh callers; one implementation is
      not a reason for an abstract interface.
- [ ] Return generic property/span outputs unless handle-indexed access is
      genuinely required, and retain `Graph` adapters for compatibility.
- [ ] Prove mesh primal adjacency enters without copied topology or fabricated
      faces; publication changes only named same-cardinality properties.

## Tests

- [ ] Run shortest-path, edge-query/length, and layout representatives against
      equivalent graph and mesh sources and compare results/diagnostics.
- [ ] Cover custom cost/position properties, invalid adjacency, deleted slots,
      and property lifetime rules.
- [ ] Verify no owning conversion or unrelated property/topology mutation.

## Docs

- [ ] Update graph/geometry API docs with each algorithm's required adjacency
      and property contract plus literature selection/exclusion notes.

## Acceptance criteria

- [ ] A graph method works on a mesh whenever the mesh supplies its named
      adjacency/property contract.
- [ ] Container and handle-wrapper types are conveniences, not eligibility
      gates.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure -R 'Graph|ShortestPath' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- No topology conversion/copy, fabricated face adjacency, universal interface,
  or unreviewed algorithm/weighting change.
