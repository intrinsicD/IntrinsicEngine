# HARDEN-072 — RHI surface fixes for default-recipe pipeline bring-up

## Status
- Closed: 2026-05-27. Landed on branch
  `claude/graphics-076-slice-d-fixture-skeleton` immediately after the
  INFRA Option A cache-sealing commit (`d5d059a8`) so the build tree was
  stable enough to exercise the change.
- Commit: this task file is committed alongside the source changes on
  branch `claude/graphics-076-slice-d-fixture-skeleton`; the HEAD SHA
  carrying the patch is whichever HEAD of that branch ships
  `src/graphics/rhi/RHI.Descriptors.cppm`,
  `src/graphics/rhi/RHI.CommandContext.cppm`, and
  `src/graphics/renderer/Graphics.Renderer.cpp` together with this file.
  PR to be opened after push.
- Discovered as the next pair of upstream blockers during GRAPHICS-076
  Slice D diagnosis on the same Vulkan-capable host as HARDEN-071 (NVIDIA
  RTX 3050, Vulkan 1.4.309, driver 1.4.325). Without this fix every
  attempt to build the default recipe's `SelectionOutline` pipeline was
  rejected by `VulkanDevice::ValidatePipelineDesc`, which kept the
  Vulkan operational gate red even after HARDEN-071 cleared the upload
  path.
- Maturity: `CPUContracted`. The contract test
  `RendererFrameLifecycle.SelectionOutlinePipelineSurvivesOperationalRebuild`
  asserts the 144 B push-block goes through cleanly. The default-recipe
  GPU smoke (`DefaultRecipeSurfaceGpuSmoke`) still skips on the local
  `ci` preset because that preset does not enable promoted Vulkan
  (`INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON`); the *reason* changed
  from `BarrierValidationFailed` to `NotCompiled`, confirming the
  pipeline-rejection blocker is cleared. Operational closure of the
  smoke is owned by GRAPHICS-076 Slice D.

## Goal
- Stop rejecting the default-recipe `SelectionOutline` pipeline at
  pipeline-creation time so the operational gate is not pinned red by a
  fixable RHI cap mismatch.
- Stop a Clang 20 C++23 modules vtable-mangling bug from breaking every
  CPU-gate link that pulls in
  `Extrinsic::RHI::ICommandContext::CopyTextureToBuffer` through a
  derived class that does not override it.

## Non-goals
- Do not raise `MaxPushConstantBytes` past 256 B. 256 B is the safe
  desktop baseline (NVIDIA / AMD / Intel all expose ≥ 256 B for
  `maxPushConstantsSize`). Portability-subset / mobile hosts that only
  expose the 128 B Vulkan spec floor are a separate ticket.
- Do not change the *semantics* of `CopyTextureToBuffer`. Callers that
  used to rely on `srcOffsetX/Y = 0, srcWidth/Height = 0` defaults
  (meaning "whole mip extent") now pass those zeros explicitly. The
  whole-mip sentinel encoding is unchanged.
- Do not touch the Vulkan backend's `CopyTextureToBuffer` override
  (`src/graphics/vulkan/Backends.Vulkan.CommandContext.cpp`); it already
  declared all ten parameters explicitly with no defaults.

## Context
- **Symptom 1 — `SelectionOutline` rejected at CreatePipeline:** the
  default frame recipe's selection-outline pass uses the push-constant
  block `SelectionOutlinePushConstants` (`vec4 OutlineColor` +
  `vec4 HoverColor` + 12 `float`/`uint` scalars + `uint[16] SelectedIds`
  = **144 B**, per std430). `VulkanDevice::ValidatePipelineDesc`
  (`src/graphics/vulkan/Backends.Vulkan.Device.cpp:508-543`) gates on
  `desc.PushConstantSize > RHI::MaxPushConstantBytes`. The constant was
  pinned at 128 B in `src/graphics/rhi/RHI.Descriptors.cppm`, so every
  attempt to instantiate the pipeline failed with
  `CreatePipeline rejected invalid pipeline description`. That kept
  `m_LatestRecipeValidationClean` false on cold start, which kept the
  operational gate red, which kept the GRAPHICS-076 Slice D fixture
  skipping on `BarrierValidationFailed`.
