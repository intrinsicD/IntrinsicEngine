# IntrinsicEngine — Active Bug Notes

This file tracks **currently reproducible correctness bugs, flaky tests, and test-harness defects**.
Each entry includes the observed repro, the likely affected symbols, and a fix plan aimed at a robust engine-level correction rather than a one-off patch.

## Active Issues

- `IntrinsicTests` currently fails to link in `tests/Test_RuntimeGraphics.cpp` with an undefined reference to `Graphics::BuildDefaultPipelineRecipe(Graphics::DefaultPipelineRecipeInputs const&)` after `IntrinsicGraphics` builds successfully.
  - Observed when rebuilding the Htex patch preview lane.
  - Likely affected symbols: `Graphics::BuildDefaultPipelineRecipe(...)`, `Test_RuntimeGraphics.cpp`.
  - Status: not introduced by the Htex patch preview work; this path was unchanged here.
  - Fix plan: inspect the `Graphics` module export/definition wiring for the default pipeline recipe and restore the missing link target or export.

---

## Verified / Closed

- Verified: mesh vertex indices are recovered from picked local-space points via KD-tree lookup.
- Verified: mesh edge/face, graph node/edge, and point-cloud point indices are covered by the focused picker regression suite.
- Closed: pick-domain policy now enforces mesh→surface face IDs, graph→edge IDs, and point-cloud→point IDs in `PickingPass`; GPU primitive IDs are authoritative while CPU is refinement-only in `ResolveGpuSubElementPick`.
