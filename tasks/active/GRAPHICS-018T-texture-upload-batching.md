# GRAPHICS-018T — Vulkan texture upload batching

## Status
- State: in-progress.
- Owner/agent: local agent workflow.
- Activated: 2026-05-07 after `GRAPHICS-027` retirement cleared `tasks/active/`. GRAPHICS-018T is the next earliest unblocked rendering backlog task in the DAG: its upstream gates (`GRAPHICS-006`, `GRAPHICS-015`, `GRAPHICS-018`, `GRAPHICS-018Q`, `GRAPHICS-026`) are all retired in `tasks/done/`, and `GRAPHICS-018Q` explicitly named this task as the owner of multi-mip / multi-layer / cubemap batching plus opt-in `gpu;vulkan` smoke.
- Branch: `claude/setup-agentic-workflow-O9JFt`.
- Promotion commit: pending (this commit moves the file from `tasks/backlog/rendering/` to `tasks/active/`, redirects the rendering backlog README link, and updates the cross-references in `src/graphics/vulkan/README.md` and `src/graphics/renderer/README.md` from the backlog path to the active path).
- Implementation slice plan: split into independently committable slices.
  - Slice A.1 (next): expose the CPU-testable RHI texture-upload byte/region math (today buried as anonymous-namespace helpers in `src/graphics/vulkan/Backends.Vulkan.Device.cpp`) as a public `Extrinsic.RHI.TextureUpload` partition, add CPU contract tests for byte-size and packed full-mip-chain offset math, and document the whole-image layout policy decision from `GRAPHICS-018Q` in `src/graphics/vulkan/README.md`. Purely additive; does not touch Vulkan submission code or callers.
  - Slice A.2 (deferred to a clang-20 + GPU host): rewrite the Vulkan backend's multi-mip / multi-layer 2D color upload path to coalesce `VkBufferImageCopy` regions in a single `VulkanTransferQueue` submission consuming the new `TextureUploadLayout`. Migrate the `Graphics.ColormapSystem.cpp` `WriteTexture()` caller to the transfer-queue path. Keep the guarded synchronous one-subresource `WriteTexture()` as the fail-closed baseline. Recommended sub-split for the next session that has clang-20 + GPU available:
    - Slice A.2a: extend `RHI::ITransferQueue` with a multi-subresource entry point (e.g. `UploadTextureFullChain(TextureHandle, std::span<const std::byte>)`), implement the Null backend (no-op returning an invalid token), and add a Null-backend CPU contract test verifying the fail-closed path. Pure RHI surface; no Vulkan submission code yet.
    - Slice A.2b: implement the Vulkan backend submission path (one staging belt allocation, one whole-image `Undefined`→`TransferDst` barrier, one `vkCmdCopyBufferToImage` with an array of `VkBufferImageCopy` regions built from `TextureUploadLayout`, one `TransferDst`→`ShaderReadOnly` barrier, one timeline-semaphore submit). Validate against the layout total-byte count and reject mismatches deterministically with the same diagnostic style as the existing single-subresource `UploadTexture()`.
    - Slice A.2c: migrate `Graphics.ColormapSystem.cpp` from `device.WriteTexture()` to `device.GetTransferQueue().UploadTexture()`. **Non-blocking clarification (added by slice A.1 author):** `device.WriteTexture()` is synchronous (the colormap LUT is sampleable as soon as `Initialize()` returns), but `ITransferQueue::UploadTexture()` is asynchronous (the staging copy completes on a later `CollectCompleted()` cycle on the render thread). Two robust sub-options without blocking the slice: (1) track the returned `TransferToken` in `ColormapSystem::Impl` and expose `IsReady()` so colormap-dependent draws can branch on readiness for the first frame or two, mirroring `GpuAssetCache::Slot::InFlight` / `GpuAssetState::GpuUploading` (preferred — matches the existing async-asset pattern); or (2) drain the transfer queue once after `Initialize()` via a single `CollectCompleted()` pump plus a fence wait, accepting one extra startup-time stall but preserving the current "sampleable immediately" invariant. Pick (1) unless the renderer's first-frame contract truly requires synchronous availability.
  - Slice B (deferred follow-up): cubemap (6-face) batching plus opt-in `gpu;vulkan` smoke tests for multi-mip / multi-layer / cubemap uploads.
