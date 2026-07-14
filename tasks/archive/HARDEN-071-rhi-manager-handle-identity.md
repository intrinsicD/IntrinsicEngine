# HARDEN-071 — RHI buffer/texture manager handle identity

## Status
- Closed: 2026-05-26. Landed alongside the GRAPHICS-076 Slice D fixture
  skeleton on branch `claude/graphics-076-slice-d-fixture-skeleton`.
- PR/commit: branch `claude/graphics-076-slice-d-fixture-skeleton`,
  sibling commit to the prior Slice D fixture commit `d115a03e`. PR
  to be opened after push; the commit SHA is whichever `HEAD` of this
  branch carries the changes under `src/graphics/rhi/RHI.*Manager.cpp`
  and this task file.
- Discovered as upstream root cause during GRAPHICS-076 Slice D diagnosis on
  a verified Vulkan-capable host (NVIDIA RTX 3050, Vulkan 1.4.309, driver
  1.4.325). The Slice D fixture currently still skips on this host pending
  the *next* upstream blocker (SelectionOutline pipeline creation +
  vkCmdPipelineBarrier driver SEGV); those are tracked separately and are
  out of scope here.
- Maturity: `CPUContracted`. The handle-identity guarantee is exercised by
  every existing RHI buffer/texture manager unit test (32 GPU smoke +
  CPU-gate runs pass) and by every renderer texture/buffer-upload call path
  that was previously silently rejected.

## Goal
- Make the `RHI::TextureManager` and `RHI::BufferManager` lease handle IS the
  `IDevice`-issued handle, eliminating the manager-local pool-handle
  indirection that caused every `IDevice::GetTransferQueue().UploadTexture()`
  / `UploadBuffer()` call to be silently rejected by the Vulkan backend.

## Non-goals
- Do not change the public surface of `TextureManager` or `BufferManager`.
  `Create()` still returns a `Core::Expected<Lease>`; `Lease::GetHandle()`
  still returns a handle of the same `RHI::TextureHandle` / `RHI::BufferHandle`
  type. The change is purely the *identity* of that handle.
- Do not touch the Vulkan backend (`src/graphics/vulkan/`). The fix is on
  the RHI side because RHI is the layer that was issuing a non-device-resolvable
  handle. The backend's `m_Buffers->GetIfValid(handle)` /
  `m_Images->GetIfValid(handle)` lookup was already correct.
- Do not touch the `Core::ResourcePool` generational-handle scheme used inside
  `VulkanDevice` itself. That stays the canonical authority for handle
  uniqueness; this fix just stops shadowing it with a parallel scheme.

## Context
- **Symptom (reproduced on RTX 3050 host):** with the default frame recipe,
  `PostProcess.SMAA.AreaTex upload rejected; lease released, will retry on
  next Initialize.` was logged on every Initialize. Trace:
  `Graphics.PostProcessSystem.cpp:399` calls
  `device.GetTransferQueue().UploadTexture(lease.GetHandle(), ...)`;
  inside `VulkanTransferQueue::UploadTexture` (`Backends.Vulkan.Transfer.cpp:304`)
  `m_Images->GetIfValid(dst)` returns nullptr, and the upload is dropped with
  "destination texture handle is invalid". The same shape affected
  `BufferManager` uploads but was less visible because most renderer
  buffers are GPU-only or device-owned-private.
- **Diagnosis:** added a one-line `[DBG-a4f2]` probe at the success exit of
  `TextureManager::Create()` printing `deviceHandle={idx,gen} poolHandle={idx,gen}`.
  On `PostProcess.SMAA.AreaTex` the device returned `{idx=4, gen=1}` while the
  manager republished `{idx=0, gen=0}`. The Vulkan backend's pool obviously
  cannot resolve `{idx=0, gen=0}` back to the `VkImage` it allocated at
  `{idx=4, gen=1}`. Every texture/buffer upload through the manager-issued
  lease was therefore guaranteed to silently fail.
- **Root cause:** `TextureManager` / `BufferManager` allocated a parallel
  slot pool (deque + free-list + generation counter) and published its own
  `{slotIndex, slotGeneration}` as the lease handle, opaque to the device
  pool. The two pools' index/generation spaces are independent. The
  `DeviceHandle` was stored *inside* the slot only for `DestroyTexture`/
  `DestroyBuffer`. Any caller that took `lease.GetHandle()` straight to a
  device-side API (which is the documented and exercised pattern — see
  `Graphics.PostProcessSystem.cppm:122` doc comment) hit a dead end.
- **Expected behavior:** `lease.GetHandle()` is the `IDevice` cookie, so
  `device.GetTransferQueue().UploadTexture(lease.GetHandle(), ...)` and
  `device.GetTransferQueue().UploadBuffer(lease.GetHandle(), ...)` resolve
  against the same pool that issued the handle.
- **Impact:** with the fix, on the same RTX 3050 host the SMAA AreaTex /
  SearchTex uploads succeed silently (no rejection warnings), default-recipe
  Initialize progresses further, and the `DefaultRecipeSurfaceGpuSmoke`
  fixture now reaches the first frame's barrier-submission path before
  stalling on the *next* unrelated upstream blocker (SelectionOutline
  pipeline creation rejected → vkCmdPipelineBarrier SEGV inside the NVIDIA
  driver). Those are real subsequent work, not regressions caused by this
  change. On the rest of the test surface (CPU gate, baseline MinimalDebug
  GPU smokes), the change is invisible — every existing exerciser of these
  managers continues to pass.

