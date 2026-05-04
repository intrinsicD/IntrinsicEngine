# GRAPHICS-018Q — Vulkan integration clarification follow-ups

## Goal
Capture Vulkan renderer integration questions that should not be implemented inside unrelated `GRAPHICS-018` plumbing slices.

## Non-goals
- No C++ behavior changes.
- No Vulkan smoke-test implementation.
- No changes to renderer/RHI ownership policy beyond documenting decisions.

## Context
`GRAPHICS-018` is active for wiring concrete Vulkan execution behind promoted graphics interfaces. The current implementation can proceed with CPU/null contract coverage while several environment- and backend-specific details remain open. Entries are split by whether they block the next operational Vulkan bring-up slice or are nonblocking clarification/follow-up work.

## Required changes

### Blocks next operational Vulkan slice

- Reconcile fail-closed fallback bindless heap and transfer queue behavior with real Vulkan services before `VulkanDevice::IsOperational()` can become true; the renderer/runtime reset prerequisite is complete in `tasks/done/GRAPHICS-018R-operational-transition.md`.
- Define shader source packaging and pipeline asset path policy for real pass rendering in Vulkan smoke tests.
- Decide the promoted depth-prepass shader asset path/packaging policy and whether a dedicated depth-only shader should be introduced before enabling real Vulkan depth-prepass smoke rendering.
- Decide ownership of dynamic-rendering attachment scope and transient texture materialization for `DepthPrepass`, `SurfacePass`, and downstream real pass command bodies.
- Define resize, acquire, present, and device-loss diagnostic taxonomy, including which diagnostics belong in `GRAPHICS-018` versus rendergraph validation hardening in `GRAPHICS-022`.

### Nonblocking clarifications and follow-ups

- Decide the platform/window fixture policy for opt-in swapchain smoke tests, including headless CI behavior and skip diagnostics when no surface-capable device is available.
- Decide the exact opt-in smoke-test assertion boundary between the promoted Vulkan lifecycle fail-closed path (`Initialize()` leaves `IsOperational() == false`) and the later real swapchain/device bring-up path (`BeginFrame()` succeeds with a valid backbuffer handle).
- Decide whether concrete Vulkan pipeline creation should land before or after swapchain bring-up, including the minimal CPU-testable diagnostics for currently fail-closed `CreatePipeline` paths.
- Decide the texture upload policy beyond the current synchronous `WriteTexture()` path, including async transfer-queue use, per-subresource layout tracking, mip-chain/cubemap batching, and opt-in smoke-test assertions; batching implementation is tracked by `GRAPHICS-018T`.
- Decide sampler feature policy for opt-in Vulkan smoke coverage, including anisotropy feature negotiation and maximum supported anisotropy clamping; sampler border-color API work is tracked by `GRAPHICS-018S`.

## Tests
- Documentation/link checks only when this clarification task is edited.

## Docs
- Update `docs/architecture/graphics.md`, `docs/architecture/rendering-three-pass.md`, or GPU verification docs with any finalized policy.

## Acceptance criteria
- Each question above is either resolved in docs or split into a concrete implementation task.
- Clarifications do not require Vulkan in the default CPU correctness gate.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not mix this docs-only clarification task with C++ behavior work.
- Do not make Vulkan/GPU tests mandatory in the default CPU gate.

