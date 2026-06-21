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

## Required changes
- [ ] Extend `RHI.TransferQueue.cppm` with
      `DownloadTexture(TextureHandle src, TextureLayout srcLayout,
      std::uint32_t mipLevel, std::uint32_t arrayLayer, ReadbackSink sink)`
      returning a `ReadbackToken`; append after existing virtuals.
- [ ] Reuse `RHI::TextureUpload` subresource byte/offset math for the staged
      layout and the delivered span; reject unsupported/depth-stencil formats and
      out-of-range subresources fail-closed (invalid token + dropped counter).
- [ ] Null device / CPU mock fail-closed + drain delivery; Vulkan backend records
      the copy into the GRAPHICS-096 ring and copies out on `CollectCompleted()`.

## Tests
- [ ] CPU contract `tests/contract/graphics/Test.TextureReadback.cpp`
      (labels `contract;graphics`): Null fail-closed; mock drain delivers the
      correct subresource bytes once; bad format/subresource dropped with counter.
- [ ] Opt-in `gpu;vulkan` smoke
      `tests/integration/graphics/Test.TextureReadbackGpuSmoke.cpp`
      (labels `gpu;vulkan;graphics`): a known subresource round-trips to the CPU.
- [ ] Default CPU gate stays green.

## Docs
- [ ] Update `src/graphics/rhi/README.md`, `src/graphics/vulkan/README.md`, and
      the readback section of `docs/architecture/graphics.md`.
- [ ] Refresh `docs/api/generated/module_inventory.md`.
- [ ] Cross-link ADR-0023.

## Acceptance criteria
- [ ] `DownloadTexture` delivers correct subresource bytes through the sink on
      drain with no caller-thread fence wait; unsupported inputs fail closed.
- [ ] Default-gate contract tests pass; the opt-in `gpu;vulkan` smoke is cited as
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

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` everywhere else.
- Slice A closes `Scaffolded → CPUContracted`. `Operational` owned by `GRAPHICS-097`
  (this task's Slice B) via the cited `gpu;vulkan` smoke.
