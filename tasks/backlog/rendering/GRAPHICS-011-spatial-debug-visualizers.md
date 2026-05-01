# GRAPHICS-011 — Spatial debug visualizers
## Goal
- Reimplement BVH, bounding volume, KD-tree, octree, convex-hull, and similar spatial debug visualizers as snapshot-producing helpers for graphics debug primitives.
## Non-goals
- No new spatial data structure algorithms unless required for visualization seams.
- No editor UI controller implementation.
- No direct dependency from graphics on higher-layer ECS ownership.
## Context
- Owner: primarily `src/graphics/renderer` for visualization packets, with geometry/core helpers only through allowed public APIs.
- Legacy spatial debug draw modules identify expected visualization categories but should not be copied.
## Required changes
- Define data-only debug visualization packet builders or adapters for supported spatial structures.
- Route output through transient line/point/triangle debug primitive contracts from `GRAPHICS-010`.
- Add diagnostics for unsupported structures, excessive primitive counts, and invalid bounds.
## Tests
- Add unit tests for deterministic packet generation from minimal BVH/bounds/KD-tree/octree/convex-hull fixtures.
- Add clamp/limit tests for large or invalid debug visualizations.
## Docs
- Document supported spatial debug visualization inputs and graphics-layer ownership boundaries.
## Acceptance criteria
- Spatial debug visualizers produce render snapshots without legacy modules.
- Debug visualization output is bounded and deterministic.
- Ownership remains compatible with graphics layer rules.
## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing editor/runtime ownership into `src/graphics`.
