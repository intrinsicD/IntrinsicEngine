# GRAPHICS-039A — Cluster grid resource + build pass

## Goal
- Land the froxel cluster grid (16×9×24 with the tile-px formula, logarithmic view-Z
  slicing) and its per-frame build compute pass that produces one view-space AABB per
  cell (`GRAPHICS-039` decisions 1/2), with `contract;graphics` null-RHI AABB-shape tests.

## Non-goals
- No light-to-cluster assignment (that is `GRAPHICS-039B`).
- No surface-shader integration (that is `GRAPHICS-039C`).
- No async-compute affinity (that is `GRAPHICS-039D`).

## Context
- Owner layer: `graphics/renderer` (cluster build pass + grid resource).
- Depends on `GRAPHICS-039` (planning, done) and `GRAPHICS-009` (deferred lighting, done).
- Decision 1: grid `16×9×24` at 16:9 scaled by `tilesX = ceil(w / clusterTilePx)`,
  `tilesY = ceil(h / clusterTilePx)` (`clusterTilePx` default 80), Z fixed 24 slices,
  logarithmic view-Z (`slice = floor(log(z/near)/log(far/near) * numZSlices)`), clamped
  to near/far. Decision 2: a compute pass produces one view-space AABB per cell, rebuilt
  every frame (reuse-when-static rejected for simplicity/robustness).

## Required changes
- [ ] Declare the cluster-grid resource (per-cell AABB buffer + grid dimensions).
- [ ] Add the cluster build compute pass producing per-cell view-space AABBs.
- [ ] `contract;graphics` null-RHI tests for grid dimensions, the tile-px formula, the
      log-Z slice mapping, and per-cell AABB correctness.

## Tests
- [ ] `contract;graphics` — grid dimension formula; log-Z slicing; per-cell AABB bounds;
      empty cells beyond far.
- [ ] CPU gate green.

## Docs
- [ ] Document the cluster grid + build pass in `src/graphics/renderer/README.md`.
- [ ] Regenerate the module inventory if surfaces change.

## Acceptance criteria
- [ ] The cluster grid resource + build pass exist and are CPU-tested.
- [ ] No new layering violations.
- [ ] `GRAPHICS-039B/C/D` remain the assignment/shader/async follow-ups.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci -L graphics -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Live ECS access from renderer code (lights/camera arrive through snapshots).
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `CPUContracted` under the null RHI for the cluster-grid build contract.
- `Operational` owned by `GRAPHICS-039C` (surface-shader integration + integration tests;
  a shader-side `gpu;vulkan` smoke rides on that operational consumer).
