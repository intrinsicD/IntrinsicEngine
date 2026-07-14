# GRAPHICS-039D — Cluster build/assignment async-compute affinity

## Status
- Commit reference: this task-landing commit.
- Status: `completed`
- Owner/agent: Codex
- Branch: `main`
- Started: 2026-06-05
- Completed: 2026-06-05
- Current slice: single-slice `CPUContracted` affinity-tagging contract for the
  cluster grid build and light-assignment passes. The slice tags both passes
  `AsyncCompute`, proves capability-absent demotion to graphics through the
  existing framegraph/RHI helpers, and preserves single-queue correctness.
- Next verification step: retired. Real multi-queue Vulkan submission remains
  covered by `GRAPHICS-037D`; this slice consumes that queue-affinity surface
  and does not claim a new backend-specific GPU smoke.

## Goal
- Tag the cluster build and light-assignment passes with `QueueAffinity::AsyncCompute`
  (`GRAPHICS-039` decision 9) so they can overlap the current frame's raster
  when an async-compute queue is available, while demoting to the graphics queue
  when async compute is absent, with null-RHI tagging tests.

## Non-goals
- No correctness dependence on async-compute availability — the passes must
  remain correct on the graphics queue when async compute is unavailable.

## Context
- Owner layer: `graphics/renderer` (pass affinity tagging).
- Depends on `GRAPHICS-039C` (clustered lighting operational), `GRAPHICS-037A`
  (`QueueAffinity` surface), and the `GRAPHICS-037D` multi-queue recording proof
  for real async execution on Vulkan-capable hosts.
- Decision 9: cluster build + assignment are tagged `AsyncCompute`; devices
  without the optional async-compute queue demote those passes to graphics. The
  work is independent of the current frame's raster and overlaps naturally on
  async compute.

## Required changes
- [x] Tag the cluster build + assignment passes `QueueAffinity::AsyncCompute`.
- [x] Confirm capability-absent demotion to the graphics queue keeps the schedule correct
      (via the `GRAPHICS-037A` demotion helper).
- [x] `contract;graphics` null-RHI tests asserting the affinity tag + demotion fallback.

## Tests
- [x] `contract;graphics` — affinity tagging; demotion to graphics queue when async
      compute is absent; correctness preserved on the single-queue path.
- [x] CPU gate green.

## Docs
- [x] Note the async-compute affinity in `src/graphics/renderer/README.md`.

## Acceptance criteria
- [x] The cluster passes carry the async-compute affinity and demote correctly.
- [x] Correctness holds on the graphics queue (no dependence on async execution).
- [x] No new layering violations.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'FrameRecipeContract\.(LightClusterAssignmentRequiresGridBuildAndImportedOutputs|ClusterPassesRequestAsyncComputeAndDemoteToGraphics)|GraphicsQueueAffinity\.(ResolveQueueAffinityDemotesMissingOptionalQueues|MissingCapabilitiesDemoteIntoGraphicsBucketAndCountDemotions|BuildQueueSubmitPlanDemotesMissingOptionalQueues)|RendererFrameLifecycle\.(AsyncComputeQueuePlanIncrementsUtilizationStat|UsesDeviceFrameLifecycleBackbufferAndCommandContext)' --timeout 60
cmake --build --preset ci --target IntrinsicTests
cmake --build --preset ci --target IntrinsicBenchmarkSmoke
ctest --test-dir build/ci -L graphics -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
git diff --check
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
```

## Forbidden changes
- Making cluster correctness depend on async-compute availability.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Achieved: `CPUContracted` under the null RHI for the affinity-tagging +
  demotion contract.
- `Operational` real multi-queue recording is owned by `GRAPHICS-037D`; this
  affinity-tagging slice does not add a new backend-specific smoke.
