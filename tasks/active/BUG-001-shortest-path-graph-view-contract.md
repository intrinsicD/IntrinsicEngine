# BUG-001 — Shortest path mesh-backed UI must pass a graph-domain view

## Goal
- Fix the shortest-path Editor UI mesh path so graph-domain shortest-path APIs receive a `Geometry::Graph::Graph` view derived from mesh property storage.

## Non-goals
- No new shortest-path overloads for `Geometry::HalfedgeMesh::Mesh`.
- No changes to shortest-path algorithm behavior.
- No broader Editor UI refactor.

## Context
- Status: in-progress.
- Owner/agent: Copilot Coding Agent.
- Branch/PR: `copilot/fix-shortest-path-graph-view` / PR #778.
- Symptom: `Runtime.EditorUI.Widgets.cpp` passes `*meshData->MeshRef` into exported graph-domain shortest-path APIs, causing the clang CI build to fail.
- Expected behavior: mesh-backed shortest-path flows construct a `Geometry::Graph::Graph` view from the mesh-bound property sets and call the graph-domain API with that view.
- Impact: the Editor UI shortest-path widget does not build in CI, and the intended domain contract is under-specified in tests.

## Required changes
- Update the mesh branch for "Compute Shortest Path" in `src/legacy/EditorUI/Runtime.EditorUI.Widgets.cpp` to build a graph view from mesh property storage before calling `Geometry::ShortestPath::Dijkstra`.
- Update the mesh branch for "Extract Path Graph" in the same file to build the same graph view before calling `Geometry::ShortestPath::Dijkstra` and `Geometry::ShortestPath::ExtractPathGraph`.
- Tighten `tests/unit/geometry/Test_ShortestPath.cpp` naming/comments so mesh-backed coverage explicitly uses a graph view.

## Tests
- Keep mesh-backed shortest-path tests graph-view based.
- Re-run focused structural/test checks relevant to the touched files.

## Docs
- No docs changes required beyond this active task record and directly related code comments.

## Acceptance criteria
- Editor UI shortest-path widget mesh branches no longer call exported shortest-path APIs with `HalfedgeMesh::Mesh`.
- Mesh-backed shortest-path tests explicitly document graph-view usage.
- Fix does not introduce layering violations.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R ShortestPath --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes
- Adding new exported or public shortest-path overloads for mesh input.
- Refactoring unrelated Editor UI logic.
- Changing shortest-path result semantics.

## Execution log
- 2026-05-09: Investigated GitHub Actions run/job failure and confirmed the exported shortest-path API is graph-domain only while the widget mesh path passed `HalfedgeMesh::Mesh`.
- 2026-05-09: Updated the Editor UI mesh-backed shortest-path branches to construct a `Geometry::Graph::Graph` view from mesh property storage before calling `Dijkstra` / `ExtractPathGraph`.
- 2026-05-09: Renamed mesh-backed shortest-path tests/helper to make the graph-view contract explicit and updated the module comment accordingly.
- 2026-05-09: `python3 tools/agents/check_task_policy.py --root . --strict` and `python3 tools/repo/check_test_layout.py --root . --strict` passed.
- 2026-05-09: Local `cmake --preset ci` reproduction is blocked in this sandbox because `clang-20` is unavailable on `PATH`; fallback local builds were also limited by missing `clang-scan-deps`, missing Xrandr headers, and an unrelated `std::expected` toolchain issue in `Core.Error.cppm`.

## Next verification step
- Run the final review/security validation and rely on CI to compile the restored Editor UI path with the full clang workflow environment.
