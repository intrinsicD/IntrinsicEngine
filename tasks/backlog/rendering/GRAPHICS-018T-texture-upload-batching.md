# GRAPHICS-018T — Vulkan texture upload batching

## Goal

Replace the current synchronous one-subresource Vulkan `WriteTexture()` path with a batched policy for multi-mip, multi-layer, and cube texture uploads.

## Non-goals

- No swapchain/presentation bring-up.
- No renderer feature changes or new asset formats.
- No mandatory GPU/Vulkan tests in the default CPU gate.

## Context

Owned by `graphics/vulkan`, with API constraints from `graphics/rhi` and upload callers in `graphics/assets`. `GRAPHICS-026` tightened the current one-shot upload path to exact-size, sampled-only, color/depth-only constraints and documented that runtime uploads should use `ITransferQueue`. This batching task is nonblocking for fail-closed CPU correctness but should land before production texture streaming or Vulkan smoke coverage for mip chains/cubemaps. Timeline: complete before enabling multi-subresource Vulkan texture upload smoke tests.

## Required changes

- Define the RHI/backend policy for uploading multiple mips/layers/cube faces.
- Batch `VkBufferImageCopy` regions where possible instead of issuing one blocking one-shot submission per subresource.
- Track per-subresource layouts or document a whole-image layout policy.
- Route runtime/streaming uploads through `ITransferQueue` rather than blocking graphics-queue one-shots.
- Preserve deterministic diagnostics for unsupported depth-stencil or mismatched upload sizes.

## Tests

- Add CPU-testable validation/mapping tests for byte-size and subresource-region calculations.
- Add opt-in `gpu;vulkan` smoke tests for multi-mip/layer uploads when hardware support is available.
- Preserve the default CPU gate.

## Docs

- Update `src/graphics/vulkan/README.md` with the chosen batching/layout policy.
- Update `docs/architecture/graphics.md` if the upload contract becomes visible above the backend.
- Update `GRAPHICS-018Q` when the texture-upload policy question is resolved.

## Acceptance criteria

- Multi-mip/multi-layer texture uploads have an explicit, tested policy.
- Runtime uploads avoid queue-stalling one-shot helpers.
- Vulkan/GPU smoke tests remain opt-in with `gpu;vulkan` labels.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Optional when Vulkan hardware/driver support is available:
ctest --test-dir build/ci --output-on-failure -L 'gpu|vulkan' --timeout 120
```

## Forbidden changes

- Do not make Vulkan/GPU tests mandatory in the default CPU gate.
- Do not bypass RHI/transfer abstractions from renderer code.
