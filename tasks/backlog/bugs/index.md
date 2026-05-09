# IntrinsicEngine — Active Bug Notes

This file tracks **currently reproducible correctness bugs, flaky tests, and test-harness defects**.
Each entry includes the observed repro, the likely affected symbols, and a fix plan aimed at a robust engine-level correction rather than a one-off patch.

## Active Issues

- `BUG-002` — [`CI full build compiles ImGuizmo upstream target without ImGui includes`](BUG-002-ci-full-build-imguizmo-upstream-target.md). Repro: `cmake --build --preset ci` fails in `external/cache/imguizmo-build/CMakeFiles/imguizmo.dir/*` with `fatal error: 'imgui.h' file not found`.
- `BUG-003` — [`FetchContent cache corruption breaks dependency checkouts during CI retries`](BUG-003-fetchcontent-cache-corrupts-shared-dependency-checkouts.md). Repro: repeated configure/build attempts against `external/cache/` leave partial GLM/JSON/Volk trees that later fail with missing headers, Git lock/ref errors, or missing `volk.h`.
- `BUG-004` — [`Compile-hotspot gate baseline references stale runtime source paths`](BUG-004-compile-hotspot-baseline-stale-runtime-paths.md). Repro: `tools/analysis/compile_hotspots.py` exits status 2 because `tools/analysis/compile_hotspot_baseline.json` still names migrated `src/Runtime/...` sources.
- `BUG-005` — [`CI dependent steps report missing artifacts as primary failures`](BUG-005-ci-dependent-steps-report-missing-artifacts-as-primary-failures.md). Repro: CTest, architecture SLO, and benchmark-result validation emit `*_NOT_BUILT`, missing-binary, or missing-directory failures after prerequisite build targets fail.

---

## Verified / Closed

- Closed: the older `BuildDefaultPipelineRecipe(...)` link-failure note is stale in the current tree. The symbol is declared in `Graphics.Pipelines.cppm`, defined in `Graphics.Pipelines.cpp`, and referenced by the runtime graphics tests. Full local link verification is currently blocked in this container because CMake configure stops in GLFW dependency discovery before test targets are generated (`libxrandr` development headers missing).
- Verified: mesh vertex indices are recovered from picked local-space points via KD-tree lookup.
- Verified: mesh edge/face, graph node/edge, and point-cloud point indices are covered by the focused picker regression suite.
- Closed: pick-domain policy now enforces mesh→surface face IDs, graph→edge IDs, and point-cloud→point IDs in `PickingPass`; GPU primitive IDs are authoritative while CPU is refinement-only in `ResolveGpuSubElementPick`.
