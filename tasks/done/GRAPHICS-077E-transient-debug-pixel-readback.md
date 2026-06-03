# GRAPHICS-077E — Transient-debug pixel-readback parity harness

- Status: completed (2026-06-03; `CPUContracted` by default, `Operational` on Vulkan-capable hosts when the opt-in smoke passes).
- Owner / agent: `graphics/renderer` test seam plus opt-in `gpu;vulkan;graphics` coverage.
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired.

## Goal
- Add an opt-in Vulkan pixel-readback harness proving the `TransientDebugSurfacePass` triangle, line, and point lanes produce expected sample colors on the swapchain/backbuffer, beyond the command-stream/counter proof from `GRAPHICS-077` Slice D.

## Non-goals
- No new transient-debug pass bodies or lane semantics; `GRAPHICS-077` owns command-stream operational proof.
- No routing through retained line/point cull buckets.
- No `GpuWorld` retention for transient debug buffers.
- No visualization-overlay assertions; `GRAPHICS-078E` owns that sibling path.

## Context
- Owner/layer: `graphics/renderer` test seam plus opt-in `gpu;vulkan;graphics` coverage.
- `GRAPHICS-077` Slice D added `TransientDebugSurfaceGpuSmoke.MixedLanesRecordOnOperationalVulkanCommandStream`, which warms the default recipe, manually submits one triangle/line/point packet, drives a real Vulkan frame, and asserts the pass records with non-zero per-lane counters.
- The existing default-recipe readback seam is `IRenderer::SetDefaultRecipeBackbufferReadbackBuffer(...)`, intentionally scoped to the canonical surface/present lane. This follow-up adds a transient-debug-specific readback hook/counter instead of reusing that canonical surface counter.

## Implemented shape
- `RenderGraphFrameStats::TransientDebugBackbufferReadbackCopyCount` records only transient-debug readback copies.
- `IRenderer::SetTransientDebugBackbufferReadbackBuffer(...)` / `GetTransientDebugBackbufferReadbackBuffer()` arms a caller-owned HostVisible+TransferDst buffer. Invalid handle disables the path.
- The renderer records the same `Present -> TransferSrc -> CopyTextureToBuffer -> Present` backbuffer triplet as the canonical default-recipe readback hook, but only when the device is operational, a valid transient-debug readback buffer is armed, and `TransientDebugSurfacePass` recorded in `CommandRecords.Passes` for the same frame.
- CPU/null tests prove default-disabled, pass-omitted, and non-operational fail-closed behavior, and prove the transient counter remains distinct from `DefaultRecipeBackbufferReadbackCopyCount`.
- The opt-in Vulkan smoke adds `TransientDebugSurfaceGpuSmoke.MixedLanesReadBackExpectedSampleColors`, which samples deterministic red triangle, green line, blue point, and clear pixels from the readback buffer.

## Required changes
- [x] Add a narrow default-recipe or pass-scoped readback hook/counter suitable for transient-debug assertions without reusing the canonical surface-readback counter.
- [x] Keep the hook fail-closed when no buffer is armed and when the device is non-operational.
- [x] Preserve normal renderer behavior when the hook is not armed.
- [x] Use deterministic sample points/colors that isolate at least one triangle, one line, and one point contribution.

## Tests
- [x] Add `contract;graphics` coverage for the new readback hook/counter fail-closed behavior.
- [x] Add or extend an opt-in `gpu;vulkan;graphics` smoke that reads back the frame and checks transient-debug sample colors.
- [x] Preserve `TransientDebugSurfaceGpuSmoke.MixedLanesRecordOnOperationalVulkanCommandStream` as the command-stream/counter proof.

## Docs
- [x] Update `src/graphics/renderer/README.md` and `src/graphics/vulkan/README.md` with the new transient-debug readback hook/counter.
- [x] Update `tasks/backlog/rendering/README.md` because this task is retired and `GRAPHICS-078E` remains the next sibling readback follow-up.

## Acceptance criteria
- [x] Vulkan readback confirms expected transient-debug triangle/line/point sample colors on Vulkan-capable hosts through the opt-in smoke.
- [x] Counters distinguish command recording from readback copy recording.
- [x] CPU/null tests prove fail-closed behavior without requiring Vulkan.
- [x] The opt-in readback smoke remains outside the default CPU gate.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'TransientDebugSurfacePass' -L 'contract' -L 'graphics' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicGraphicsVulkanSmokeTests
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests
LSAN_OPTIONS=suppressions=$PWD/lsan.supp ctest --test-dir build/ci-vulkan --output-on-failure -R 'TransientDebugSurfaceGpuSmoke' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
git --no-pager diff --check
```

## Forbidden changes
- Weakening the existing transient-debug command-stream smoke.
- Reusing the canonical surface-readback counter as evidence for default-recipe transient-debug pixels.
- Mixing mechanical moves with semantic refactors.

## Maturity
- Target: `Operational` on Vulkan-capable hosts for transient-debug pixel-readback parity; `CPUContracted` everywhere else.
- Completed as `CPUContracted` through CPU/null contract tests and as `Operational` on hosts where the opt-in `TransientDebugSurfaceGpuSmoke` readback fixture passes.
