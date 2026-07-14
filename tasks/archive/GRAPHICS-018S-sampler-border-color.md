# GRAPHICS-018S — Sampler border color RHI surface

## Status

- State: done.
- Owner/agent: local agent workflow.
- Commit / PR: pending local agent workflow handoff.
- Completed: 2026-05-04.
- Completion note: added backend-neutral `RHI::SamplerBorderColor` / `SamplerDesc::BorderColor`, preserved the opaque-black default, included the field in sampler-manager deduplication, and routed Vulkan sampler creation through `ToVkBorderColor(desc.BorderColor)` with CPU-testable unit/contract coverage.

## Goal

Add an API-neutral sampler border-color field and honor it in Vulkan sampler creation.

## Non-goals

- No Vulkan device/swapchain bring-up.
- No broad sampler feature-negotiation work beyond border color.
- No shader or material feature changes.

## Context

Owned by `graphics/rhi` and `graphics/vulkan`. Before this task, `RHI::SamplerDesc` had `AddressMode::ClampToBorder` but no border-color selector, so `VulkanDevice::CreateSampler()` intentionally used `VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK`. This follow-up completed before renderer/material behavior or Vulkan smoke tests rely on non-black border colors.

## Required changes

- [x] Add a backend-neutral border-color enum/field to `RHI::SamplerDesc`.
- [x] Map the enum to Vulkan `VkBorderColor` in `Backends.Vulkan.Mappings` or the sampler creation path.
- [x] Preserve the current opaque-black default for existing callers.
- [x] Update source-grep/linkage guards that mention sampler creation.
- [x] Done 2026-05-04: all required changes landed; no Vulkan types were exposed through RHI.

## Tests

- [x] Add RHI/Vulkan mapping contract tests for default opaque black and at least one alternate border color.
- [x] Preserve default CPU gate compatibility; tests must not require a live Vulkan device.
- [x] Done 2026-05-04: `RHISamplerManager.DefaultSamplerBorderColorPreservesOpaqueBlack` and `RHISamplerManager.BorderColorParticipatesInDedupKey` cover the RHI default and dedup key; `RendererRhiBoundary.SamplerBorderColorStaysBackendNeutralAndMapsInVulkanBackend` guards the backend-neutral RHI surface and Vulkan mapping/use path without GPU execution.

## Docs

- [x] Update `docs/architecture/graphics.md` if the sampler contract is described there.
- [x] Update `src/graphics/vulkan/README.md` to remove the temporary hard-coded-border-color note.
- [x] Update `GRAPHICS-018`/`GRAPHICS-026` task links if this lands while either is active.
- [x] Done 2026-05-04: updated `docs/architecture/graphics.md`, `src/graphics/vulkan/README.md`, active `GRAPHICS-018`, and `GRAPHICS-018Q`; `GRAPHICS-026` was already done and did not require active-task link changes.

## Acceptance criteria

- [x] `RHI::SamplerDesc` can represent border color without exposing Vulkan types.
- [x] Vulkan sampler creation honors the new field when operational.
- [x] Existing sampler behavior remains unchanged by default.

## Verification

Final evidence (2026-05-04):

- `cmake --preset ci`
- `cmake --build --preset ci --target IntrinsicGraphicsUnitTests IntrinsicGraphicsContractTests -j2`
- `ctest --test-dir build/ci --output-on-failure -R 'RHISamplerManager|RendererRhiBoundary' --timeout 60`
- Confirmed `build/dev-clang-ninja` C++ compiler: `/usr/bin/clang++` (Ubuntu clang version 23.0.0).
- `cmake --build build/dev-clang-ninja --target ExtrinsicBackendsVulkan -j2`
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root .`
- `python3 tools/repo/check_layering.py --root src --strict`
- `python3 tools/repo/check_test_layout.py --root . --strict`
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main`
- `cmake --build --preset ci --target IntrinsicTests -j2`
- `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 --quiet`

Planned focused commands for future re-runs:

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsUnitTests IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -R 'Sampler|RendererRhiBoundary' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes

- Do not add Vulkan types to RHI public headers/modules.
- Do not require Vulkan/GPU execution in the default CPU gate.
