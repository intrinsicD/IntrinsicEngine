# GRAPHICS-011 — Spatial debug visualizers

## Status
- State: done.
- Owner/agent: local agent workflow.
- Activated: 2026-05-03 after `GRAPHICS-010` completion.
- Completed: 2026-05-03.
- PR/commit: pending local commit.
- Completed slice: added `Extrinsic.Graphics.SpatialDebugVisualizers`, deterministic data-only bounds/hierarchy/split-plane/convex-hull/point packet builders, diagnostics/budget limits, docs, and CPU unit tests.
- Follow-up questions: `tasks/backlog/rendering/GRAPHICS-011Q-spatial-debug-adapter-clarifications.md`.

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
- Label spatial debug visualizer unit tests `unit;graphics` so they run in the default CPU gate.
## Docs
- Document supported spatial debug visualization inputs and graphics-layer ownership boundaries.
## Acceptance criteria
- Spatial debug visualizers produce render snapshots without legacy modules.
- Debug visualization output is bounded and deterministic.
- Ownership remains compatible with graphics layer rules.
## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsRendererCpuUnitTests -j 4
ctest --test-dir build/ci --output-on-failure -R 'GraphicsSpatialDebugVisualizers' --timeout 60
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing editor/runtime ownership into `src/graphics`.

## Follow-up cross-link
`GRAPHICS-024` (overlays/presentation/editor handoff planning) confirms that
spatial debug visualizers continue on the data-only adapter contract defined in
this task: runtime/editor/app produces snapshot records and graphics never
holds live geometry trees, ECS state, or editor mutation. Continued runtime
adapter clarifications stay in `GRAPHICS-011Q`. See the overlay / presentation /
editor handoff inventory in
`../../docs/migration/nonlegacy-parity-matrix.md` for the per-row owner matrix.
This appendix does not modify acceptance criteria or completion metadata.
