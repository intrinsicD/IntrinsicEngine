# GEOM-003 — Split shared graph connectivity from mesh face incidence

## Goal
- Refactor geometry connectivity storage so graph traversal connectivity is shared between `Geometry::Graph` and `Geometry::HalfedgeMesh`, while mesh-only face incidence remains owned by `HalfedgeMesh`.

## Non-goals
- No broad shortest-path algorithm changes.
- No renderer, runtime, ECS, or UI feature changes beyond call-site updates required by the connectivity API migration.
- No performance claims without benchmark evidence.

## Context
- Status: done.
- Owner/agent: GitHub Copilot.
- Created: 2026-05-09 during BUG-002..BUG-005 hardening follow-up.
- Completed: 2026-05-09.
- Commit: this commit (`Split graph and mesh connectivity`).
- Owning subsystem/layer: `geometry` (`geometry -> core` only).
- Current issue: `Graph::VertexConnectivity` / `Graph::HalfedgeConnectivity` and `HalfedgeMesh::VertexConnectivity` / `HalfedgeMesh::HalfedgeConnectivity` duplicate graph traversal fields, but mesh halfedge connectivity also stores `FaceHandle`. Mesh-backed graph views currently need compatibility copying when property names collide by type.
- Desired direction: keep graph connectivity face-free and put mesh face incidence into a separate mesh-owned property, rather than adding `FaceHandle` to `Graph::HalfedgeConnectivity`.

## Implementation notes
- `HalfedgeMesh::VertexConnectivity` and `HalfedgeMesh::HalfedgeConnectivity` are aliases for the canonical `Graph` traversal connectivity records.
- Mesh-owned halfedge face incidence is stored in `HalfedgeMesh::HalfedgeFaceConnectivity` under `h:face`.
- `Graph::EnsureProperties()` no longer imports `HalfedgeMesh` or creates `v:graph_connectivity` / `h:graph_connectivity` compatibility copies.
- `HalfedgeMesh::Mesh` now has an explicit move constructor so module-boundary return-by-value paths keep mesh property ownership deterministic after the public connectivity surface change.
- No temporary compatibility alias remains after this migration.

## Required changes
- Define shared graph traversal connectivity as the canonical representation for:
  - `Vertex -> representative HalfedgeHandle`.
  - `Halfedge -> to VertexHandle, Next HalfedgeHandle, Prev HalfedgeHandle`.
- Split mesh face incidence into a mesh-only type/property such as `HalfedgeMesh::HalfedgeFaceConnectivity` containing `FaceHandle`.
- Migrate `HalfedgeMesh` internals to use the shared graph-compatible connectivity for traversal and the mesh-only face property for face ownership.
- Update graph-view construction and callers so mesh-backed graph views no longer need graph-specific compatibility copies.
- Keep property names and migration shims reviewable; document any temporary compatibility aliases with removal criteria.

## Tests
- Update `tests/unit/geometry/Test_ShortestPath.cpp` to prove mesh-backed graph views reuse shared connectivity without compatibility-copy properties.
- Add or update mesh topology tests covering face incidence after the split.
- Run focused geometry tests:
  ```bash
  cmake --build --preset ci --target IntrinsicGeometryTests
  ctest --test-dir build/ci --output-on-failure -R 'ShortestPath|HalfedgeMesh|MeshTopology' --timeout 60
  ```

## Docs
- Update geometry architecture/API docs if public connectivity type names or property names change.
- Update `docs/api/generated/module_inventory.md` if module surfaces change.
- Update this task with the final migration notes and any compatibility alias removal follow-up.

## Acceptance criteria
- `Graph::HalfedgeConnectivity` remains face-free.
- `HalfedgeMesh` stores `FaceHandle` incidence separately from graph traversal connectivity.
- Mesh-backed graph views can traverse mesh topology without copying graph compatibility connectivity.
- Existing graph and mesh topology tests pass.
- No new dependency edge outside `geometry -> core` is introduced.

## Verification
```bash
cmake --preset ci -DFETCHCONTENT_BASE_DIR=/home/alex/Documents/IntrinsicEngine/build/ci-agent-deps -DINTRINSIC_DEPS_CACHE_DIR=/home/alex/Documents/IntrinsicEngine/build/ci-agent-deps
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure -R 'ShortestPath|HalfedgeMesh|MeshTopology' --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

Results on 2026-05-09:

- Fresh `build/ci` configure and `IntrinsicGeometryTests` build passed.
- Focused geometry CTest passed: 93/93 tests.
- `IntrinsicTests` build passed after resuming from the terminal timeout.
- Default CPU CTest gate completed but failed 14 graphics/runtime graphics tests outside the GEOM-003 touched scope: `GpuAssetCache.*`, `GraphicsMaterialSystem.ResolvesReadyTextureAssetBindingsToBindlessMaterialParams`, and `RendererFrameLifecycle.*`. Geometry tests in the gate passed.
- Layering, test layout, task policy, and doc link checks passed.

## Forbidden changes
- Do not add `FaceHandle` to `Graph::HalfedgeConnectivity`.
- Do not make `Graph` depend on mesh/surface face semantics.
- Do not mix this semantic connectivity migration with unrelated CI/dependency-cache fixes.

