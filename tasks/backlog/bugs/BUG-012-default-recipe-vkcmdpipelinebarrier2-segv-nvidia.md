# BUG-012 — Default-recipe `vkCmdPipelineBarrier2` SEGV in NVIDIA driver

## Goal
- Identify and fix the malformed pipeline barrier (or unrooted image
  handle reference) that the default frame recipe submits during its
  first `engine.Run()` frame, which crashes `libnvidia-glcore.so` on
  Linux/NVIDIA hosts and prevents the default recipe from ever
  reaching `IDevice::IsOperational() == true`.
- This is the single remaining upstream blocker preventing
  `GRAPHICS-076 / 077 / 078` Slice D from graduating from `Skipped` to
  `Passed` on Vulkan-capable hosts.

## Non-goals
- Do not work around the SEGV by skipping the offending pass; the
  fix must resolve the barrier so the default recipe runs end-to-end.
- Do not raise / lower the `MaxPushConstantBytes` cap, the SMAA upload
  identity, or any of the surface fixes already addressed by HARDEN-071
  and HARDEN-072.

## Context
- Symptom: with the `ci-vulkan` preset
  (`INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON`) on NVIDIA RTX 3050,
  driver 590.48.01, Vulkan 1.4.309, running
  `DefaultRecipeSurfaceGpuSmoke.RecipeSelectorReachesOperationalVulkanCommandStream`
  with the cold-gate bootstrap pre-check bypassed yields an
  AddressSanitizer-reported SEGV inside
  `libnvidia-glcore.so.590.48.01+0xe8251d` (verified 2026-05-27 on
  branch `claude/graphics-076-slice-d-fixture-skeleton` after
  HARDEN-071 + HARDEN-072 landed).
- Reproducible stack (excerpts from ASan report):
  ```
  #3 libnvidia-glcore.so.590.48.01+0xf94d52
  #4 VulkanCommandContext::SubmitBarriers
      src/graphics/vulkan/Backends.Vulkan.CommandContext.cpp:574
      → vkCmdPipelineBarrier2(m_Cmd, &dep)
  #5 SubmitBarrierPacket
      src/graphics/renderer/Graphics.Renderer.cpp:179
  #6 NullRenderer::ExecuteFrame lambda
      src/graphics/renderer/Graphics.Renderer.cpp:2099
  ...
  #17 Engine::Run
      src/runtime/Runtime.Engine.cpp:542
  ```
- Expected behavior: the recipe-aware validator
  (`m_Device->NoteRecipeGraphValidation(...)` at
  `Graphics.Renderer.cpp:1365`) should either reject any malformed
  barrier (`recipeValidationClean = false`) so the operational gate
  cannot flip true, OR the barrier emission code should produce
  driver-valid `VkImageMemoryBarrier2` entries for every image touched
  by the default recipe. Right now the validator passes (recipe-aware
  validation is clean) but the actual barrier still crashes the
  driver, which means there is a gap between what the validator
  inspects and what the backend submits.
- Impact: all three of `GRAPHICS-076` (default-recipe present + debug
  view), `GRAPHICS-077` (transient debug primitives), and `GRAPHICS-078`
  (visualization overlay) Slice D `gpu;vulkan` smokes are currently
  parked at `Skipped` on every Vulkan-capable host because of this one
  SEGV. The CPU-only correctness gate, MinimalDebug smoke, and every
  other graphics test is unaffected (only the default recipe trips
  this barrier).
- Adjacent already-landed fixes:
  - `HARDEN-071` (RHI buffer/texture manager handle identity) — cleared
    the upstream SMAA AreaTex / SearchTex upload-rejection blocker.
  - `HARDEN-072` (push-constant cap + virtual default-arg removal) —
    cleared the SelectionOutline pipeline-creation blocker.
  Without those two the SEGV would still be masked by the earlier
  blockers; with those landed the SEGV is the *only* thing left.

## Required changes
- [ ] Run the reproducer with `VK_LAYER_KHRONOS_validation`
      (`-DINTRINSIC_VK_ENABLE_VALIDATION_LAYERS=ON` or equivalent
      runtime env: `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` +
      `VK_LAYER_PATH`). Capture the first validation message that
      precedes the SEGV. The barrier that crashes the driver is almost
      certainly being flagged earlier by the validation layer.
- [ ] At the SEGV site, dump `dep.pImageMemoryBarriers[i].{image,
      oldLayout, newLayout, srcStageMask, dstStageMask, srcAccessMask,
      dstAccessMask, subresourceRange.{aspectMask, baseMipLevel,
      levelCount, baseArrayLayer, layerCount}}` for every barrier in
      the batch. The crash is processing one specific barrier; isolate
      which one and tag it back to the originating render-graph pass /
      resource handle.