## Required changes
- [x] Re-key `TextureManager::Impl::Slots` from a `std::deque<TextureSlot>`
      indexed by `slotIndex` to an
      `std::unordered_map<TextureHandle, TextureSlot, Core::StrongHandleHash<TextureTag>>`
      keyed by the IDevice-issued `TextureHandle`. Drop the `DeviceHandle`
      slot field as a separate storage; the map key holds it. Add a
      `CurrentDeviceHandle` field so `Reupload()` can swap the underlying
      VkImage (mip-streaming case) without invalidating outstanding leases
      keyed on the original handle.
- [x] Re-key `BufferManager::Impl::Slots` from `std::deque<BufferSlot>` +
      `FreeList` to
      `std::unordered_map<BufferHandle, BufferSlot, Core::StrongHandleHash<BufferTag>>`.
      Drop the `DeviceHandle` slot field; the map key holds it. Buffers
      have no `Reupload()` equivalent (transfer queue does in-place
      `UploadBuffer` at offset), so no `CurrentDeviceHandle` mirror is
      needed.
- [x] Update both managers' `Create()` to publish `deviceHandle` (not a
      manager-local pool handle) through `Lease::Adopt(*this, deviceHandle)`.
      Add an `assert(inserted)` on the `try_emplace` to catch the
      pathological case where the device pool would issue a duplicate live
      handle.
- [x] Update both managers' `Release()` to snapshot the destroy-side state
      (`BindlessSlot`, `CurrentDeviceHandle`) under the mutex, erase the
      map entry, then drop the lock before calling `Device.DestroyTexture()`
      / `Device.DestroyBuffer()` and `Bindless.FreeSlot()`. We cannot hold
      a raw `Slot*` across the erase because `unordered_map::erase`
      invalidates the node's address.
- [x] Mark both slot structs as non-movable in addition to non-copyable
      (atomic refcount + node-based map storage gives stable addresses).
- [x] Update the inline comments in both `.cpp` files to record the new
      storage shape, the handle-identity guarantee, the complexity
      characteristics (amortised O(1) for create/retain/release/view/getdesc
      via map insert/find/erase), and a cross-reference to GRAPHICS-076
      Slice D diagnosis.

## Tests
- [x] Existing `RHITextureManager.*` and `RHIBufferManager.*` unit tests
      (`tests/unit/graphics/Test.RHI.TextureManager.cpp`,
      `tests/unit/graphics/Test.RHI.BufferManager.cpp`) continue to pass
      unchanged. They cover Create-on-success, Create-on-device-failure,
      Create-on-non-operational-device, last-lease-drop destruction,
      destructor cleanliness, sampler binding, and bindless registration.
- [x] On a Vulkan-capable host, the default frame recipe's SMAA AreaTex
      and SearchTex uploads now succeed silently (no "destination texture
      handle is invalid" / "upload rejected" warnings). Recorded in
      session notes for GRAPHICS-076 Slice D.
- A targeted regression test that exercises "lease handle is consumable by
  IDevice transfer queue" is the next-most-valuable test to add, but it
  requires a transfer-queue mock that records the handle passed to
  `UploadTexture`/`UploadBuffer`. `Tests::MockDevice` (used by the existing
  manager unit tests) does not currently model a transfer queue. Adding it
  is tracked as a follow-up under the same backlog row that owns the
  manager unit tests.

## Docs
- [x] Inline `.cpp` comments updated as part of the required changes
      (above): new storage-shape paragraph + handle-identity guarantee +
      cross-reference to GRAPHICS-076 Slice D diagnosis.
- [x] `tasks/archive/GRAPHICS-076-default-recipe-debug-view-and-present-wiring.md`
      carries a 2026-05-26 follow-up clarification pointing at this task
      for the upstream fix that retired the upload-rejection symptom.
- [x] `src/graphics/rhi/README.md` does not currently call out either
      manager's storage shape and was not updated; if a future task adds
      a storage-shape paragraph there, it should link this fix. Tracked
      as a documentation-only follow-up under the same backlog row that
      owns the README.

## Acceptance criteria
- [x] CPU gate green: `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` → 2286/2286.
- [x] Vulkan gate green: `ctest --test-dir build/ci-vulkan -L gpu -LE 'slow|flaky-quarantine' --output-on-failure --timeout 120` → 32/32 (DefaultRecipeSurfaceGpuSmoke still skips with structured reason pending the next upstream blocker).
- [x] No layering violations: `python3 tools/repo/check_layering.py --root src --strict`.
- [x] No task-policy regressions: `python3 tools/agents/check_task_policy.py --root . --strict`.
- [x] No broken doc links: `python3 tools/docs/check_doc_links.py --root .`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests
ctest --test-dir build/ci-vulkan -L gpu -LE 'slow|flaky-quarantine' --output-on-failure --timeout 120

python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Follow-ups (not in scope here)
- Default-recipe SelectionOutline pipeline rejected by `VulkanDevice::CreatePipeline`
  (`error=403`) — needs a `graphics/vulkan` bring-up ticket.
- Default-recipe vkCmdPipelineBarrier SEGV inside NVIDIA's driver
  (`libnvidia-glcore.so.590.48.01+0xe8251d`) reached via
  `VulkanCommandContext::SubmitBarriers` — likely caused by a barrier
  referencing a still-non-Operational image or by a missing pipeline state;
  needs a `graphics/vulkan` debug ticket.
- Targeted regression test "lease handle is consumable by IDevice transfer
  queue" — needs a `MockTransferQueue` extension to `Tests::MockDevice`.

## Forbidden changes
- Re-introducing a manager-local-pool-handle indirection. Future managers
  that need an indirection layer must either (a) publish the device handle
  *as* the lease handle and store any extra per-slot state map-keyed by
  the device handle (the pattern this task adopts), or (b) document a
  translation API on the manager and audit every device-facing call site
  to use it explicitly. Silent indirection is the bug this task fixes.






