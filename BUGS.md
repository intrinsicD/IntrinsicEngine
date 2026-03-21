# IntrinsicEngine — Active Bug Notes

This file tracks **currently reproducible correctness bugs, flaky tests, and test-harness defects**.
Each entry includes the observed repro, the likely affected symbols, and a fix plan aimed at a robust engine-level correction rather than a one-off patch.

## Active Issues

*(No active reproducible issues currently tracked.)*

---

## Verified / Closed

- Closed: the older `BuildDefaultPipelineRecipe(...)` link-failure note is stale in the current tree. The symbol is declared in `Graphics.Pipelines.cppm`, defined in `Graphics.Pipelines.cpp`, and referenced by the runtime graphics tests. Full local link verification is currently blocked in this container because CMake configure stops in GLFW dependency discovery before test targets are generated (`libxrandr` development headers missing).
- Verified: mesh vertex indices are recovered from picked local-space points via KD-tree lookup.
- Verified: mesh edge/face, graph node/edge, and point-cloud point indices are covered by the focused picker regression suite.
- Closed: pick-domain policy now enforces mesh→surface face IDs, graph→edge IDs, and point-cloud→point IDs in `PickingPass`; GPU primitive IDs are authoritative while CPU is refinement-only in `ResolveGpuSubElementPick`.
