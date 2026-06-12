---
id: BUG-034
theme: G
depends_on: []
maturity_target: CPUContracted
---
# BUG-034 — VulkanDevice never reclaims ResourcePool slots: unbounded pending-kill growth and handle-space leak

## Goal

- The promoted Vulkan device recycles destroyed-resource pool slots: after a destroy plus the retirement window, slots return to the pool free-list, `m_PendingKill` drains, and create/destroy churn no longer grows `m_Slots` monotonically for the lifetime of the device.

## Non-goals

- No change to the deferred *Vulkan-object* destruction path (`DeferDelete` / per-frame `DeletionQueue` / `FlushDeletionQueue`) — that mechanism is correct and separate.
- No `Core::ResourcePool` redesign; its contract is correct, the backend just never honors it.
- No new diagnostics surface beyond what falls out naturally (a pending-kill depth counter is optional, not required).
- The on-host GPU proof is owned by `BUG-035`, not this task.

## Context

- Symptom: `Core::ResourcePool` (src/core/Core.ResourcePool.cppm:20-30) is a generational pool with deferred reclamation: `Remove()` deactivates the slot and queues a `PendingKill` (73-84); **only** `ProcessDeletions(frameNumber)` — documented "must be called each frame" — resets slot data and returns indices to `m_Free` (87-107). `AllocSlot()` appends a brand-new slot whenever `m_Free` is empty (189-199).
- `VulkanDevice` declares its four pools with `RetirementFrames = kMaxFramesInFlight` (= 3, src/graphics/vulkan/Backends.Vulkan.Sync.cppm:19) at src/graphics/vulkan/Backends.Vulkan.Device.cppm:271-274, and calls `Remove(handle, m_GlobalFrameNumber)` from `DestroyBuffer`/`DestroyTexture`/`DestroySampler`/`DestroyPipeline` (Backends.Vulkan.Device.cpp:3733, 4065, 4277, 4559) — but `ProcessDeletions` is **never called anywhere in `src/graphics/vulkan/`** (`git log -S ProcessDeletions -- src/graphics/vulkan/` is empty; the call was never wired).
- Consequence: every resource ever destroyed permanently consumes a slot. `m_Slots` and `m_PendingKill` grow with *total creates*, not peak-live; the free-list never refills, so handle indices increase monotonically. The actual `VkBuffer`/`VkImage`/`VkSampler`/`VkPipeline`/VMA objects are *not* GPU-leaked — each `Destroy*` moves them out of the pooled struct and defers destruction through the per-frame `DeletionQueue` (e.g. 3726-3743, flushed at 2225/2523) — so this is unbounded CPU-side bookkeeping growth plus handle-space exhaustion, exactly what the pool's retirement design exists to prevent. The `DestroyBuffer` comment even states the intent: "Move the Vulkan objects out so the pool slot can be reclaimed."
- Churn sources that feed it: framegraph per-frame transients recreated on desc change/resize (src/graphics/renderer/Graphics.Renderer.cpp:5320/5323, 5354/5357), GPU asset cache eviction/reupload, pipeline hot reload. Long editor sessions on the promoted Vulkan path degrade steadily.
- The contract reference implementations both do this correctly: `NullDevice::EndFrame` calls `ProcessDeletions` on all four pools every frame (src/graphics/renderer/Backends/Null/Backends.Null.cpp:122-125), and the legacy RHI managers did the same (src/legacy/RHI/RHI.Buffer.cpp:328-331).
- Timing/threading constraints for the fix: `ProcessDeletions` must run on a single maintenance thread (pool contract) — the render thread that runs `BeginFrame`/`EndFrame` qualifies. Reclaim lag is already safe: a slot killed at frame N is reclaimed only when `globalFrameNumber > N + 3`, and the moved-out Vulkan objects are destroyed independently via fence-gated `FlushDeletionQueue`. Note `EndFrame` has **two** submit paths that increment `m_GlobalFrameNumber` (multi-queue ~2966, single-queue ~3018) plus failure early-returns (~2941-2957, ~2995-3011); the maintenance call must not be skipped on failed/headless frames or it silently stops reclaiming under fallback conditions.
- Impact: unbounded memory growth and monotonically increasing handle indices in every promoted-Vulkan session; violates the pool contract the rest of the engine relies on.
- Owner/layer: `graphics/vulkan` backend only; no RHI surface change.

## Required changes

- [ ] Add a private `VulkanDevice` maintenance step that calls `ProcessDeletions(m_GlobalFrameNumber)` on `m_Buffers`, `m_Images`, `m_Samplers`, and `m_Pipelines` once per frame, placed so **all** frame outcomes reach it (recommended: at the top of `BeginFrame` after the frame-slot fence wait, or as the unconditional tail of `EndFrame` covering both submit paths and the failure early-returns — pick one site, document why in a short comment).
- [ ] Audit `Shutdown`/destructor ordering: pending-kill entries hold moved-out (nulled) structs, and live slots are destroyed via the existing teardown path — confirm `Clear()`/teardown still releases everything with the new per-frame reclamation active (no double-destroy, since `Destroy*` nulls the handles before `Remove`).
- [ ] Confirm no consumer holds raw pool indices across more than `kMaxFramesInFlight` frames (the `GetByIndex` bulk-upload readers are the ones to check) — record the audit result in the PR description.

## Tests

- [ ] New RHI device contract test (CPU gate, runs on the Null device today and pins the contract the Vulkan fix must meet): create a buffer, destroy it, advance `RetirementFrames + 1` frames via `BeginFrame`/`EndFrame`, create again — assert the new handle **reuses the freed index with a bumped generation** (observable through public handles; today this passes on Null and the equivalent behavior is what BUG-035 proves on Vulkan).
- [ ] Extend `tests/unit/core/Test.Core.ResourcePool.cpp` if reclamation timing (`KillFrame + RetirementFrames` boundary, free-list refill, generation bump on reuse) is not already covered.
- [ ] Default CPU gate stays green.

## Docs

- [ ] Update `src/graphics/vulkan/README.md` (or the device module comment) to state the per-frame resource-maintenance contract: which call site runs `ProcessDeletions`, and why the deferred `DeletionQueue` is a separate mechanism.

## Acceptance criteria

- [ ] `ProcessDeletions` runs for all four pools every frame on the promoted Vulkan device, including failed-submit frames.
- [ ] The Null-device slot-recycling contract test exists, is labeled into the default CPU gate, and passes.
- [ ] Repeated create/destroy of a buffer across bounded frames yields a bounded set of handle indices (no monotonic growth) — asserted in the contract test on Null; the Vulkan-host proof is `BUG-035`.
- [ ] No change to RHI interfaces or renderer behavior.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ResourcePool|SlotRecycl|HandleReuse' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Calling `ProcessDeletions` from `Destroy*` (would collapse the retirement window the bulk-upload readers may rely on).
- Reducing `RetirementFrames` below `kMaxFramesInFlight`.
- Folding the `DeletionQueue` and pool-reclamation mechanisms together "while here".
- Claiming `Operational` from this task — that proof needs a Vulkan host and is owned by `BUG-035`.

## Maturity

- Target: `CPUContracted` here; `Operational` owned by `BUG-035` (on-host `gpu;vulkan` smoke proving slot reuse through the real `VulkanDevice` frame loop).