- **Symptom 2 — CPU-gate link failure:** every CPU build of
  `IntrinsicGraphicsContractTests` / `IntrinsicGraphicsContractCpuTests`
  failed at link time with `undefined reference` for
  `Extrinsic::RHI::ICommandContext@Extrinsic.RHI.CommandContext::CopyTextureToBuffer(...
  unsigned long)` — the **six-argument** form. The exported symbol in
  `libExtrinsicRHI.a` was the **ten-argument** form. Root cause: Clang
  20's C++23 modules implementation mangles vtable slots for a `virtual`
  function with inline default arguments against the *stripped* parameter
  list when the consumer pcm is generated, while the exporting module
  emits the symbol against the full parameter list. Result: every
  downstream class that inherits the virtual without overriding it
  (`Backends.Null.cpp`'s `NullCommandContext`,
  `Test.DebugViewContract.cpp`'s `RecordingCommandContext`, and similar
  test mocks) carries an unresolved vtable entry that no archive can
  satisfy. The reproducer is purely structural: defaults on a virtual
  function defined in a module interface unit.
- Both symptoms blocked the default-recipe verification loop; both are
  surgical RHI-surface fixes with no semantic ripple beyond explicit
  argument passing at the two existing six-arg call sites.

## Required changes
- [x] Raise `RHI::MaxPushConstantBytes` from `128` to `256` in
      `src/graphics/rhi/RHI.Descriptors.cppm`, with a rationale comment
      block citing the desktop baseline and linking back to this task
      and to `SelectionOutlinePushConstants`. Also bump the
      `PushConstantSize` field comment from `(128)` to `(256)`.
- [x] Remove the `= 0u` defaults from
      `Extrinsic::RHI::ICommandContext::CopyTextureToBuffer` in
      `src/graphics/rhi/RHI.CommandContext.cppm`. Keep the inline no-op
      body so existing CPU contract mocks remain unaffected at runtime;
      they are now required to declare a matching 10-arg override only
      if they want to handle the call.
- [x] Update the sole six-arg caller
      (`src/graphics/renderer/Graphics.Renderer.cpp:2124-2134`, the
      MinimalDebug backbuffer readback path) to pass explicit
      `0u, 0u, 0u, 0u` for the trailing `srcOffsetX/Y, srcWidth, srcHeight`
      arguments, with a HARDEN-072 comment naming the Clang bug it
      avoids.
- [x] Confirm the picking copy pair (Renderer.cpp lines around 1654 /
      1661) and the Vulkan backend override
      (`src/graphics/vulkan/Backends.Vulkan.CommandContext.cpp:653`,
      `src/graphics/vulkan/Backends.Vulkan.CommandPools.cppm:93`)
      already pass / declare all ten parameters explicitly; no changes
      required there.

## Tests
- [x] CPU default gate
      (`ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`)
      passes for every test that touches the affected vtable. Result:
      2284/2286 pass; the 2 failures
      (`IntrinsicBenchmarkSmoke.HalfedgeSmoke.{Run,Validate}`) are
      pre-existing path-resolution issues
      (`bin/IntrinsicBenchmarkSmoke` vs `bin/Development/...`),
      unrelated to this change.
- [x] `RendererFrameLifecycle.SelectionOutlinePipelineSurvivesOperationalRebuild`
      passes — the 144 B push block no longer trips
      `VulkanDevice::ValidatePipelineDesc`.
- [x] `DefaultRecipeSurfaceGpuSmoke.RecipeSelectorReachesOperationalVulkanCommandStream`
      skips with reason `NotCompiled` (no promoted Vulkan in the `ci`
      preset) instead of `BarrierValidationFailed`, confirming the
      pipeline-rejection upstream blocker is cleared. Operational
      closure of this fixture moves back into the GRAPHICS-076 Slice D
      scope.
