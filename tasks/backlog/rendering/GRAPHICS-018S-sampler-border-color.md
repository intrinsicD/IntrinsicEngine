# GRAPHICS-018S — Sampler border color RHI surface

## Goal

Add an API-neutral sampler border-color field and honor it in Vulkan sampler creation.

## Non-goals

- No Vulkan device/swapchain bring-up.
- No broad sampler feature-negotiation work beyond border color.
- No shader or material feature changes.

## Context

Owned by `graphics/rhi` and `graphics/vulkan`. `RHI::SamplerDesc` currently has `AddressMode::ClampToBorder` but no border-color selector, so `VulkanDevice::CreateSampler()` intentionally uses `VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK`. This is a nonblocking follow-up for `GRAPHICS-018`; timeline: complete before relying on non-black border colors in renderer/material behavior or Vulkan smoke tests.

## Required changes

- Add a backend-neutral border-color enum/field to `RHI::SamplerDesc`.
- Map the enum to Vulkan `VkBorderColor` in `Backends.Vulkan.Mappings` or the sampler creation path.
- Preserve the current opaque-black default for existing callers.
- Update source-grep/linkage guards that mention sampler creation.

## Tests

- Add RHI/Vulkan mapping contract tests for default opaque black and at least one alternate border color.
- Preserve default CPU gate compatibility; tests must not require a live Vulkan device.

## Docs

- Update `docs/architecture/graphics.md` if the sampler contract is described there.
- Update `src/graphics/vulkan/README.md` to remove the temporary hard-coded-border-color note.
- Update `GRAPHICS-018`/`GRAPHICS-026` task links if this lands while either is active.

## Acceptance criteria

- `RHI::SamplerDesc` can represent border color without exposing Vulkan types.
- Vulkan sampler creation honors the new field when operational.
- Existing sampler behavior remains unchanged by default.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests IntrinsicGraphicsRhiCpuUnitTests
ctest --test-dir build/ci --output-on-failure -R 'Sampler|RendererRhiBoundary' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes

- Do not add Vulkan types to RHI public headers/modules.
- Do not require Vulkan/GPU execution in the default CPU gate.