- [ ] If the offending barrier references an image whose
      `VkImage` is `VK_NULL_HANDLE` or whose
      `VulkanImagePool::Entry::CurrentLayout` was never initialized
      (cold-pool defaults), fix the entry creation path in
      `src/graphics/vulkan/Backends.Vulkan.ImagePool.*` / the texture
      manager so the entry is fully populated before any barrier
      references it.
- [ ] If the offending barrier has a layout transition the driver
      cannot satisfy (e.g. `UNDEFINED → SHADER_READ_ONLY_OPTIMAL`
      without prior write, or a queue-family ownership transfer with
      mismatched src/dst stages), fix either the render-graph compile
      step that emits the barrier packet (in
      `src/graphics/framegraph/`) or the renderer's barrier-packet
      translation in `Graphics.Renderer.cpp:179` (`SubmitBarrierPacket`).
- [ ] Extend `ValidateRecipeCompiledGraph` (callsite at
      `Graphics.Renderer.cpp:1363`) so it would have caught the
      malformed barrier and kept `m_LatestRecipeValidationClean = false`
      instead of letting the gate flip true with a barrier the driver
      will reject. This closes the loop so a future regression cannot
      replay the same SEGV under `IsOperational()` claiming true.

## Tests
- [ ] Restore the GRAPHICS-076 Slice D fixture's behavior by removing
      the bootstrap pre-check workaround (the test should attempt
      `engine.Run()` and either pass or fail loudly, not silently
      skip) — but only after this bug is fixed and the SEGV no longer
      reproduces. Until then, the pre-check stays in place to keep
      the GPU smoke from crashing the test runner.
- [ ] Add a render-graph contract test under
      `tests/contract/graphics/` that rebuilds the default frame
      recipe, snapshots every emitted `BarrierPacket`'s image
      transitions, and asserts no `oldLayout == UNDEFINED` for an
      image that the recipe assumes is written upstream. Or whatever
      narrower assertion captures the specific class of bug found.
- [ ] Add a regression test under `tests/regression/graphics/`
      reproducing the previously-crashing barrier sequence through the
      Null backend (the Null backend's `SubmitBarriers` is a no-op so
      it cannot crash, but the contract assertions against the
      compiled barrier packets do not need a real GPU).

## Docs
- [ ] Update `tasks/active/GRAPHICS-076-default-recipe-debug-view-and-present-wiring.md`
      to retire the `vkCmdPipelineBarrier SEGV` line from the upstream
      blocker list once the fix lands; flip the fixture's status from
      `Skipped` to `Passed` and graduate the maturity rung from
      `CPUContracted` to `Operational`.
- [ ] Mirror the same update on `GRAPHICS-077` and `GRAPHICS-078`
      Slice D status lines.
- [ ] Append a short note to `src/graphics/vulkan/README.md` describing
      the validator gap that allowed the barrier to slip through, plus
      the new validator coverage added.

## Acceptance criteria
- [ ] Running
      `ctest --test-dir build/ci-vulkan -L 'gpu' -LE 'slow|flaky-quarantine' --output-on-failure --timeout 120`
      on the same NVIDIA RTX 3050 / driver 590.48.01 host yields
      `Passed` (not `Skipped`) for
      `DefaultRecipeSurfaceGpuSmoke.RecipeSelectorReachesOperationalVulkanCommandStream`.
- [ ] `IDevice::IsOperational()` returns true on this host within the
      first `engine.Run()` frame under the default recipe.
- [ ] No regression in the existing 32/32 Vulkan smoke gate or the
      2284/2286 CPU gate (the two pre-existing failures
      `IntrinsicBenchmarkSmoke.HalfedgeSmoke.{Run,Validate}` are a
      separate test-binary-path issue and stay out of scope).
- [ ] The new contract / regression tests fail on the pre-fix code
      and pass on the post-fix code.

## Verification
```bash
# Configure (sealed-cache fast path from INFRA Option A).
cmake --preset ci-vulkan

# Build the smoke binary.
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests

# Run the default-recipe smoke directly.
build/ci-vulkan/bin/IntrinsicGraphicsVulkanSmokeTests \
    --gtest_filter='DefaultRecipeSurfaceGpuSmoke*'

# Capture validation-layer output for diagnosis.
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
    build/ci-vulkan/bin/IntrinsicGraphicsVulkanSmokeTests \
    --gtest_filter='DefaultRecipeSurfaceGpuSmoke*' 2>&1 | head -200

# Full Vulkan GPU gate (must stay green after fix).
ctest --test-dir build/ci-vulkan -L 'gpu' \
    -LE 'slow|flaky-quarantine' --output-on-failure --timeout 120

# Default CPU gate (must stay at 2284/2286 or better).
ctest --test-dir build/ci --output-on-failure \
    -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Shipping a fix without a regression test when one is feasible.
- Disabling / weakening the recipe-aware validator instead of teaching
  it to catch this class of barrier defect.
- Working around the SEGV by skipping the offending pass at runtime;
  the fix must produce a driver-valid barrier sequence so the default
  recipe runs end-to-end.

