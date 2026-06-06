# BUG-015 — ExtrinsicSandbox clustered Vulkan validation cascade

## Status

- Status: active (promoted from `tasks/backlog/bugs/` on 2026-06-05).
- Reported: 2026-06-05 from a local debug-build `ExtrinsicSandbox` run.
- Owner/layer: `graphics/vulkan`, `graphics/rhi`, `graphics/renderer`, and
  runtime-owned promoted Vulkan operational gating. `app` remains out of scope.

## Goal
- Fix the promoted Vulkan default sandbox path so `ExtrinsicSandbox` reaches an
  operational default-recipe frame (visible clear + working ImGui) on a
  Vulkan-capable host, with or without validation layers present, and without
  descriptor-layout, command-buffer-lifetime, or transient-image usage
  validation errors when validation layers are available.

## Non-goals
- Do not disable validation layers or demote the sandbox to the Null backend to
  hide the defect.
- Do not skip `ClusterGridBuildPass` or `LightClusterAssignmentPass` as a
  workaround unless the task records a separate follow-up that restores the
  clustered-lighting operational path.
- Do not introduce app-layer Vulkan knowledge; fixes stay in
  renderer/RHI/Vulkan/runtime ownership.

## Context
- Symptom: running the local debug `ExtrinsicSandbox` with promoted Vulkan,
  the device stays fail-closed
  (`VulkanRequestedButNotOperational reason=BarrierValidationFailed`) and never
  presents a usable frame; with the clustered-lighting compute passes present the
  pipeline-layout validation cascade is the first class reported.
- Expected behavior: `ExtrinsicSandbox` reaches an operational promoted Vulkan
  default-recipe frame (visible clear + working ImGui) on a Vulkan-capable host.
- Impact: blocks the visible-triangle → mesh/graph/point-cloud sandbox path and
  any Vulkan-backed visual verification.

## Diagnosis (2026-06-05)

Feedback loop: `build/ci-vulkan/bin/ExtrinsicSandbox` (authoritative preset),
run with a bounded `timeout`, capturing the full stdout/stderr log.

Root causes found, in dependency order:

1. **Crash (Bug A) — `VulkanDevice::CreateBuffer` dereferences a null
   `vkSetDebugUtilsObjectNameEXT`.** When `RenderConfig::EnableValidation` is
   requested but `VK_LAYER_KHRONOS_validation` / `VK_EXT_debug_utils` is not
   actually loaded, `m_ValidationEnabled` stays `true` while the debug-utils
   entry point is `nullptr`. `CreateImage`, `CreateSampler`, and the
   `SetDebugName` helper all guard with `&& vkSetDebugUtilsObjectNameEXT`, but
   `CreateBuffer` (`Backends.Vulkan.Device.cpp` ~3686) does not, so the first
   transient buffer allocation in `AllocateFrameTransientResources` SEGVs.
   Repro: ASan `SEGV ... CreateBuffer ... :3693` reached from
   `AllocateFrameTransientResources`.

2. **Clustered compute pipeline-layout mismatch (Bug B).**
   `assets/shaders/cluster_grid_build.comp` and
   `assets/shaders/light_cluster_assign.comp` declare descriptor-bound storage
   buffers at `set = 0, binding = 0..4`, but the engine's single global
   pipeline layout only exposes the bindless `COMBINED_IMAGE_SAMPLER` array at
   `set = 0, binding = 0` plus push constants; all other buffers use Buffer
   Device Address (BDA) via push constants (see `instance_cull.comp` +
   `common/gpu_scene.glsl`). With validation enabled this trips
   `VUID-VkComputePipelineCreateInfo-layout-07990/07988` at
   `vkCreateComputePipelines`. The two compute shaders must move to the BDA
   convention; the record helpers must pass buffer device addresses through
   push constants.

3. **Operational gate.** The runtime gate fails at
   `BarrierValidationClean`, which the renderer publishes via
   `NoteRecipeGraphValidation(recipeValidationClean)` where
   `recipeValidationClean = (recipe validation errors == 0) && transientResourcesReady`.
   Re-verify after Bug A/Bug B land.

4. **Visible blue clear.** The default-recipe color attachments clear to black
   (`ClearR/G/B = 0`). Set the default-recipe surface/clear color to blue so an
   operational frame is visually confirmable per the report's acceptance.

