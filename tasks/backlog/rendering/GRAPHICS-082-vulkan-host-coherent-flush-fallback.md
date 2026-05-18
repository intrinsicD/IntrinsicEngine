# GRAPHICS-082 — Track non-`HOST_COHERENT` flush fallback in promoted Vulkan device

## Goal
- Replace the bare `// This is left as a TODO for production hardening.` marker at `src/graphics/vulkan/Backends.Vulkan.Device.cpp:2974` with either (a) an implemented `vmaFlushAllocation(...)` fallback for non-`HOST_COHERENT` upload paths, or (b) a tracked task-tagged TODO referencing this task ID, so the promoted Vulkan backend has no untagged production-hardening markers.

## Non-goals
- Do not rewrite the staging-buffer upload pipeline. The fast/slow path split and the synchronous `vkQueueWaitIdle` slow path are out of scope.
- Do not change RHI public surfaces (`graphics/rhi/`).
- Do not introduce a new memory-property heuristic abstraction; reuse whatever VMA already exposes.
- Do not add new render features or buffer types under cover of this task.

## Context
- Owning subsystem/layer: `src/graphics/vulkan/`. Per `AGENTS.md` §4 the Vulkan backend may use `Vulkan::Vulkan`, `volk`, `VulkanMemoryAllocator`, and `glfw` only.
- Discovered site: `src/graphics/vulkan/Backends.Vulkan.Device.cpp:2974` (within `Device::UpdateBuffer` / staging-buffer path). The surrounding comment notes that on discrete GPUs that lack `HOST_COHERENT`, the mapping is write-combined and a coherent flush would be needed; today the code returns without calling `vmaFlushAllocation`.
- All other Vulkan/Null TODOs in promoted layers are tagged (`TODO(GRAPHICS-018)` etc.) or documented in a sibling README (`src/graphics/renderer/Backends/Null/README.md`). This is the only untagged production-hardening marker in the promoted backend.
- Engine currently targets desktops where `HOST_COHERENT` is typically present, so the missing flush is latent rather than observed — but per `AGENTS.md` §5 it should be either fixed or tracked, not left bare.

## Required changes
- [ ] Pick one outcome and implement it:
  - [ ] **Outcome A (preferred):** Call `vmaFlushAllocation(m_Vma, buf->Allocation, offset, size)` on the fast path when the allocation's memory type does not include `VK_MEMORY_PROPERTY_HOST_COHERENT_BIT`. Use `vmaGetAllocationMemoryProperties` (or the cached memory-type properties on the allocation) to gate the call so the coherent fast path remains zero-cost.
  - [ ] **Outcome B (fallback):** Replace the bare TODO with `// TODO(GRAPHICS-082): add vmaFlushAllocation fallback for non-HOST_COHERENT mappings.` and leave behavior unchanged.
- [ ] If Outcome A: add a focused contract test that exercises `Device::UpdateBuffer` on a fast-path mapping and asserts the buffer contents are observable via `IsOperational()`-gated readback. Skip if the host has no Vulkan device.

## Tests
- [ ] Default CPU gate stays green:
      `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`.
- [ ] Vulkan-labeled tests (opt-in) stay green where they previously passed:
      `ctest --test-dir build/ci --output-on-failure -L vulkan --timeout 60` (when a Vulkan device is available).
- [ ] If Outcome A is taken: new contract test `tests/contract/graphics/Test.VulkanUpdateBufferFlush.cpp` (or extension of an existing one) covers the non-`HOST_COHERENT` branch under a label-skipped guard when the device reports coherent memory only.

## Docs
- [ ] If Outcome A: add a one-line entry to [`src/graphics/vulkan/README.md`](../../../src/graphics/vulkan/README.md) (or the closest current README) noting the flush fallback.
- [ ] If Outcome B: no doc change; the TODO tag itself is the record.

## Acceptance criteria
- [ ] No bare `TODO for production hardening` text remains in `src/graphics/vulkan/Backends.Vulkan.Device.cpp`.
- [ ] If Outcome A: `grep -nE 'TODO[^(]' src/graphics/vulkan/Backends.Vulkan.Device.cpp` returns no hits in the staging-upload region; the new code path is exercised by at least one contract test.
- [ ] If Outcome B: every remaining `TODO` in promoted Vulkan code carries a `TODO(<TASK-ID>)` tag.
- [ ] Layering allowlist is unchanged.
- [ ] Promoted Vulkan backend continues to gate on `RHI::IDevice::IsOperational()`, not Vulkan diagnostics (`AGENTS.md` §4).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

python3 tools/repo/check_layering.py --root src --strict

# Sanity: no untagged production-hardening TODO left at the discovered site.
! grep -nE 'TODO for production hardening' src/graphics/vulkan/Backends.Vulkan.Device.cpp
```

## Forbidden changes
- Changing RHI public surfaces.
- Wiring ECS, runtime, or live-asset traffic into `graphics/vulkan/`.
- Promoting Vulkan device behavior past `Operational` per `docs/agent/task-maturity.md` without a separate task.
- Skipping or quarantining existing Vulkan tests to make this commit green.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` elsewhere (unchanged by this task).
- Outcome A keeps the Vulkan backend at `Operational` and removes a latent correctness gap.
- Outcome B is `Scaffolded → tracked`; explicit follow-up is the same task ID re-promoted with Outcome A.
