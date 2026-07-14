# BUG-015 — ExtrinsicSandbox clustered Vulkan validation cascade

## Status

- Status: done (retired 2026-06-06).
- Reported: 2026-06-05 from a local debug-build `ExtrinsicSandbox` run.
- Completed: 2026-06-06. Implementation commits: `51e905fd`, `eef2622d`,
  `bcbee8b5`; retirement recorded in this commit.
- Commit: this retirement commit records the final task move and backlog index
  synchronization.
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
- [x] Root-cause the recurring present-pass crash to a ccache + C++23-modules
      stale-object hazard (`depend_mode=false` cannot track module BMI changes),
      producing an `ICommandContext` vtable slot mismatch between the renderer and
      the Vulkan backend (`cmd.Draw(...)` dispatching into `PushConstants(...)`).
      `cmake/Dependencies.cmake` now runs ccache in depend mode as a first safety
      mitigation.
- [x] Record the remaining ccache/modules caveat outside this rendering bug.
      Reverification on 2026-06-05 showed that ccache can still serve a
      vtable-inconsistent object on this host even under depend mode, while an
      identical `CCACHE_DISABLE=1` rebuild runs crash-free. Structural
      command-context hardening remains tracked by
      [`HARDEN-073`](HARDEN-073-rhi-command-context-vtable-key-function.md);
      this BUG-015 retirement verifies the rendering fix with ccache disabled
      after the cache/toolchain failure is observed.
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
- [x] **Black-frame rendering correctness — resolved by
      [`BUG-016`](BUG-016-extrinsic-sandbox-operational-frame-black-readback.md).**
      The downstream black-readback defect was localised with per-stage GPU
      readback bisection. Its root causes were frame-sampled bridge slot 0 being
      clobbered by late barrier/ImGui descriptor writes and recipe clear colors
      being dropped during framegraph compilation. BUG-016 retired on 2026-06-06
      with the light-blue clear and visible ImGui covered by automated
      `gpu;vulkan` smokes.

## Tests
- [x] CPU contract coverage that the cluster compute pipeline descs carry the
      BDA push-constant layout (no descriptor-bound storage-buffer requirement).
- [x] Update `Test.LightClusterGrid.cpp` record-helper coverage for the new
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
- [x] Preserve the default CPU-supported gate and existing ImGui/default-recipe
      Vulkan smokes.

## Docs
- [x] Update `src/graphics/vulkan/README.md` and/or
      `docs/architecture/rendering-three-pass.md` for the clustered compute BDA
      contract.
- [x] Update `tasks/backlog/bugs/index.md` when the root cause and verified fix
      are known.
- [x] Refresh `docs/api/generated/module_inventory.md` if module surfaces change.

## Acceptance criteria
- [x] `ExtrinsicSandbox` no longer crashes when validation is requested but the
      layer is unavailable.
- [x] With validation available, the clustered compute pipelines create without
      `VUID-VkComputePipelineCreateInfo-layout-07990/07988`.
- [x] `ExtrinsicSandbox` reaches an operational promoted Vulkan frame and shows a
      blue background with working ImGui on a Vulkan-capable host.
- [x] Default CPU/null contract gate stays green.

## Verification
```bash
# Focused Vulkan/source verification from the implementation commits:
cmake --build --preset ci-vulkan --target IntrinsicGraphicsContractCpuTests IntrinsicGraphicsVulkanSmokeTests ExtrinsicSandbox
ctest --test-dir build/ci-vulkan --output-on-failure -R 'ImGuiPassContract|ImGuiSurfaceGpuSmoke|VisualizationOverlaySurfaceGpuSmoke|RuntimeSandboxAcceptanceGpuSmoke' --timeout 120
python3 tools/repo/check_layering.py --root src --strict

# Retirement verification on 2026-06-06:
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

Results: focused Vulkan smokes passed 20/20 in the implementation slice; the
default CPU gate passed 2787/2787 on 2026-06-06. The ccache-enabled
`IntrinsicTests` build first failed with a clang-23 frontend bus error in module
compilation, then the identical build with `CCACHE_DISABLE=1` passed.

## Forbidden changes
- Shipping a fix without a regression test when one is feasible.
- Silencing validation messages without fixing the underlying defect.
- Moving Vulkan descriptor/queue/image-layout policy into `runtime`/`app`/`ecs`/
  `assets`/`platform`.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; the default CPU/null contract
  gate must remain green.