5. **Queue-family ownership-transfer (QFOT) barrier cascade (Bug E,
   2026-06-05).** With the device operational, the run emits a high-volume
   validation cascade on `GpuWorld.Lights` / `ClusterLights.Headers` /
   `ClusterLights.Indices`: `UNASSIGNED-VkBufferMemoryBarrier-buffer-00004`
   (acquire ownership `srcQueueFamilyIndex 2 -> dstQueueFamilyIndex 0` with no
   matching release queued) plus `-00001` / `-00003` duplicate release/acquire
   warnings. Root cause: the cluster passes author `RenderQueue::AsyncCompute`
   (`Graphics.FrameRecipe.cpp` `builder.SetQueue(...)`), and the framegraph
   compiler emits cross-queue release/acquire ownership transfers from each pass's
   *requested* queue (`Graphics.RenderGraph.Compiler.cpp` ~1697-1745). The
   promoted device reports a graphics-only framegraph `QueueCapabilityProfile`
   and `PartitionPassesByQueue` demotes every pass onto the graphics queue at
   submit time, so all work is recorded on one queue — but the Vulkan barrier
   path (`SubmitBarriers`) resolved the async/transfer ownership tokens against
   *physical* queue-family presence (family 2 exists), lowering the transfers to a
   real second family that single-queue submission never matches. Fix: the device
   binds the async-compute/transfer barrier families into each command context
   only when the framegraph profile actually schedules onto them
   (`ResolveFrameGraphBarrierQueueFamilies`); under the graphics-only profile they
   are `VK_QUEUE_FAMILY_IGNORED`, so async/transfer tokens resolve back to the
   graphics family and no QFOT barrier is recorded. (Compiler-level demotion that
   avoids emitting the now-redundant release/acquire pair entirely remains a
   `GRAPHICS-039D` async-affinity follow-up.)

## Required changes
- [x] Guard `CreateBuffer`'s debug-name path on a non-null
      `vkSetDebugUtilsObjectNameEXT` (match `CreateImage`/`CreateSampler`).
- [x] Convert the cluster-grid and light-assignment compute shaders to the BDA
      convention so pipeline creation is validation-clean against the global
      pipeline layout. Verified at the SPIR-V level: both modules now use
      `PhysicalStorageBufferAddresses` with zero descriptor-set bindings.
- [x] Pass cluster buffer device addresses through the record helpers'
      push constants; cluster buffers already carry storage/device-address usage.
- [x] Set the default-recipe clear color to blue (forward no-motion and
      forward+motion scene-color attachments).
- [x] Re-run the sandbox: `IDevice::IsOperational()` flips true and the full
      pipeline records (`ClusterGridBuildPass`, `LightClusterAssignmentPass`,
      `ImGuiPass`, `Present` all `Recorded`) with no crash and no cluster
      pipeline-layout validation errors.
- [x] Root-cause + durably fix the recurring present-pass crash: it was a
      ccache + C++23-modules stale-object hazard (`depend_mode=false` cannot track
      module BMI changes), producing an `ICommandContext` vtable slot mismatch
      between the renderer and the Vulkan backend (`cmd.Draw(...)` dispatching
      into `PushConstants(...)`). `cmake/Dependencies.cmake` now runs ccache in
      depend mode.
  - [ ] **Reverified 2026-06-05 — depend-mode remedy is INCOMPLETE on this host
        (NVIDIA 590.48.01 / clang-20).** The same `cmd.Draw(3,1,0,0)` →
        `VulkanCommandContext::PushConstants(data=0x3, size=1)` SEGV
        (`memcpy` from `0x3`, the `vertexCount=3` arg reinterpreted as the
        `const void* data` pointer) still reproduces on a `ci-vulkan`
        `--clean-first` rebuild *when ccache serves the cached objects*. A
        `CCACHE_DISABLE=1 --clean-first` rebuild of the identical sources runs the
        sandbox crash-free (15s, no SEGV), which confirms the root cause is ccache
        serving a vtable-inconsistent object even under depend mode. Follow-up:
        strengthen the ccache safety (e.g. include module BMI/compiler identity in
        the hash or fall back to `ccache -C` on module-interface churn) under a
        build-hygiene task; tracks with the BUG-013 clang-20 modules vtable hazard.
- [x] Stop the QFOT validation cascade (Bug E): bind async-compute/transfer
      barrier families into command contexts only when the framegraph
      `QueueCapabilityProfile` schedules onto them
      (`ResolveFrameGraphBarrierQueueFamilies` in `Backends.Vulkan.cppm`; applied
      at all three `VulkanCommandContext::Bind` sites in
      `Backends.Vulkan.Device.cpp`). Under the graphics-only profile async/transfer
      ownership tokens resolve to the graphics family and no cross-queue barrier is
      recorded.
