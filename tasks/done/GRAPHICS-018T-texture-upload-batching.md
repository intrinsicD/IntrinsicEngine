# GRAPHICS-018T — Vulkan texture upload batching

## Status
- State: done.
- Completed: 2026-05-07.
- Owner/agent: local agent workflow.
- Activated: 2026-05-07 after `GRAPHICS-027` retirement cleared `tasks/active/`. GRAPHICS-018T is the next earliest unblocked rendering backlog task in the DAG: its upstream gates (`GRAPHICS-006`, `GRAPHICS-015`, `GRAPHICS-018`, `GRAPHICS-018Q`, `GRAPHICS-026`) are all retired in `tasks/done/`, and `GRAPHICS-018Q` explicitly named this task as the owner of multi-mip / multi-layer / cubemap batching plus opt-in `gpu;vulkan` smoke.
- Branch: `main` (local branch inspected during this work; ahead of `origin/main` by the local `GRAPHICS-018T` implementation/docs/task commits).
- Promotion commit: landed previously (the task was active under `tasks/active/` before this retirement).
- Implementation slice plan: split into independently committable slices.
  - Slice A.1 (complete): expose the CPU-testable RHI texture-upload byte/region math (previously buried as anonymous-namespace helpers in `src/graphics/vulkan/Backends.Vulkan.Device.cpp`) as a public `Extrinsic.RHI.TextureUpload` partition, add CPU contract tests for byte-size and packed full-mip-chain offset math, and document the whole-image layout policy decision from `GRAPHICS-018Q` in `src/graphics/vulkan/README.md`. Purely additive; does not touch Vulkan submission code or callers.
  - Slice A.2 (complete): rewrite the Vulkan backend's multi-mip / multi-layer 2D color upload path to coalesce `VkBufferImageCopy` regions in a single `VulkanTransferQueue` submission consuming the new `TextureUploadLayout`. Migrate the `Graphics.ColormapSystem.cpp` `WriteTexture()` caller to the transfer-queue path. Keep the guarded synchronous one-subresource `WriteTexture()` as the fail-closed baseline.
    - Slice A.2a (complete in this slice): extend `RHI::ITransferQueue` with `UploadTextureFullChain(TextureHandle, std::span<const std::byte>)`, implement the Null backend and Vulkan fallback/live transfer queue as deterministic fail-closed invalid-token paths, and add CPU contract coverage for the Null and Vulkan fallback behavior. Pure RHI surface; no Vulkan submission code yet.
    - Slice A.2b (complete in this slice): implement the Vulkan backend submission path for 2D color texture arrays (one staging belt allocation, one whole-image `Undefined`/current-layout→`TransferDst` barrier, one `vkCmdCopyBufferToImage` with an array of `VkBufferImageCopy` regions built from `TextureUploadLayout`, one `TransferDst`→`ShaderReadOnly` barrier, one timeline-semaphore submit). Validate against the layout total-byte count and reject mismatches deterministically with the same diagnostic style as the existing single-subresource `UploadTexture()`. Cubemaps/3D remain deferred.
    - Slice A.2c (complete in this slice): migrate `Graphics.ColormapSystem.cpp` from `device.WriteTexture()` to `device.GetTransferQueue().UploadTexture()`, track the returned `TransferToken` values, and expose `ColormapSystem::IsReady()` so colormap-dependent draws can branch on first-frame readiness. `GetBindlessIndex()` now returns `kInvalidBindlessIndex` until every LUT upload token is valid and complete. This uses the preferred async readiness option from the A.1 clarification and avoids startup-time fence draining.
    - Slice A.2d (complete in this slice): unblock Slice B by exposing the live Vulkan transfer queue once guarded live prerequisites are ready while keeping `IDevice::IsOperational()` and public bindless access fail-closed; add opt-in `gpu;vulkan` smoke coverage for a 2D multi-mip `UploadTextureFullChain()` submission through the public transfer seam; defer submitted transfer command-buffer reclamation until timeline completion.
  - Slice B (complete in this slice): cubemap (6-face) batching plus opt-in `gpu;vulkan` smoke tests for multi-mip / multi-layer / cubemap uploads.
