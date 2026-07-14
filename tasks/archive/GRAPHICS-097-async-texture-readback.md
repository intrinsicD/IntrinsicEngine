---
id: GRAPHICS-097
theme: B
depends_on: [GRAPHICS-096]
maturity_target: Operational
---
# GRAPHICS-097 — Async GPU→CPU texture readback through the readback ring

## Goal
- Add `RHI::ITransferQueue::DownloadTexture(...)` that stages a
  `CopyTextureToBuffer` into the GRAPHICS-096 host-visible readback ring and
  delivers the subresource bytes on `CollectCompleted()`, reusing
  `RHI::TextureUpload`'s packed-subresource layout in reverse, so GPU-produced
  images (bakes, computed fields, render targets) can be read to the CPU without
  a device stall.

## Non-goals
- No new readback transport (reuses the GRAPHICS-096 ring and drain).
- No depth-stencil readback (the upload contract already excludes depth-stencil;
  same exclusion applies here).
- No automatic source-layout transition: the caller transitions the source into
  `TransferSrc` and back, exactly as `ICommandContext::CopyTextureToBuffer`
  already requires.

## Context
- Owning subsystem/layer: `src/graphics/rhi/` + `src/graphics/vulkan/`.
- `ICommandContext::CopyTextureToBuffer(...)` already exists (used by the
  GRAPHICS-076E pixel-readback parity harness and GRAPHICS-074 single-pixel
  picking readback) and copies a mip/layer subresource extent into a
  `TransferDst` buffer (`src/graphics/rhi/RHI.CommandContext.cppm:287-296`).
- `RHI::TextureUpload` already computes the packed, alignment-correct
  per-subresource byte layout (`ComputeFullChainUploadLayout`,
  `ComputeSubresourceUploadSize`, `RequiredBufferOffsetAlignment`); the same math
  describes how staged readback bytes are laid out for delivery.
- Builds on GRAPHICS-096's `ReadbackToken` / `ReadbackSink` / ring + drain.

## Slice plan
- **Slice A (CPU contract).** Add `DownloadTexture(...)` surface + layout reuse +
  Null fail-closed + CPU-mock drain delivery; validate format/subresource
  selection fail-closed. Preserves the CPU gate.
- **Slice B (Vulkan + smoke).** Record the device→host `CopyTextureToBuffer` into
  the readback ring and copy out on drain; opt-in `gpu;vulkan` smoke round-trips
  a rendered/computed texture subresource to the CPU.

## Execution plan
- Keep `ITransferQueue` append-only: add `DownloadTexture(...)` after the
  existing readback virtuals with a default fail-closed body so existing
  consumers keep their upload/poll/drain slots stable.
- Reuse `RHI::TextureUpload` math for size and subresource extents. This slice
  accepts color `Tex2D` and `TexCube` subresources, rejects depth-stencil,
  undefined/unsupported formats, out-of-range mips/layers, `Tex3D`, invalid
  sinks, and source textures without `TransferSrc`.
- Reuse the GRAPHICS-096 readback ring as the transport. Vulkan allocates a
  readback slot sized to the single subresource byte count, records
  `vkCmdCopyImageToBuffer` at buffer offset 0, submits through the existing
  readback timeline path, and delivers the bytes on `CollectCompleted()`.
- Do not auto-transition source layouts. `DownloadTexture(...)` requires
  `TextureLayout::TransferSrc`, records only a read visibility barrier with no
  layout transition, and leaves the caller responsible for any before/after
  layout ownership.
- Mirror the buffer readback verification shape: CPU contract tests cover Null,
  mock delivery, and fail-closed invalid inputs; the opt-in Vulkan smoke uploads
  a known texture through the transfer queue, transitions it to `TransferSrc`,
  then downloads and verifies the bytes through the ring without `WaitIdle`.

## Required changes
- [x] Extend `RHI.TransferQueue.cppm` with
      `DownloadTexture(TextureHandle src, TextureLayout srcLayout,
      std::uint32_t mipLevel, std::uint32_t arrayLayer, ReadbackSink sink)`
      returning a `ReadbackToken`; append after existing virtuals.
- [x] Reuse `RHI::TextureUpload` subresource byte/offset math for the staged
      layout and the delivered span; reject unsupported/depth-stencil formats and
      out-of-range subresources fail-closed (invalid token + dropped counter).
- [x] Null device / CPU mock fail-closed + drain delivery; Vulkan backend records
      the copy into the GRAPHICS-096 ring and copies out on `CollectCompleted()`.

## Tests
- [x] CPU contract `tests/contract/graphics/Test.TextureReadback.cpp`
      (labels `contract;graphics`): Null fail-closed; mock drain delivers the
      correct subresource bytes once; bad format/subresource dropped with counter.
- [x] Opt-in `gpu;vulkan` smoke
      `tests/integration/graphics/Test.TextureReadbackGpuSmoke.cpp`
      (labels `gpu;vulkan;graphics`): a known subresource round-trips to the CPU.
- [x] Default CPU gate stays green.

## Docs
- [x] Update `src/graphics/rhi/README.md`, `src/graphics/vulkan/README.md`, and
      the readback section of `docs/architecture/graphics.md`.
- [x] Refresh `docs/api/generated/module_inventory.md`.
- [x] Cross-link ADR-0023.

## Acceptance criteria
- [x] `DownloadTexture` delivers correct subresource bytes through the sink on
      drain with no caller-thread fence wait; unsupported inputs fail closed.
- [x] Default-gate contract tests pass; the opt-in `gpu;vulkan` smoke is cited as
      run for `Operational`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -- -j16
ctest --test-dir build/ci --output-on-failure -R 'TextureReadback' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Operational (Vulkan-capable host):
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'TextureReadbackGpuSmoke' -L 'gpu' -L 'vulkan' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Blocking a caller thread on a GPU fence.
- Auto-transitioning the source texture layout (caller owns the transition).
- Adding ECS/runtime/asset knowledge to `graphics/rhi` or `graphics/vulkan`.

## Completion notes
- PR/commit: this retirement commit.
- Completed on 2026-06-22 at maturity `Operational` on the local
  Vulkan-capable host.
- Implemented `ITransferQueue::DownloadTexture(...)` as an append-only
  non-blocking RHI readback virtual, with Null and non-operational Vulkan
  fallback queues failing closed.
- Live Vulkan reuses the GRAPHICS-096 readback ring, validates color `Tex2D`
  arrays / cubemaps through `RHI.TextureUpload`, records a no-transition
  `vkCmdCopyImageToBuffer`, and delivers bytes from `CollectCompleted()`.
- Focused evidence:
  `cmake --build --preset ci --target IntrinsicGraphicsContractTests -j 16`,
  `ctest --test-dir build/ci --output-on-failure -R 'TextureReadback' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`,
  `cmake --build --preset ci --target IntrinsicGraphicsVulkanSmokeTests -j 16`,
  `ctest --test-dir build/ci --output-on-failure -R 'TextureReadbackGpuSmoke' -L 'gpu' -L 'vulkan' --timeout 120`,
  `cmake --preset ci-vulkan`,
  `cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests -j 16`,
  and
  `ctest --test-dir build/ci-vulkan --output-on-failure -R 'TextureReadbackGpuSmoke' -L 'gpu' -L 'vulkan' --timeout 120`.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` everywhere else.
- Slice A closes `Scaffolded → CPUContracted`. `Operational` owned by `GRAPHICS-097`
  (this task's Slice B) via the cited `gpu;vulkan` smoke.
