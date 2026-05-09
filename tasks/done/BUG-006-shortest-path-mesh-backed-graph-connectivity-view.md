# BUG-006 — Mesh-backed graph views abort ShortestPath tests on connectivity type collision

## Goal
- Make mesh-backed `Geometry::Graph::Graph` views safe for graph-domain shortest-path traversal without asserting on invalid graph connectivity properties.

## Non-goals
- No broad connectivity storage migration; that semantic cleanup is tracked separately by `tasks/backlog/geometry/GEOM-003-shared-graph-mesh-connectivity.md`.
- No changes to shortest-path algorithm semantics.
- No graph/mesh public API redesign in this bugfix.

## Context
- Status: done 2026-05-09.
- Owner/agent: Copilot.
- Owning subsystem/layer: `geometry` (`geometry -> core` only), with Editor UI call-site construction in `src/legacy`.
- Symptom: after dependency/cache bugs were fixed, the default CPU CTest gate exposed three aborting `ShortestPath` tests:
  - `ShortestPath.MeshBackedGraphViewTriangleChoosesDirectEdge`
  - `ShortestPath.ReverseTreeWhenStartsEmpty`
  - `ShortestPath.ForwardTreeWhenTargetsEmpty`
- Failure: `PropertyBuffer<Geometry::Graph::VertexConnectivity>::operator[]` asserted because a mesh-backed graph view used mesh property storage where `v:connectivity` and `h:connectivity` existed with `HalfedgeMesh::*Connectivity` types, not `Graph::*Connectivity` types.
- Root cause: mesh-backed graph view construction also passed edge and halfedge property sets in the wrong order in tests/UI call sites.

## Required changes
- Fix mesh-backed graph view construction to pass `VertexProperties`, `HalfedgeProperties`, then `EdgeProperties` in `Graph::Graph` constructor order.
- Let `Geometry::Graph::Graph::EnsureProperties()` create graph-specific compatibility connectivity properties when it detects mesh connectivity properties with colliding names.
- Keep the compatibility-copy fix small and defer the shared-connectivity semantic migration to `GEOM-003`.

## Tests
- Rebuild geometry and aggregate tests.
- Run focused `ShortestPath` tests.
- Run the default CPU-supported CTest gate.

## Docs
- Add this completion task.
- Add the long-term connectivity split task `GEOM-003`.

## Acceptance criteria
- Mesh-backed `ShortestPath` tests no longer abort.
- Full default CPU-supported CTest gate passes.
- No new cross-layer dependency is introduced.
- Temporary compatibility behavior has a tracked follow-up (`GEOM-003`).

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R '^ShortestPath\.' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not add `FaceHandle` to `Graph::HalfedgeConnectivity`.
- Do not make graph-domain APIs depend on mesh face semantics.
- Do not combine the full shared-connectivity migration with this compatibility bugfix.

## Captured evidence
- `build/ci-full-logs/ctest_cpu_after_dependency_cache_fixes.log` showed 3 aborting `ShortestPath` tests and the `Geometry.Properties.cppm:309` assertion.

## Completion
- Completed: 2026-05-09.
- Commit reference: pending.
- Notes: `Geometry.Graph.cpp` now creates graph-specific compatibility connectivity properties from mesh connectivity when constructing graph views over mesh property storage. `Test_ShortestPath.cpp` and `Runtime.EditorUI.Widgets.cpp` now pass halfedge/edge property sets in the correct `Graph::Graph` constructor order.
- Verification logs:
  - `build/ci-full-logs/build_shortest_path_graph_view_fix.log`
  - `build/ci-full-logs/ctest_shortest_path_after_graph_view_fix.log`
  - `build/ci-full-logs/ctest_cpu_after_shortest_path_fix.log`

