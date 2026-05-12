# GRAPHICS-033C — Vulkan command-recording for `FrameRecipe::MinimalDebugSurface`

## Goal
- Implement Vulkan command-recording bodies for the GRAPHICS-032 minimal-debug-surface recipe so that, on hosts with a Vulkan-capable surface and the GRAPHICS-018R operational-transition reset seam available, `Pass.Surface.MinimalDebug` and `Pass.Present.MinimalDebug` route to real `VulkanCommandContext` calls instead of soft-skipping. Once the GRAPHICS-033 nine-step gate is satisfied for these recording paths, `EvaluateVulkanOperationalStatus(...)` returns `Operational` and `VulkanDevice::IsOperational()` flips to `true`.

> **Scaffold notice.** The two minimal-recipe executor routes wired by this task are removed by [`GRAPHICS-081`](GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md) once the default-recipe equivalents (`GRAPHICS-070`/`076`) are operational. The Vulkan operational gate, the `EvaluateVulkanOperationalStatus` evaluator (`GRAPHICS-033A`), and the operational diagnostics (`GRAPHICS-033B`) all stay — they are canonical. Per `GRAPHICS-081`, if the `MinimalRecipeRecordingMissing` reason is referenced by the `VulkanOperationalReason` taxonomy at retirement time, it is renamed (default-recipe-recording absence reason) rather than deleted to keep that enum append-only.

## Non-goals
- No `gpu;vulkan` smoke fixture (`GRAPHICS-033D`).
- No additional pass bodies beyond the minimal recipe (those are the per-pass tasks under the Phase-2 backlog, e.g. `GRAPHICS-070..076`).
- No mutation of the validation-layer policy (already locked by `GRAPHICS-018Q`).
- No swapchain or surface lifecycle change (`GRAPHICS-018` already brought up acquire/present timing).

## Context
- Status: not started.
- Owner/layer: `graphics/vulkan` (command bodies, descriptor flow), `graphics/renderer` (executor route reuse).
- Planning parent: [`tasks/done/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md`](../../done/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md), Recorded as Impl-C in the parent's Required changes.
- Upstream gates: `GRAPHICS-032A`/`B`/`C` (recipe + CPU-mock pass bodies must exist), `GRAPHICS-031A`/`B` (slot-0 pipeline + substitution), `GRAPHICS-033A`/`B` (status seam + diagnostics).
- The barrier translation already lives in `VulkanCommandContext::TextureBarrier`/`SubmitBarriers`; this task wires the recipe's barrier packets through that translation.

## Required changes
- [ ] Validate that `MinimalDebugSurfacePass::Execute` and `MinimalDebugPresentPass::Execute` issue command-context calls supported by `VulkanCommandContext` post-bind (`BindPipeline`, `BindIndexBuffer`, `PushConstants`, `DrawIndexedIndirectCount`, `Draw`, `BeginRenderPass`/`EndRenderPass` via dynamic rendering helpers).
- [ ] Ensure the renderer's executor lambda passes the live `VulkanCommandContext` (acquired via `IDevice::GetGraphicsContext(frameIndex)`) to the minimal-recipe pass routes; remove the `SkippedNonOperational`/`SkippedUnavailable` early-return for these passes when the device is operational.
- [ ] Wire the GRAPHICS-018R `RebuildOperationalResources()` path so that on the false→true operational transition, the slot-0 default-debug-surface pipeline (GRAPHICS-031A), depth prepass pipeline, present pipeline, and `GpuWorld` buffers all rebuild byte-identical.
- [ ] Add CPU command-sequence parity tests asserting that the Vulkan command stream for one minimal-recipe frame matches the property-based CPU-mock command stream from `GRAPHICS-032C` (same opcodes, same bind targets, same draw counts) — the parity is asserted against a recorded backend-trace, not against `VkCommandBuffer` directly.
- [ ] Keep real-device execution opt-in: only run as `gpu;vulkan` smoke (which lands in `GRAPHICS-033D`); the parity test in this slice runs against a CPU-recordable Vulkan trace.

## Tests
- [ ] `contract;graphics` parity test: minimal recipe command stream is identical between the CPU-mock executor (GRAPHICS-032C) and the Vulkan-recorded trace (this slice). Same bind/draw shape, same barrier packet sequence.
- [ ] `contract;runtime` test: with `EnablePromotedVulkanDevice = true` on a host without Vulkan support, the device falls back to Null and `VulkanFallbackToNullCount` increments by 1 (no regression of `GRAPHICS-033B`).
- [ ] `contract;graphics` test: `EvaluateVulkanOperationalStatus(BuildOperationalInputs())` returns `Operational` only when all 9 gate steps succeed including `MinimalRecipeRecordingMissing == false`.
- [ ] No `gpu;vulkan` test in this slice (deferred to `GRAPHICS-033D`).

## Docs
- [ ] Update `src/graphics/vulkan/README.md` to record that `Pass.Surface.MinimalDebug` and `Pass.Present.MinimalDebug` recording bodies execute against the live command context once this task lands.
- [ ] Update `docs/architecture/graphics.md` "operational-transition ownership" section if call ordering changes.

## Acceptance criteria
- [ ] On a Vulkan-capable host with promoted Vulkan enabled, the minimal recipe routes through the real command context end-to-end (CPU verification only at this stage).
- [ ] `IsOperational()` returns `true` once and only once when the 9-step gate passes; flipping any gate input back to `false` returns `IsOperational()` to `false`.
- [ ] No regression of fail-closed behavior on hosts without Vulkan support.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target ExtrinsicBackendsVulkanContractTests IntrinsicGraphicsContractTests IntrinsicRuntimeTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding a `gpu;vulkan` test (reserved for `GRAPHICS-033D`).
- Wiring additional pass bodies beyond the minimal recipe.
- Relaxing the validation-layer policy.
- Mutating the swapchain/surface lifecycle.

## Next verification step
- Wire the executor branches against the live command context, add the parity tests, run the verification commands above.
