# GRAPHICS-039C — Clustered surface-shader integration + recipe wiring

## Status
- Commit reference: this task-landing commit.
- Status: `completed`
- Owner/agent: Codex
- Branch: `main`
- Started: 2026-06-05
- Completed: 2026-06-05
- Current slice: single-slice `CPUContracted` clustered-light consumption
  contract. Adapted the older "global descriptor set SSBO pair" wording to the
  promoted renderer's BDA root-table architecture: `ClusterLights.Headers` /
  `ClusterLights.Indices` are published through `GpuSceneTable` BDAs and
  metadata, then consumed by forward/deferred lighting shaders with a full-loop
  fallback when clustered data is unavailable.
- Next verification step: retired. Async-compute affinity remains
  `GRAPHICS-039D`; a backend-specific `Operational` proof is not claimed by this
  CPU/null slice.

## Goal
- Publish the packed light-index buffer + offset/count header through the
  promoted renderer's `GpuSceneTable` BDA root and integrate per-cell light
  iteration into the surface/deferred lighting shaders (`GRAPHICS-039` decision
  6), with recipe wiring and integration tests.

## Non-goals
- No async-compute affinity (that is `GRAPHICS-039D`).
- No IBL probe parallel-buffer extension (decision 7 — owned by `GRAPHICS-042`).

## Context
- Owner layer: `graphics/renderer` (shader integration + recipe wiring).
- Depends on `GRAPHICS-039B` (assignment) and `GRAPHICS-008` (surface/G-buffer passes, done).
- Decision 6: bind the packed index buffer + offset/count header as a read-only SSBO pair
  on the per-frame global descriptor set (`set 0`, next free bindings against the then-
  current global layout); surface shaders derive their cell from `gl_FragCoord.xy` (tile)
  and view-Z (log-Z slice), read the header, and iterate only the assigned indices.
- Current promoted renderer adaptation: the Vulkan pipeline layout exposes one
  bindless descriptor heap at `set 0`, while scene storage buffers are reached
  through the `GpuSceneTable` BDA root table. This slice therefore publishes the
  cluster header/index buffers and grid metadata through `GpuSceneTable`; it does
  not add a new backend-neutral descriptor-set binding API.

## Required changes
- [x] Publish the cluster index/header SSBO pair through the scene-table BDA root.
- [x] Update the surface/deferred lighting shaders to derive the cell and iterate the
      per-cell light list instead of the full light loop.
- [x] Wire the cluster build + assignment + lighting consumption into the default recipe.
- [x] `contract;graphics` + integration tests for recipe wiring and per-cell iteration shape.

## Tests
- [x] `contract;graphics` — recipe includes cluster build/assignment before lighting;
      binding layout correctness.
- [x] `integration` — clustered lighting produces the same lit result as the full-loop
      path for a small known scene (parity within tolerance).
- [x] CPU gate green.

## Docs
- [x] Document the clustered surface-shader path in `src/graphics/renderer/README.md`
      and `docs/architecture/rendering-three-pass.md`.

## Acceptance criteria
- [x] Surface/deferred shaders iterate per-cell lists; recipe wiring is CPU-tested.
- [x] Clustered lighting matches the full-loop reference within tolerance.
- [x] No new layering violations.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'GraphicsLightClusterGrid|FrameRecipeContract\.(LightClusterAssignmentRequiresGridBuildAndImportedOutputs|ClusterGridBuildRequiresDepthPrepassAndImportedTarget)|RendererFrameLifecycle\.(UsesDeviceFrameLifecycleBackbufferAndCommandContext|OperationalRebuildAfterNonOperationalStartupRecordsRoutedCommands|DepthPrepassPipelineFailureSkipsUnavailableCommandPass|CullingPipelineFailureSkipsRoutedCommandPassesUnavailable|FrameRecipePassesAllProduceStructuredCommandRecordStatuses|ClusterLightingPipelinesAndSceneTablePublishSurviveRebuild|HZBBuildPassRecordsFallbackDispatches)' --timeout 60
for shader in assets/shaders/forward/default_debug_surface.frag assets/shaders/deferred/lighting.frag assets/shaders/cluster_grid_build.comp assets/shaders/light_cluster_assign.comp; do glslc "$shader" -I assets/shaders -o "/tmp/$(basename "$shader").spv" --target-env=vulkan1.3; done
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
cmake --build --preset ci --target IntrinsicTests
cmake --build --preset ci --target IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
git diff --check
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes
- Adding per-material light descriptors instead of the scene-table published
  clustered-light SSBO pair.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Achieved: `CPUContracted` for this slice — clustered-light publication and
  default-recipe command wiring are proven under the CPU/null gate and touched
  shaders compile through `glslc`.
- `Operational` owned by `GRAPHICS-039D` for async-compute pass affinity and
  backend scheduling proof; this slice does not claim opt-in GPU/Vulkan
  shader-iteration coverage.
