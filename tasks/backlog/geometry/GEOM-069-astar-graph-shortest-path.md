---
id: GEOM-069
theme: I
depends_on: [GEOM-068]
maturity_target: CPUContracted
---
# GEOM-069 — A* graph shortest path

## Goal
- Add deterministic single-source/single-target A* to `Geometry.Graph.ShortestPath`, reusing `GEOM-068`'s edge costs, result data, heap behavior, and failure contract.

## Non-goals
- No multi-goal search, navigation-mesh layer, path smoothing, bidirectional search, D*, all-pairs solver, or GPU backend.
- No heuristic interface, callback, registry, strategy class, or general search framework.
- No promise that an inadmissible caller-provided heuristic returns an optimal path.

## Context
- Owner/layer: `src/geometry`; `geometry -> core` only.
- `Engine25/Core/Frameworks/Geometry/GraphAStar.*` demonstrates the missing idea, but its implementation should not replace IntrinsicEngine's stronger indexed-heap and diagnostic contracts.
- A heuristic paired with arbitrary authored edge costs cannot safely default to geometric distance. The universally correct default is zero, which intentionally degenerates to `GEOM-068` Dijkstra.
- The heuristic is goal-specific data: use a borrowed, index-aligned per-vertex nonnegative estimate rather than a callable policy.

## Required changes
- [ ] Add a plain `AStar` free function for one valid source and one valid target in the existing shortest-path module.
- [ ] Reuse `GEOM-068`'s optional edge-cost view and result/path-extraction conventions.
- [ ] Accept an optional borrowed per-vertex heuristic-to-goal view; absent means zero. Validate coverage plus finite, nonnegative values before touching result properties.
- [ ] Document admissibility as the caller's optimality contract and implement vertex reopening so admissible-but-inconsistent heuristics remain correct.
- [ ] Define deterministic ordering for equal `f`, then equal `g`, using stable vertex identity as the final tie-break.
- [ ] Keep shared implementation details private; do not export a generic frontier/search-kernel abstraction.

## Tests
- [ ] With a zero heuristic, assert exact weighted-Dijkstra distance, path, predecessor, and deterministic repeat parity.
- [ ] Use an admissible grid heuristic and assert the same optimum with strictly fewer settled vertices than zero-heuristic search; make no wall-clock claim.
- [ ] Add an admissible-but-inconsistent fixture that requires reopening and still returns the optimum.
- [ ] Reject negative/NaN/Inf/misaligned heuristic or edge-cost data before partial writes.
- [ ] Cover invalid endpoints, deleted slots, an unreachable target, zero-cost edges, and equal-priority ties.

## Docs
- [ ] Document heuristic lifetime, alignment, default-zero, admissibility, inconsistency/reopening, and deterministic tie semantics.
- [ ] Regenerate `docs/api/generated/module_inventory.md` only if the public module surface changes.
- [ ] Update the geometry backlog dependency notes.

## Acceptance criteria
- [ ] A* returns the same optimal result as weighted Dijkstra for every admissible test fixture.
- [ ] Zero-heuristic behavior is a correctness oracle, not a separate implementation.
- [ ] Invalid inputs fail closed and unreachable targets use the documented result state.
- [ ] No new public policy/interface hierarchy is introduced.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ShortestPath|AStar' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Silently using Euclidean distance as a heuristic with arbitrary authored edge costs.
- Closing a vertex permanently when a better admissible path can reopen it.
- Turning two algorithms into a public search framework.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed.
