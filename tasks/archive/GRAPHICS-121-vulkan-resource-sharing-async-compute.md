---
id: GRAPHICS-121
theme: B
depends_on:
  - GRAPHICS-120
maturity_target: Operational
completed: 2026-07-07
---
# GRAPHICS-121 — Vulkan resource sharing includes async compute

## Status
- Retired on 2026-07-07 at `Operational`.
- PR/commit: this retirement commit.
- Fix: Vulkan buffer/image create-info now includes graphics, live async
  compute, and live transfer queue families when resources may be submitted
  from those queues, and command contexts sanitize Sync2 barrier stage masks
  for the bound queue family.
- Evidence: the default-recipe Vulkan smoke passed on a Vulkan-capable host
  after the fix, and the default CPU-supported gate remained green.

## Goal
- Remove Vulkan validation errors where default-recipe framegraph resources submitted on the async compute queue were created without that queue family in their concurrent-sharing family list.

## Non-goals
- Redesigning framegraph queue scheduling, timeline semaphore ordering, or queue-family ownership-transfer policy.
- Changing the RHI public resource descriptor surface.
- Implementing the larger domain-driven mesh/graph/pointcloud appearance UI from GRAPHICS-105.

## Context
- Owner/layer: `graphics/vulkan`; RHI remains API-agnostic and no `Vk*` type crosses out of the backend.
- After GRAPHICS-119/120, render-graph passes can record and submit on async compute. The Vulkan backend already creates async command pools and queue-submit contexts, but buffers/images are currently created as concurrent only across graphics plus transfer.
- Validation reports `VUID-vkQueueSubmit-pSubmits-04626` for resources such as `ClusterLights.Headers`, `ClusterLights.Counter`, `PostProcess.Histogram`, and `SceneColorHDR` when a command buffer submitted to the async family accesses resources whose create-info omitted that family.

## Required changes
- [x] Include every live framegraph queue family that can submit render-graph work when building Vulkan buffer create-info.
- [x] Include every live framegraph queue family that can submit render-graph work when building Vulkan image create-info.
- [x] Keep memory-requirement and placed-resource create paths using the same sharing-family computation as normal resource creation.
- [x] Constrain Vulkan Sync2 barrier stage masks to stages valid for the command buffer's bound queue family.

## Tests
- [x] Existing async-compute `gpu;vulkan` default-recipe smoke no longer emits `VUID-vkQueueSubmit-pSubmits-04626`.
- [x] CPU/default gate remains buildable for the touched graphics/vulkan code.

## Docs
- [x] Update generated session brief after opening this task.
- [x] No architecture doc update required; this preserves the existing backend-local concurrent-sharing policy.

## Acceptance criteria
- [x] Vulkan buffers/images created by the promoted backend include graphics, async compute, and transfer queue families when those queues are live and distinct.
- [x] The async-compute default recipe can submit framegraph resources on the async queue without queue-family sharing validation errors.
- [x] Async-compute command buffers do not record graphics-only Sync2 stage masks.
- [x] No public RHI or renderer API grows Vulkan-specific queue-family details.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root .
python3 tools/repo/check_layering.py --root src --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Vulkan-capable host:
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -R DefaultRecipeSurfaceGpuSmoke --timeout 120
# Broader label intersection was run for signal; 270/272 passed. Remaining
# deterministic failures are unrelated to this backend fix:
# - GraphicsRenderGraph.LifetimeFirstAndLastUseTracksPassIndices
# - ExtrinsicSandbox.FramePacingDiagnosticCapture
```

## Forbidden changes
- Adding `Vk*` types to RHI/renderer/runtime public APIs.
- Hiding validation errors by disabling async compute, validation layers, or the default-recipe async-compute smoke.
- Mixing this backend fix with GRAPHICS-105's material/appearance UI redesign.

## Maturity
- Target: `Operational` on Vulkan-capable hosts.
