# GRAPHICS-018T — Vulkan texture upload batching

## Status
- State: in-progress.
- Owner/agent: local agent workflow.
- Activated: 2026-05-07 after `GRAPHICS-027` retirement cleared `tasks/active/`. GRAPHICS-018T is the next earliest unblocked rendering backlog task in the DAG: its upstream gates (`GRAPHICS-006`, `GRAPHICS-015`, `GRAPHICS-018`, `GRAPHICS-018Q`, `GRAPHICS-026`) are all retired in `tasks/done/`, and `GRAPHICS-018Q` explicitly named this task as the owner of multi-mip / multi-layer / cubemap batching plus opt-in `gpu;vulkan` smoke.
- Branch: `claude/setup-agentic-workflow-O9JFt`.
- Promotion commit: pending (this commit moves the file from `tasks/backlog/rendering/` to `tasks/active/`, redirects the rendering backlog README link, and updates the cross-references in `src/graphics/vulkan/README.md` and `src/graphics/renderer/README.md` from the backlog path to the active path).
- Implementation slice plan: split into independently committable slices.
  - Slice A.1 (next): expose the CPU-testable RHI texture-upload byte/region math (today buried as anonymous-namespace helpers in `src/graphics/vulkan/Backends.Vulkan.Device.cpp`) as a public `Extrinsic.RHI.TextureUpload` partition, add CPU contract tests for byte-size and packed full-mip-chain offset math, and document the whole-image layout policy decision from `GRAPHICS-018Q` in `src/graphics/vulkan/README.md`. Purely additive; does not touch Vulkan submission code or callers.
  - Slice A.2: rewrite the Vulkan backend's multi-mip / multi-layer 2D color upload path to coalesce `VkBufferImageCopy` regions in a single `VulkanTransferQueue` submission consuming the new `TextureUploadLayout`. Migrate the `Graphics.ColormapSystem.cpp` `WriteTexture()` caller to the transfer-queue path. Keep the guarded synchronous one-subresource `WriteTexture()` as the fail-closed baseline.
  - Slice B (deferred follow-up): cubemap (6-face) batching plus opt-in `gpu;vulkan` smoke tests for multi-mip / multi-layer / cubemap uploads.
- Implementation commits: Slice A.1 landed in commit `6073d70` (`GRAPHICS-018T: expose CPU-testable RHI texture-upload byte/region math`). Adds `Extrinsic.RHI.TextureUpload` with `BytesPerBlock`/`IsBlockCompressedFormat`/`IsDepthStencilFormat`/`BlockExtent`/`IsUploadableFormat`/`MipExtent`/`ComputeSubresourceUploadSize`/`ComputeFullChainUploadLayout` plus `TextureUploadSubresource`/`TextureUploadLayout`; pins the layer-major / mip-minor packing convention; rejects zero extents and depth-stencil / `Undefined` formats deterministically (`InvalidArgument` / `InvalidFormat`). Adds `tests/contract/graphics/Test.TextureUploadLayout.cpp` covering format helpers, mip extent clamping, per-subresource byte math (RGBA8 power-of-two and non-power-of-two chains, BC7 block rounding, out-of-range mip, unsupported format), and full-chain layout properties (single-layer chain, multi-layer ordering, cubemap, Tex3D, BC7 packed chain, rejection paths). Registers it under `_graphics_contract_test_files` so it lands in `IntrinsicGraphicsContractTests`. Updates `src/graphics/vulkan/README.md` to point at the new module as canonical CPU-testable upload math. Slice A.2 (next): rewrite the Vulkan multi-mip / multi-layer 2D color upload path through `VulkanTransferQueue::UploadTexture` to consume `TextureUploadLayout` and coalesce `VkBufferImageCopy` regions; migrate the `Graphics.ColormapSystem.cpp` `WriteTexture()` caller to the transfer-queue path. Slice B (deferred): cubemap (6-face) batching plus opt-in `gpu;vulkan` smoke for multi-mip / multi-layer / cubemap uploads.
- Task-state commit: pending retirement commit (will move the file from `tasks/active/` to `tasks/done/` once Slice A.1 + Slice A.2 are complete and Slice B has been promoted as a follow-up backlog task or scheduled separately).
- Next verification step: `python3 tools/agents/check_task_policy.py --root . --strict` and `python3 tools/docs/check_doc_links.py --root .` after the file move; the implementation slice runs `cmake --preset ci`, `cmake --build --preset ci --target IntrinsicGraphicsContractTests`, and the default CPU correctness gate `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` from `AGENTS.md` once the build is green.

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