- [x] **Primary, build-staleness-robust QFOT fix (renderer lowering).** The
      backend-only gate above was insufficient in practice because a stale
      `Backends.Vulkan.Device.cpp` object (the confirmed ccache + C++23-modules
      hazard) kept binding the physical async/transfer families, so the QFOT
      `…buffer-00004` acquire-without-release error and `-00001/-00003` duplicate
      warnings persisted. The renderer now neutralizes the ownership transfer at
      the lowering seam: `SubmitBarrierPacket` collapses a compiled release/acquire
      pair to a plain barrier (`SrcQueueFamily == DstQueueFamily == IGNORED`)
      whenever the device's framegraph `QueueCapabilityProfile` resolves the
      producer and consumer onto the same queue, via the shared
      `IsLiveCrossQueueOwnershipTransfer(transfer, profile)` predicate
      (`Graphics.RenderGraph:Barriers`). Because `Graphics.Renderer.cpp` is a
      directly edited TU it always recompiles, so this takes effect even when the
      Vulkan backend object lags. Covered by
      `OwnershipTransferBarriers.LiveCrossQueueTransferDependsOnDeviceProfile`
      (default CPU gate).

## Remaining (follow-up)
- [ ] **Black-frame rendering correctness — carved out to
      [`BUG-016`](../backlog/bugs/BUG-016-extrinsic-sandbox-operational-frame-black-readback.md).**
      With the device operational and the QFOT cascade cleared, the
      sandbox-acceptance GPU smoke still reads the backbuffer back entirely black
      (`nonBlackPixels == 0`) even though every pass — including `Present` and
      `ImGuiPass` — records and ImGui produces draw data (7 lists / 1900 verts).
      This is a downstream postprocess/present/ImGui (or transient-resource)
      content defect, out of scope for the clustered-validation cascade fixed
      here. It is localizable in this environment (per-attachment GPU readback
      bisection + validation `debug_printf`); interactive RenderDoc is **not**
      required. Regressed in the GRAPHICS-039/040 window after `RUNTIME-095`
      verified a non-black frame on 2026-06-04.



## Tests
- [ ] CPU contract coverage that the cluster compute pipeline descs carry the
      BDA push-constant layout (no descriptor-bound storage-buffer requirement).
- [ ] Update `Test.LightClusterGrid.cpp` record-helper coverage for the new
      BDA push-constant payloads.
- [x] Contract regression `VulkanFailClosedContract.FrameGraphBarrierFamiliesCollapseUnderGraphicsOnlyProfile`
      (`tests/contract/graphics/Test.VulkanFailClosedContract.cpp`): a graphics-only
      framegraph profile collapses distinct physical async/transfer families to
      `VK_QUEUE_FAMILY_IGNORED`, so the resolved ownership tokens map back to the
      graphics family (QFOT suppressed); a full profile preserves the families.
- [x] Contract regression `OwnershipTransferBarriers.LiveCrossQueueTransferDependsOnDeviceProfile`
      (`tests/contract/graphics/Test.OwnershipTransferBarriers.cpp`, default CPU
      gate): the renderer's `IsLiveCrossQueueOwnershipTransfer` predicate collapses
      a compiled async/transfer ownership transfer under the graphics-only profile
      and keeps it live only when the profile genuinely exposes that queue.
- [ ] Preserve the default CPU-supported gate and existing ImGui/default-recipe
      Vulkan smokes.

## Docs
- [ ] Update `src/graphics/vulkan/README.md` and/or
      `docs/architecture/rendering-three-pass.md` for the clustered compute BDA
      contract.
- [ ] Update `tasks/backlog/bugs/index.md` when the root cause and verified fix
      are known.
- [ ] Refresh `docs/api/generated/module_inventory.md` if module surfaces change.

## Acceptance criteria
- [ ] `ExtrinsicSandbox` no longer crashes when validation is requested but the
      layer is unavailable.
- [ ] With validation available, the clustered compute pipelines create without
      `VUID-VkComputePipelineCreateInfo-layout-07990/07988`.
- [ ] `ExtrinsicSandbox` reaches an operational promoted Vulkan frame and shows a
      blue background with working ImGui on a Vulkan-capable host.
- [ ] Default CPU/null contract gate stays green.

## Verification
```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target ExtrinsicSandbox IntrinsicGraphicsContractCpuTests
LSAN_OPTIONS=suppressions=$PWD/lsan.supp timeout 15s ./build/ci-vulkan/bin/ExtrinsicSandbox

cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes
- Shipping a fix without a regression test when one is feasible.
- Silencing validation messages without fixing the underlying defect.
- Moving Vulkan descriptor/queue/image-layout policy into `runtime`/`app`/`ecs`/
  `assets`/`platform`.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; the default CPU/null contract
  gate must remain green.