- Implementation commits: Slice A.1 landed in commit `6073d70` (`GRAPHICS-018T: expose CPU-testable RHI texture-upload byte/region math`). Adds `Extrinsic.RHI.TextureUpload` with `BytesPerBlock`/`IsBlockCompressedFormat`/`IsDepthStencilFormat`/`BlockExtent`/`IsUploadableFormat`/`MipExtent`/`ComputeSubresourceUploadSize`/`ComputeFullChainUploadLayout` plus `TextureUploadSubresource`/`TextureUploadLayout`; pins the layer-major / mip-minor packing convention; rejects zero extents and depth-stencil / `Undefined` formats deterministically (`InvalidArgument` / `InvalidFormat`). Adds `tests/contract/graphics/Test.TextureUploadLayout.cpp` covering format helpers, mip extent clamping, per-subresource byte math (RGBA8 power-of-two and non-power-of-two chains, BC7 block rounding, out-of-range mip, unsupported format), and full-chain layout properties (single-layer chain, multi-layer ordering, cubemap, Tex3D, BC7 packed chain, rejection paths). Registers it under `_graphics_contract_test_files` so it lands in `IntrinsicGraphicsContractTests`. Updates `src/graphics/vulkan/README.md` to point at the new module as canonical CPU-testable upload math. Followup fixup commit: align per-subresource offsets to `RequiredBufferOffsetAlignment(Format)` = `max(4, BytesPerBlock(Format))` so emitted offsets directly satisfy `VUID-VkBufferImageCopy-bufferOffset-00193` (multiple of element/block size) AND `VUID-vkCmdCopyBufferToImage-commandBuffer-07737` (multiple of 4 on transfer-only queues, which the Vulkan backend prefers in `Backends.Vulkan.Device.cpp`); without this padding a 2-layer 2x2 R8 texture would land layer 1 mip 0 at offset 5 and trip the transfer-queue VUID. Adds `RequiredBufferOffsetAlignment(Format)` helper and CPU contract tests for R8 multi-layer (the regression case), RG8 odd-mip padding, RGB32_FLOAT non-power-of-two alignment (12-byte LCM with the 4-byte floor), and BC7 16-byte alignment. Slice A.2a adds `ITransferQueue::UploadTextureFullChain(TextureHandle, std::span<const std::byte>)` plus Null/Vulkan fail-closed implementations and CPU contract coverage. Slice A.2b landed in commit `967fe50` (`GRAPHICS-018T: batch Vulkan full-chain texture uploads`): implements the live Vulkan 2D color texture-array batching path through `VulkanTransferQueue::UploadTextureFullChain`, consumes `TextureUploadLayout`, fixes staging-belt non-power-of-two alignment for `RGB32_FLOAT`, copies only initialized subresource byte ranges, and coalesces one `VkBufferImageCopy` array in a timeline-semaphore transfer submission. Slice A.2c landed in commit `87da4cd` (`GRAPHICS-018T: route colormap LUT uploads through transfer queue`): migrates `Graphics.ColormapSystem.cpp` to `ITransferQueue::UploadTexture()`, adds `ColormapSystem::IsReady()`, gates bindless indices until LUT upload tokens complete, and adds CPU renderer-unit coverage. Slice A.2d landed in commit `351abbe` (`GRAPHICS-018T: unblock Vulkan full-chain smoke`): it exposes service-ready public transfer queue access, adds 2D full-chain Vulkan smoke coverage, and defers transfer command-buffer reclamation to timeline completion. Slice B lands in this implementation commit: it admits `TexCube` to the live Vulkan full-chain transfer path as a six-layer cube-compatible 2D image, keeps 3D uploads fail-closed/deferred, and expands opt-in `gpu;vulkan` smoke coverage to submit 2D single-layer, 2D array, and cubemap multi-mip full-chain uploads through the public transfer seam.
- Task-state commit: this retirement commit (moves the file from `tasks/active/` to `tasks/done/` after Slice A.1, A.2a/b/c/d, and Slice B are implemented, verified, and documented).
- Latest verification: Slice B was verified on 2026-05-07 with the `ci` preset reconfigured to the available Clang 22 toolchain (`/usr/bin/clang`, `/usr/bin/clang++`), `INTRINSIC_OFFLINE_DEPS=ON`, and explicit `EXTRINSIC_BACKEND=Vulkan` because the exact preset still names unavailable `clang-20`/`clang++-20` aliases and the Vulkan backend target must be selected before `src/graphics/renderer/Backends` is processed. Commands run: exact `cmake --preset ci` (failed because `clang-20`/`clang++-20` are unavailable in `PATH`), Clang 22 Vulkan reconfigure (passed), focused `IntrinsicGraphicsContractTests` + `IntrinsicGraphicsVulkanContractTests` + `IntrinsicGraphicsVulkanSmokeTests` build, `TextureUploadLayout.*:TransferQueueFullChain*` (`9/9` passed), `VulkanFailClosedContract.*` (`18/18` passed), direct `VulkanBootstrapSmoke.InitializeCreatesPerFrameResourcesOrFailsCleanly` (`1/1` passed on this GPU/window host and exercised 2D, 2D-array, and cubemap full-chain uploads), focused CTest `ctest -R 'VulkanBootstrapSmoke|VulkanFailClosedContract|TextureUploadLayout|TransferQueueFullChain'` (`28/28` passed), `IntrinsicTests` build, and default CPU CTest gate (`1625/1625` passed).
- Next implementation step: none for `GRAPHICS-018T`; multi-mip, multi-layer, and cubemap batching has an explicit tested policy and remains opt-in for Vulkan/GPU smoke.

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