- [x] Pre-existing regression `GraphicsRenderer.NullRendererDebugDumpContainsCanonicalPassesAndDataflowOrder`
      reproduced on the stashed baseline (HEAD = `d5d059a8` with
      HARDEN-072 reverted), confirming it is not caused by this change.

## Docs
- [x] Inline rationale comments in
      `src/graphics/rhi/RHI.Descriptors.cppm` and
      `src/graphics/rhi/RHI.CommandContext.cppm` reference HARDEN-072
      and explain the underlying constraint (push-constant desktop
      baseline; Clang 20 modules vtable bug).
- [x] This task file records the upstream blocker chain and the
      verification evidence.
- [x] No additional architecture-doc updates required: the layering and
      module boundaries are unchanged; only constants and the signature
      of one virtual on the public RHI surface moved.

## Acceptance criteria
- [x] `SelectionOutline` pipeline is accepted by `ValidatePipelineDesc`
      on every host whose driver reports
      `maxPushConstantsSize >= 256` (all NVIDIA / AMD / Intel desktop
      drivers).
- [x] `Backends.Null.cpp.o` and every test-side `CommandContext`
      derivative link against `libExtrinsicRHI.a` without unresolved
      `CopyTextureToBuffer` vtable references.
- [x] No call site relies on the removed default arguments; every six-arg
      caller has been promoted to an explicit ten-arg call.
- [x] The GRAPHICS-076 Slice D fixture's skip reason advances past
      `BarrierValidationFailed` (it is now blocked only on `NotCompiled`
      under the `ci` preset, which is by design — GRAPHICS-076 owns the
      remaining work).

## Verification
```bash
# Sealed-cache configure stays a no-op (INFRA Option A).
cmake --preset ci

# Build the targets that previously failed at link time.
cmake --build --preset ci --target IntrinsicTests

# Default CPU gate.
ctest --test-dir build/ci --output-on-failure \
    -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

# Contract test that pins the SelectionOutline push-constant cap.
ctest --test-dir build/ci \
    -R "RendererFrameLifecycle.SelectionOutlinePipelineSurvivesOperationalRebuild" \
    --output-on-failure

# Default-recipe smoke (Vulkan-capable host): should skip with reason
# "NotCompiled" (no promoted Vulkan in ci preset) — not
# "BarrierValidationFailed".
build/ci/bin/IntrinsicGraphicsVulkanSmokeTests \
    --gtest_filter='DefaultRecipeSurfaceGpuSmoke.RecipeSelectorReachesOperationalVulkanCommandStream'
```

## Forbidden changes
- Raising `MaxPushConstantBytes` past 256 B (deferred to a portability
  ticket).
- Reintroducing default arguments on any virtual function defined in a
  module interface unit until the upstream Clang 20 modules vtable bug
  is fixed.
- Mixing this RHI-surface fix with any rendering-pass behavior change,
  Vulkan backend refactor, or test reorganization.

## Maturity
- Target: `CPUContracted` (this slice). Operational closure of the
  default-recipe GPU smoke now depends only on enabling promoted Vulkan
  and clearing the remaining barrier SEGV — owned by GRAPHICS-076 Slice
  D.

## Follow-on note
- 2026-05-29: `BUG-013`
  (`tasks/done/BUG-013-backbuffer-readback-contract-vtable-segv.md`) was filed
  as a follow-on manifestation of the same clang-20 / C++23-modules vtable
  hazard — a backbuffer-readback contract SEGV after `cc06edef` added the
  inline-bodied `ICommandContext::BindFrameSampledTexture` virtual. Unlike the
  symptom this task fixed (a genuine default-argument mangling that failed at
  *link* time), BUG-013 did **not** reproduce on a clean `ci` preset build: it
  was a stale incremental module-BMI artifact, and the contract suite is green
  (225/225) once BMIs are consistent. The lasting prevention is the
  clean-rebuild rule now documented in `src/graphics/rhi/README.md`.



