# GRAPHICS-082 — Track non-`HOST_COHERENT` flush fallback in promoted Vulkan device

## Status

- Status: done — Outcome A landed. The promoted Vulkan backend now caches each host-visible allocation's `HOST_COHERENT` flag at creation time and gates a `vmaFlushAllocation` fallback on the `WriteBuffer` fast path. The bare `TODO for production hardening` marker is removed.
- Maturity: `Operational` on Vulkan-capable hosts; `CPUContracted` elsewhere (unchanged).
- Owner/agent: Claude on `claude/setup-agentic-workflow-8uTIT`.
- Branch: `claude/setup-agentic-workflow-8uTIT`.
- Completed: 2026-05-19.
- Commit/PR: pending current change on `claude/setup-agentic-workflow-8uTIT`.

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
- [x] Outcome A (preferred): cache the per-allocation `HOST_COHERENT` flag on `VulkanBuffer` via `vmaGetAllocationMemoryProperties` at `CreateBuffer` time and call `vmaFlushAllocation(m_Vma, buf->Allocation, offset, size)` on the `WriteBuffer` fast path only when the cached flag reports the mapping is non-coherent.
- [x] Outcome A coverage: extended the opt-in `VulkanBootstrapSmoke` (`tests/gpu/Test.VulkanBootstrapSmoke.cpp`) so the host write is consumed by the GPU before any host read — `WriteBuffer` stages a pattern into a host-visible `TransferSrc` buffer, the frame's graphics context records `CopyBuffer` from that source into a host-visible `TransferDst` destination, and `ReadBuffer` pulls the device-written bytes from the destination after `Present`. A missing or broken `vmaFlushAllocation` on non-`HOST_COHERENT` memory would let `vkCmdCopyBuffer` consume stale source bytes and the destination readback would fail; on `HOST_COHERENT` hosts the flush is a documented no-op and the GPU still observes the pattern. The fixture skips on hosts without a Vulkan device.

## Tests
- [x] Default CPU gate stays green:
      `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`.
- [x] Vulkan-labeled tests (opt-in) remain consistent with prior behavior:
      `ctest --test-dir build/ci --output-on-failure -L vulkan --timeout 60` — on hosts without a Vulkan ICD or GLFW surface the existing `VulkanBootstrapSmoke` fixture skips before reaching device creation; on Vulkan-capable hosts the new round-trip block runs inside the existing service-ready guard.
- [x] Outcome A: round-trip coverage added by extending the canonical `VulkanBootstrapSmoke` fixture rather than creating a new contract file; the contract task allowed an extension of an existing fixture and contract-only CPU tests cannot exercise the live VMA flush gate.

## Docs
- [x] Added a one-line entry to [`src/graphics/vulkan/README.md`](../../../src/graphics/vulkan/README.md) noting the cached `HOST_COHERENT` flag and the `vmaFlushAllocation` fast-path fallback.

## Acceptance criteria
- [x] No bare `TODO for production hardening` text remains in `src/graphics/vulkan/Backends.Vulkan.Device.cpp`.
- [x] Outcome A: `grep -nE 'TODO[^(]' src/graphics/vulkan/Backends.Vulkan.Device.cpp` returns no hits in the staging-upload region; the new code path is exercised by the `VulkanBootstrapSmoke` host→GPU→host round-trip (`WriteBuffer` → `CopyBuffer` → `ReadBuffer`) on hosts with a live Vulkan device.
- [x] Layering allowlist is unchanged.
- [x] Promoted Vulkan backend continues to gate on `RHI::IDevice::IsOperational()`, not Vulkan diagnostics (`AGENTS.md` §4).

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
