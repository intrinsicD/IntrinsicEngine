# IntrinsicEngine — Active Bug Notes

This file tracks **currently reproducible correctness bugs, flaky tests, and test-harness defects**.
Each entry includes the observed repro, the likely affected symbols, and a fix plan aimed at a robust engine-level correction rather than a one-off patch.

## Active Issues

- Currently no active reproducible issues are tracked.

---

## Verified / Closed

- Closed 2026-05-09: [`BUG-002` — CI full build compiles ImGuizmo upstream target without ImGui includes](../../done/BUG-002-ci-full-build-imguizmo-upstream-target.md). ImGuizmo is populated as source-only and repository consumers use `imguizmo_lib` with the ImGui dependency wired explicitly.
- Closed 2026-05-09: [`BUG-003` — FetchContent cache corruption breaks dependency checkouts during CI retries](../../done/BUG-003-fetchcontent-cache-corrupts-shared-dependency-checkouts.md). Dependency source trees are validated before reuse and incomplete online caches are removed before repopulation.
- Closed 2026-05-09: [`BUG-004` — Compile-hotspot gate baseline references stale runtime source paths](../../done/BUG-004-compile-hotspot-baseline-stale-runtime-paths.md). The baseline now uses current `src/geometry/` and `src/legacy/` paths.
- Closed 2026-05-09: [`BUG-005` — CI dependent steps report missing artifacts as primary failures](../../done/BUG-005-ci-dependent-steps-report-missing-artifacts-as-primary-failures.md). CI dependent steps now run explicit prerequisite guards and benchmark validation reports missing result roots as blocked prerequisites.
- Closed 2026-05-09: [`BUG-006` — Mesh-backed graph views abort ShortestPath tests on connectivity type collision](../../done/BUG-006-shortest-path-mesh-backed-graph-connectivity-view.md). Mesh-backed graph view construction now uses the correct property-set order and graph-specific compatibility connectivity until `GEOM-003` performs the semantic split.
- Closed: the older `BuildDefaultPipelineRecipe(...)` link-failure note is stale in the current tree. The symbol is declared in `Graphics.Pipelines.cppm`, defined in `Graphics.Pipelines.cpp`, and referenced by the runtime graphics tests. Full local link verification is currently blocked in this container because CMake configure stops in GLFW dependency discovery before test targets are generated (`libxrandr` development headers missing).
- Verified: mesh vertex indices are recovered from picked local-space points via KD-tree lookup.
- Verified: mesh edge/face, graph node/edge, and point-cloud point indices are covered by the focused picker regression suite.
- Closed: pick-domain policy now enforces mesh→surface face IDs, graph→edge IDs, and point-cloud→point IDs in `PickingPass`; GPU primitive IDs are authoritative while CPU is refinement-only in `ResolveGpuSubElementPick`.
