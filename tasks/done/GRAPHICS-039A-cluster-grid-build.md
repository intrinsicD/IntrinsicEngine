# GRAPHICS-039A — Cluster grid resource + build pass

## Status
- Commit reference: this task-landing commit.
- Status: `completed`
- Owner/agent: Codex
- Branch: `main`
- Started: 2026-06-04
- Completed: 2026-06-04
- Current slice: single-slice `CPUContracted` implementation. Landed the
  promoted cluster-grid value contract, per-cell view-space AABB resource
  description with clamped partial-edge tile bounds, null-RHI build-pass
  recording seam, and shader asset without light assignment, shader
  consumption, or async-compute affinity.
- Next verification step: retired. Light assignment remains `GRAPHICS-039B`,
  surface-shader integration remains `GRAPHICS-039C`, and async-compute
  affinity remains `GRAPHICS-039D`.

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
- [x] Declare the cluster-grid resource (per-cell AABB buffer + grid dimensions).
- [x] Add the cluster build compute pass producing per-cell view-space AABBs.
- [x] `contract;graphics` null-RHI tests for grid dimensions, the tile-px formula, the
      log-Z slice mapping, and per-cell AABB correctness.

## Tests
- [x] `contract;graphics` — grid dimension formula; log-Z slicing; per-cell AABB bounds;
      partial-edge tile clamping; empty cells beyond far.
- [x] CPU gate green.

## Docs
- [x] Document the cluster grid + build pass in `src/graphics/renderer/README.md`.
- [x] Regenerate the module inventory if surfaces change.

## Acceptance criteria
- [x] The cluster grid resource + build pass exist and are CPU-tested.
- [x] No new layering violations.
- [x] `GRAPHICS-039B/C/D` remain the assignment/shader/async follow-ups.

## Verification
```bash
glslc assets/shaders/cluster_grid_build.comp -I assets/shaders -o /tmp/cluster_grid_build.comp.spv --target-env=vulkan1.3
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'GraphicsLightClusterGrid|FrameRecipeContract.ClusterGridBuildRequiresDepthPrepassAndImportedTarget' --timeout 60
ctest --test-dir build/ci -L graphics -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
git diff --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/check_root_hygiene.py --root .
python3 tools/repo/check_pr_contract.py
```

## Forbidden changes
- Live ECS access from renderer code (lights/camera arrive through snapshots).
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Achieved: `CPUContracted` under the null RHI for the cluster-grid build contract.
- `Operational` owned by `GRAPHICS-039C` (surface-shader integration + integration tests;
  a shader-side `gpu;vulkan` smoke rides on that operational consumer).
