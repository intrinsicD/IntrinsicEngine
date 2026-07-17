---
id: GEOM-068
theme: I
depends_on: []
maturity_target: CPUContracted
---
# GEOM-068 — Weighted Dijkstra edge-cost contract

## Goal
- Extend the existing graph-domain Dijkstra free function with an optional borrowed, index-aligned nonnegative edge-cost view while keeping Euclidean edge length as the exact default.

## Non-goals
- No A* implementation; `GEOM-069` owns that algorithm after this cost contract is stable.
- No negative-weight, Bellman-Ford, all-pairs, minimum-spanning-tree, GPU, or generic graph-search framework.
- No `std::function`, virtual cost policy, registry, or serialized algorithm configuration.

## Context
- Owner/layer: `src/geometry`; `geometry -> core` only.
- `Geometry.Graph.ShortestPath` currently computes every relaxation from endpoint Euclidean distance, so authored traversal cost, risk, material, or semantic weights cannot participate.
- `Engine25/Core/Frameworks/Geometry/GraphDijkstra.cpp` is the strongest old-repository concept source for property-driven weights, but IntrinsicEngine already has the better indexed decrease-key heap, deterministic tie handling, diagnostics, deleted-slot guards, and multi-source/reverse/full-tree semantics. Adapt only the missing cost input.
- `Geometry.Properties` already provides borrowed const edge-property handles; no new ownership abstraction is needed.

## Required changes
- [ ] Add one optional borrowed/index-aligned edge-cost input to the existing `Dijkstra` surface. Only the explicit absent/default-constructed handle state selects the current Euclidean calculation; any valid supplied property, including an empty property on a graph with live edges, must cover indexed edge storage or fail.
- [ ] Validate supplied coverage and every live-edge cost before creating or modifying persistent distance/predecessor properties; values must be finite and nonnegative, and zero is valid.
- [ ] Use the same undirected edge cost from either halfedge traversal direction unless the current graph contract explicitly supports a directed representation.
- [ ] Preserve all existing empty-set, multi-source, reverse-tree, target-stop, deletion, settle-budget, diagnostics, and path-extraction semantics.
- [ ] Preserve the indexed heap and deterministic equal-cost predecessor/tie policy.

## Tests
- [ ] Add a crafted graph where the geometrically longer route has lower authored cost and assert distance, predecessor, and extracted path.
- [ ] Cover zero-cost edges and deterministic equal-cost ties.
- [ ] Reject negative, NaN, Inf, and undersized/misaligned cost data before partial property writes.
- [ ] Prove default Euclidean distances, predecessors, and diagnostics remain exact on the existing reference fixtures.
- [ ] Cover disconnected components and deleted edge/vertex slots with authored costs.

## Docs
- [ ] Document borrowed lifetime, index alignment, validation, zero-cost, and Euclidean-default semantics in `Geometry.Graph.ShortestPath`.
- [ ] Regenerate `docs/api/generated/module_inventory.md` only if the public module surface changes.
- [ ] Update `tasks/backlog/geometry/README.md` with the cross-repository audit provenance.

## Acceptance criteria
- [ ] A caller can select a lower-cost non-Euclidean path without copying the graph or exposing Eigen/EnTT types.
- [ ] Invalid authored costs fail before persistent result properties change.
- [ ] Calls without authored costs remain behavior- and diagnostics-identical.
- [ ] Default CPU geometry and layering gates pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ShortestPath' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Adding a callback-based cost strategy or a second graph container.
- Silently skipping invalid weights or treating them as infinity.
- Implementing `GEOM-069` in this slice.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed.