- Implementation commits: Slice A.1 landed in commit `6073d70` (`GRAPHICS-018T: expose CPU-testable RHI texture-upload byte/region math`). Adds `Extrinsic.RHI.TextureUpload` with `BytesPerBlock`/`IsBlockCompressedFormat`/`IsDepthStencilFormat`/`BlockExtent`/`IsUploadableFormat`/`MipExtent`/`ComputeSubresourceUploadSize`/`ComputeFullChainUploadLayout` plus `TextureUploadSubresource`/`TextureUploadLayout`; pins the layer-major / mip-minor packing convention; rejects zero extents and depth-stencil / `Undefined` formats deterministically (`InvalidArgument` / `InvalidFormat`). Adds `tests/contract/graphics/Test.TextureUploadLayout.cpp` covering format helpers, mip extent clamping, per-subresource byte math (RGBA8 power-of-two and non-power-of-two chains, BC7 block rounding, out-of-range mip, unsupported format), and full-chain layout properties (single-layer chain, multi-layer ordering, cubemap, Tex3D, BC7 packed chain, rejection paths). Registers it under `_graphics_contract_test_files` so it lands in `IntrinsicGraphicsContractTests`. Updates `src/graphics/vulkan/README.md` to point at the new module as canonical CPU-testable upload math. Followup fixup commit: align per-subresource offsets to `RequiredBufferOffsetAlignment(Format)` = `max(4, BytesPerBlock(Format))` so emitted offsets directly satisfy `VUID-VkBufferImageCopy-bufferOffset-00193` (multiple of element/block size) AND `VUID-vkCmdCopyBufferToImage-commandBuffer-07737` (multiple of 4 on transfer-only queues, which the Vulkan backend prefers in `Backends.Vulkan.Device.cpp`); without this padding a 2-layer 2x2 R8 texture would land layer 1 mip 0 at offset 5 and trip the transfer-queue VUID. Adds `RequiredBufferOffsetAlignment(Format)` helper and CPU contract tests for R8 multi-layer (the regression case), RG8 odd-mip padding, RGB32_FLOAT non-power-of-two alignment (12-byte LCM with the 4-byte floor), and BC7 16-byte alignment. Slice A.2 (next): rewrite the Vulkan multi-mip / multi-layer 2D color upload path through `VulkanTransferQueue::UploadTexture` to consume `TextureUploadLayout` and coalesce `VkBufferImageCopy` regions; migrate the `Graphics.ColormapSystem.cpp` `WriteTexture()` caller to the transfer-queue path. Slice B (deferred): cubemap (6-face) batching plus opt-in `gpu;vulkan` smoke for multi-mip / multi-layer / cubemap uploads.
- Task-state commit: pending retirement commit (will move the file from `tasks/active/` to `tasks/done/` once Slice A.1 + Slice A.2a/b/c are complete on a clang-20 + GPU host and Slice B has been promoted as a follow-up backlog task or scheduled separately).
- Next verification step: on a clang-20 + GPU host, run `cmake --preset ci`, `cmake --build --preset ci --target IntrinsicGraphicsContractTests`, and the default CPU correctness gate `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` from `AGENTS.md` to confirm the slice A.1 module + tests build and run green before starting slice A.2a. Slice A.1 was inspected-only in the original session because that environment provided clang-18 / g++-13 only and the `ci` preset hard-pins `clang++-20`; structural gates (`check_task_policy --strict`, `check_doc_links`, `check_layering --strict`, `check_test_layout --strict`) all passed.

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
