# GRAPHICS-076 — Default-recipe `Pass.DebugView` and canonical `Pass.Present` wiring

## Status

- Status: done. Slices A–D landed; Slice D recipe-selector smoke now passes
  instead of `GTEST_SKIP` on the Vulkan-capable host after the 2026-05-27
  BUG-012 command-stream fixes. Maturity reached: `Operational` for the
  default-recipe command-stream / recipe-selector smoke on Vulkan-capable hosts,
  with CPU/null contracts preserving the default gate. The default-recipe
  pixel-readback parity harness is explicitly deferred to
  [`GRAPHICS-076E`](GRAPHICS-076E-default-recipe-pixel-readback.md)
  rather than silently expanding this Slice D fixture into a renderer API change.
- Completed: 2026-05-28. Final graduation commit: `37c53a94`
  (`GRAPHICS-076: graduate default-recipe Vulkan smoke`); retirement file move
  recorded in the follow-up task-retirement commit.
- Owner/agent: local agent workflow.
- Branch: local `main` after merge `fb81eb95`.
- Started: 2026-05-23. Landed slices:
  - Slice A: PR #921 on `claude/intrinsicengine-agent-onboarding-GdEzP`
    (commits `203d4c4` + Slice A follow-up `22cbbe9` via PR #922 on
    `claude/present-draw-render-pass-AUIdC`, merge `1131435`).
  - Slice B: PR #923 on `claude/intrinsicengine-agent-onboarding-cp7C2`
    (commit `21be263`, merge `ddedfb4`).
  - Slice C: PR #924 on `claude/intrinsicengine-agent-onboarding-wImXR`
    (commit `e33c9b3`, merge `9ccc42c`).
  - Slice D (fixture skeleton + bootstrap pre-check + build
    wiring): on the current
    `claude/intrinsicengine-agent-onboarding-*` branch (2026-05-26).
- Completion verification: 2026-05-28
  verification completed under `ci` / `ci-vulkan` trees configured with
  `/usr/bin/clang-22` / `clang-scan-deps-22`: focused `DefaultRecipeSurfaceGpuSmoke`
  passed normally (not skipped), full opt-in GPU smoke passed 4/4, focused
  `contract;graphics` CPU/null tests passed 253/253, and the default CPU gate
  passed 2297/2297 after building `IntrinsicBenchmarkSmoke`.

## Nonblocking clarification (2026-05-23)

- Slices A–C are fully landed via PRs #921, #922 (Slice A follow-up),
  #923, and #924. The only remaining slice is the opt-in `gpu;vulkan`
  default-recipe smoke (Slice D), which cannot run in a sandbox that
  lacks a Vulkan ICD (`/dev/dri` absent, `vkCreateInstance` returns
  `ERROR_INCOMPATIBLE_DRIVER`). The task is parked here rather than
  retired so it stays visible to the next agent on a host with a real
  Vulkan device; CPU-only environments should pick the next earliest
  unblocked Theme A leaf from `tasks/backlog/README.md` rather than
  scaffolding Slice D without the ability to verify it.
- The `GRAPHICS-081` scaffold-retirement obligation cannot be unblocked
  until Slice D is green on a Vulkan-capable host (see the
  "Scaffold-retirement obligation" callout below).

## Nonblocking clarification (2026-05-26)

- Slice D fixture file `tests/integration/graphics/Test.DefaultRecipeSurfaceGpuSmoke.cpp`
  has now landed as the recipe-selector leg (mirrors
  `MinimalDebugSurfaceGpuSmoke.RecipeSelectorReachesOperationalVulkanCommandStream`).
  Wired into `GraphicsVulkanSmokeTestObjs` /
  `IntrinsicGraphicsVulkanSmokeTests` under labels `gpu;vulkan;graphics`.
  Verified on a Vulkan-capable host (NVIDIA RTX 3050,
  Vulkan instance 1.4.309, driver 1.4.325) with the `ci-vulkan`
  preset: full opt-in `gpu;vulkan` suite is 32/32 green
  (`ctest --test-dir build/ci-vulkan -L gpu -LE 'slow|flaky-quarantine'`),
  default CPU gate is 2286/2286 green
  (`ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine'`).
- The fixture currently SKIPS on the same Vulkan-capable host because
  the default recipe does not yet reach the operational Vulkan gate
  during `Engine::Initialize()`. Reproducible symptoms observed in
  the smoke-binary stderr stream:
    - `[WARN] [Runtime] VulkanRequestedButNotOperational status=RequestedButIncompleteGate reason=BarrierValidationFailed`
    - `[WARN] [VulkanTransferQueue] UploadTexture rejected; destination texture handle is invalid`
    - `[WARN] [Graphics] PostProcess.SMAA.AreaTex upload rejected; lease released, will retry on next Initialize.`
    - `[WARN] [VulkanDevice] CreatePipeline rejected invalid pipeline description`
    - `[WARN] [Graphics] SelectionOutline pipeline unavailable; default-recipe selection outline recording will be skipped: error=403`
    - Result post-`Run()`: `status=RequestedButFailedInit reason=DeviceLost`
  Additionally, LeakSanitizer reports a 12640-byte leak from
  `Backends::Vulkan::VulkanCommandContext::PushConstants` reached via
  `Graphics::CullingSystem::DispatchCull` → `NullRenderer::RecordCullingPass`
  when the device fails mid-frame, which the fixture's pre-`Run()`
  operational-gate check now avoids by skipping at the
  bootstrap-helper before `engine.Run()` is called.
- Status implication for `GRAPHICS-081`: the scaffold-retirement
  obligation remains gated. Even on a Vulkan-capable host the default
  recipe currently has no operational frame, so the equivalent
  pixel-readback parity coverage that GRAPHICS-081 needs cannot be
  produced today. The unblock requires upstream Vulkan bring-up work
  on (a) SMAA AreaTex upload, (b) SelectionOutline pipeline creation,
  (c) the barrier validator that gates the operational promotion.
  These belong in new tickets owned by `graphics/vulkan` and
  `graphics/renderer`, not in this slice.
- What this slice ships:
  - The fixture skeleton (`Test.DefaultRecipeSurfaceGpuSmoke.cpp`)
    with a structured `GTEST_SKIP` reason that names the upstream
    blockers and points at this clarification.
  - Test-list wiring in `tests/CMakeLists.txt` so the fixture builds
    and registers on the `ci-vulkan` preset.
  - Bootstrap pre-check of the operational gate so the smoke skips in
    ~2 seconds on hosts where the gate is red, instead of paying the
    ~35-second `vkWaitForFences`/`DeviceLost` wall-time penalty.
  - The recipe-selector assertion shape (Operational status, Compile/
    Execute succeeded, `CommandRecords["Present"]` recorded, MinimalDebug
    counters all zero, Vulkan fallback / init / validation / gate
    counters stable across the operational frame).
- What this slice does NOT ship and why:
  - The four-sample-point pixel-readback parity portion of the
    Slice D "Required changes" list. The renderer's existing readback
    seam (`SetMinimalDebugBackbufferReadbackBuffer(...)`,
    `MinimalDebugBackbufferReadbackCopyCount`) is gated to
    `FrameRecipeKind::MinimalDebug` in `Graphics.Renderer.cpp` around
    the `m_FrameRecipe == FrameRecipeKind::MinimalDebug` check on the
    `Present → TransferSrc → CopyImageToBuffer → Present` triplet, so
    generalising to the default recipe is a renderer-API change
    (separate setter or recipe-agnostic rename, plus a sibling
    `DefaultRecipeBackbufferReadbackCopyCount` for diagnostic
    clarity, plus contract-test updates) that is meaningfully larger
    than a test-fixture slice. It is also moot until the default
    recipe reaches operational — the readback hook cannot run on a
    non-operational frame. A follow-up slice (Slice E, or a
    dedicated `GRAPHICS-076E-default-recipe-pixel-readback` task) can
    layer the API + assertion on top of the fixture shipped here once
    the upstream bring-up issues above are resolved.

## Nonblocking clarification (2026-05-26, follow-up)

- Upstream root cause for the SMAA / texture-upload portion of the
  bring-up blockers above turned out to be an RHI-side handle-identity
  bug in `RHI::TextureManager` and `RHI::BufferManager`: both managers
  published their own manager-local pool handle as the lease handle
  rather than the `IDevice`-issued handle, so every
  `IDevice::GetTransferQueue().UploadTexture()` /
  `UploadBuffer()` call was silently rejected by the Vulkan backend's
  `m_Images.GetIfValid(handle)` / `m_Buffers.GetIfValid(handle)` lookup.
  Fix landed as a sibling slice on the same branch and is recorded
  under `tasks/archive/HARDEN-071-rhi-manager-handle-identity.md`.
- Effect on this fixture: SMAA AreaTex / SearchTex uploads now succeed
  silently on the same host. The default recipe progresses past the
  upload-rejection step into actual frame execution. The fixture still
  skips with a structured reason on this host because the *next*
  upstream blockers remain:
    - `[VulkanDevice] CreatePipeline rejected invalid pipeline description`
      → SelectionOutline pipeline unavailable (error 403).
    - When the smoke is allowed to proceed past Initialize regardless,
      `vkCmdPipelineBarrier` SEGVs inside the NVIDIA driver
      (`libnvidia-glcore.so.590.48.01+0xe8251d`) reached via
      `Backends::Vulkan::VulkanCommandContext::SubmitBarriers`.
  These two are real subsequent bring-up tickets owned by
  `graphics/vulkan` / `graphics/renderer` (see HARDEN-071 Follow-ups
  section). The Slice D fixture-skeleton + RHI-fix combination is the
  current best-available CPU-side evidence; flipping the fixture from
  Skipped to Passed requires those upstream tickets to land.
- Verification on this branch (post-fix): `ctest --test-dir build/ci-vulkan
  -L gpu -LE 'slow|flaky-quarantine' --output-on-failure --timeout 120`
  → 32/32 (DefaultRecipeSurfaceGpuSmoke still Skipped, baseline
  MinimalDebug smokes pass with no upload-rejection warnings); CPU gate
  2286/2286.

## Nonblocking clarification (2026-05-27)

- Re-verified on the same NVIDIA RTX 3050 / Vulkan 1.4.309 / driver
  590.48.01 host after HARDEN-071 (RHI handle identity) and HARDEN-072
  (RHI push-constant cap + virtual default-arg removal) landed on
  `main`:
    - The `SMAA AreaTex upload rejected` warning is gone (HARDEN-071
      cleared the upload-identity bug).
    - The `SelectionOutline pipeline unavailable; error=403` warning
      is gone (HARDEN-072 raised `RHI::MaxPushConstantBytes` to 256 B
      and `RendererFrameLifecycle.SelectionOutlinePipelineSurvivesOperationalRebuild`
      passes).
    - When the cold-gate bootstrap pre-check in this fixture is
      temporarily bypassed and `engine.Run()` is allowed to proceed,
      the only remaining failure is the `vkCmdPipelineBarrier2` SEGV
      inside `libnvidia-glcore.so.590.48.01+0xe8251d` reached via
      `VulkanCommandContext::SubmitBarriers`
      (`src/graphics/vulkan/Backends.Vulkan.CommandContext.cpp:574`).
- At this point, the barrier defect was the next actionable upstream
  blocker for graduating this fixture from `Skipped` to `Passed`. It is captured as
  [`BUG-012`](BUG-012-default-recipe-vkcmdpipelinebarrier2-segv-nvidia.md)
  with a full reproducer, stack trace, and acceptance criteria. The
  later 2026-05-27 BUG-012 partial update below supersedes the
  "only remaining" wording by recording the newly exposed
  pipeline-layout and command-buffer lifetime blockers.

## Nonblocking clarification (2026-05-27, BUG-012 partial)

- This session worked the active GRAPHICS-076 Vulkan blocker through
  `BUG-012` and landed CPU-visible pieces that remove the first malformed
  default-recipe barrier class:
  - `RHI::MemoryAccess` now includes color/depth attachment access scopes, and
    `Graphics.Renderer` populates texture barrier access masks before submitting
    framegraph barrier packets to RHI.
  - The renderer now replaces compiled graph synthetic transient handles with
    real per-frame RHI texture/buffer allocations, cached by frame slot and
    descriptor, before barrier submission or render-pass setup. This prevents
    `TextureHandle{1,1}`-style framegraph handles from colliding with live Vulkan
    swapchain/color-image handles.
  - Vulkan transfer uploads no longer record final-layout barriers with shader
    destination stages on transfer-only command buffers.
  - Added CPU contracts:
    `RHICommandContext.MemoryAccessCombinesAttachmentBitsWithoutTruncation` and
    `FrameRecipeContract.DefaultRecipeDoesNotDepthTransitionColorResources`.
- Verification from this session:
  - `ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
    → 252/252 passed.
  - `INTRINSIC_DEFAULT_RECIPE_SMOKE_BYPASS_COLD_GATE=1 VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation build/ci-vulkan/bin/IntrinsicGraphicsVulkanSmokeTests --gtest_filter='DefaultRecipeSurfaceGpuSmoke*'`
    no longer reports the original `VUID-VkImageMemoryBarrier2-oldLayout-01209`
    color-image→depth-layout barrier. It still fails later with default-recipe
    pipeline-layout validation errors and command-buffer invalidation
    (`vkBeginCommandBuffer-commandBuffer-00049`, plus validation reporting a
    destroyed bound buffer) before the NVIDIA driver SEGV.
- Status implication: keep the normal Slice D fixture pre-check/`GTEST_SKIP` in
  place. GRAPHICS-076 remains `CPUContracted` on default gates and not yet
  `Operational`; follow-up Vulkan work must address the pipeline-layout and
  command-buffer lifetime failures now exposed by the barrier/handle fixes.

## Nonblocking clarification (2026-05-27, BUG-012 follow-up)

- This session continued the bypassed default-recipe Vulkan smoke on the same
  NVIDIA/validation host and removed the shader-module / pipeline-layout
  validation blockers that previously hid the command-stream lifetime failure:
  - Vulkan logical-device feature negotiation now requires/enables
    `geometryShader`, `runtimeDescriptorArray`, and
    `shaderSampledImageArrayNonUniformIndexing`, matching the SPIR-V
    capabilities emitted by the current selection/bindless shader set.
  - Retained line/point default-recipe pipeline descriptors now use the
    GpuScene-aware `forward/line.*` and `forward/point.*` shader pairs; those
    shaders no longer declare the legacy camera UBO descriptor and follow the
    same BDA-only clip-space convention as `forward/default_debug_surface.vert`.
  - Postprocess, `DebugView`, and selection-outline sampled resources were
    reshaped to the promoted Vulkan global sampled-texture layout
    (`set=0,binding=0` runtime arrays) so the default pipeline set no longer
    emits `VkGraphicsPipelineCreateInfo-layout-07988/07990` errors.
  - `post_histogram.comp` no longer declares the unmatched descriptor-backed
    storage buffer during bring-up; the renderer still clears/copies the RHI
    histogram buffer through command-context operations, but real shader-side
    bin accumulation needs a follow-up storage-buffer/BDA descriptor seam before
    pixel/metric correctness can be claimed.
  - Vulkan deferred deletion now queues live resources behind the frame slot
    fence even while the operational gate is transitioning, and the graphics
    command context defensively resets its command buffer before recording.
- Verification/evidence:
  - `INTRINSIC_DEFAULT_RECIPE_SMOKE_BYPASS_COLD_GATE=1 VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation build/ci-vulkan/bin/IntrinsicGraphicsVulkanSmokeTests --gtest_filter='DefaultRecipeSurfaceGpuSmoke*'`
    no longer reports shader-module capability errors or pipeline-layout
    descriptor mismatch errors before frame execution. The remaining first
    failure is command-buffer lifetime/resource-lifetime validation:
    `vkBeginCommandBuffer-commandBuffer-00049` followed by commands rejected
    because a bound `VkBuffer` was destroyed, then the same NVIDIA
    `vkCmdPipelineBarrier2` driver SEGV. This is narrower than the prior
    pipeline-layout + command-buffer invalidation combination but still blocks
    graduating the fixture.
  - Normal skip-safe fixture run (without the bypass) remains safe:
    `build/ci-vulkan/bin/IntrinsicGraphicsVulkanSmokeTests --gtest_filter='DefaultRecipeSurfaceGpuSmoke*'`
    → 1 skipped in ~2s with the structured operational-gate reason.
  - CPU contract gate remains green:
    `ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
    → 356/356 passed.
- Status implication: GRAPHICS-076 is still not `Operational`; the next Vulkan
  slice should focus on the destroyed-bound-buffer / command-buffer lifetime
  path now that shader/pipeline layout validation no longer masks it. The
  temporary shader-side sampled-resource/histogram compromises above must be
  replaced by real bindless indices or a storage-buffer/BDA seam before
  four-sample pixel parity or histogram correctness can be claimed.

## Nonblocking clarification (2026-05-27, BUG-012 command-stream follow-up)

- This session continued from the narrowed command-buffer/resource-lifetime
  blocker and got the diagnostic default-recipe Vulkan smoke through the real
  command stream on the NVIDIA/validation host when the cold-gate pre-check is
  bypassed:
  - `VulkanDevice::BeginOneShot()` no longer reuses the per-frame graphics
    command buffer for synchronous staging uploads. It owns a dedicated transient
    command pool/buffer and serializes worker-thread `WriteBuffer()` staging
    uploads through a one-shot mutex.
  - Promoted Vulkan now requires/enables `drawIndirectCount`, matching the
    default renderer's `vkCmdDrawIndirectCount` / `vkCmdDrawIndexedIndirectCount`
    command shape.
  - The default recipe now declares dynamic-rendering scopes for graphics draw
    passes (`DepthPrepass`, picking, shadow, surface, line, point, transient
    debug, visualization overlay, postprocess, AA, selection outline, debug
    view, and present), and `NullRenderer::ExecuteFrame()` resolves compiled
    pass names by pass index instead of accidentally remapping them through
    topological order.
  - `PostProcessPass` opens the LDR tone-map attachment as the active render
    target in the current recorded path; `BloomScratch` remains declared for the
    resource contract, but the bloom multi-target render-scope split remains
    future cleanup before bloom correctness can be claimed.
  - `lsan.supp` now covers two Vulkan ICD allocations that surface through VMA
    buffer binding and `vkCmdPushConstants` during the ASan Vulkan smoke.
- Verification/evidence:
  - `LSAN_OPTIONS=suppressions=/home/alex/Documents/IntrinsicEngine/lsan.supp INTRINSIC_DEFAULT_RECIPE_SMOKE_BYPASS_COLD_GATE=1 VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation ctest --test-dir build/ci-vulkan --output-on-failure -R 'DefaultRecipeSurfaceGpuSmoke' --timeout 60`
    → 1/1 passed; no Vulkan validation errors before completion.
  - `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
    → 2242/2242 passed.
  - Added `FrameRecipeContract.DefaultRecipeDrawPassesDeclareRenderPassAttachments`
    so default-recipe graphics draw passes cannot silently compile without a
    dynamic-rendering attachment scope again.
- Status implication: the BUG-012 command-stream blocker is no longer reproduced
  under the bypassed fixture. GRAPHICS-076 still should not be declared closed
  until Slice D intentionally removes or revises the cold-gate pre-check and, if
  needed, extends the default-recipe pixel/readback parity harness beyond the
  current recipe-selector proof.

## Nonblocking clarification (2026-05-28, Slice D graduation)

- Slice D now removes the default-recipe cold operational pre-check and the
  `INTRINSIC_DEFAULT_RECIPE_SMOKE_BYPASS_COLD_GATE` diagnostic bypass from
  `tests/integration/graphics/Test.DefaultRecipeSurfaceGpuSmoke.cpp`. Host
  capability failures still skip at the GLFW / logical-device / swapchain /
  command-sync readiness checks, but once those pass a default-recipe
  non-operational result after `engine.Run()` is a real test failure.
- Focused verification on this host:
  `LSAN_OPTIONS=suppressions=/home/alex/Documents/IntrinsicEngine/lsan.supp ctest --test-dir build/ci-vulkan --output-on-failure -R 'DefaultRecipeSurfaceGpuSmoke' --timeout 120`
  → 1/1 passed; the fixture reports `Passed`, not `Skipped`.
- Full verification follow-up on the same host:
  `LSAN_OPTIONS=suppressions=/home/alex/Documents/IntrinsicEngine/lsan.supp ctest --test-dir build/ci-vulkan -L 'gpu' -LE 'slow|flaky-quarantine' --output-on-failure --timeout 120`
  → 4/4 passed, and
  `ctest --test-dir build/ci --output-on-failure -L 'contract' -L 'graphics' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  → 253/253 passed, and
  `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  → 2297/2297 passed after explicitly building the benchmark smoke executable
  referenced by the first two CTest entries.
- Acceptance decision for this slice: the recipe-selector command-stream proof
  is the `GRAPHICS-076` Slice D closure gate. The four-sample default-recipe
  pixel-readback parity harness remains valuable, but requires a renderer API /
  diagnostic-counter extension beyond this fixture. It is tracked explicitly as
  [`GRAPHICS-076E`](GRAPHICS-076E-default-recipe-pixel-readback.md)
  instead of remaining as an unchecked item inside this task.

## Slice plan

The task spans canonical `Pass.Present` wiring, canonical `Pass.DebugView`
wiring, a render-graph validation negative test, and the
scaffold-retirement-obligation `gpu;vulkan` smoke for the default recipe.
Each slice is independently reviewable and preserves the CPU/null
correctness gate; only Slice D exercises an opt-in `gpu;vulkan` smoke.

- **Slice A (this slice).** Canonical `Pass.Present` operational wiring.
  Adds `m_PresentPass` + `m_PresentPipelineLease` to `NullRenderer`,
  introduces a dedicated `BuildPresentPipelineDesc()` built around new
  `assets/shaders/present.{vert,frag}` (fullscreen-triangle copy of
  `FrameRecipe.PresentSource`), wires the executor `"Present"` branch to a
  new `RecordPresentPass(...)` helper, and adds `contract;graphics` tests
  asserting `BindPipeline(present) + Draw(3, 1, 0, 0)` and the
  `SkippedUnavailable` taxonomy when the pipeline lease is missing.
  Updates `Test.RendererFrameLifecycle.cpp` to move `"Present"` from
  `kSoftSkippedPasses` to `kRoutedPasses` and bumps the lifecycle
  `BindPipelineCalls` count. Defers `Pass.DebugView` wiring to Slice B,
  the non-present `Backbuffer` write negative test to Slice C, and the
  `gpu;vulkan` smoke to Slice D.
- **Slice B.** Canonical `Pass.DebugView` operational wiring. Adds
  `m_DebugViewPass` + `m_DebugViewPipelineLease` (built from
  `assets/shaders/debug_view.{vert,frag}`), wires the executor
  `"DebugViewPass"` branch, and adds `contract;graphics` tests covering
  the BindPipeline + Draw shape, push-constant builder, and the
  deterministic fallback when `DebugViewSettings::RequestedResourceName`
  resolves to a non-previewable resource. Preserves CPU gate.
- **Slice C.** Render-graph validation negative test. Adds a
  `contract;graphics` test asserting that a non-present write to the
  imported `Backbuffer` produces a `RenderGraphValidationResult` finding
  rather than silent success. Does not change runtime behavior beyond
  what the existing validator already enforces. Preserves CPU gate.
- **Slice D.** Default-recipe equivalent of the `GRAPHICS-033D`
  `gpu;vulkan` visible-triangle smoke (scaffold-retirement obligation).
  Re-uses the GRAPHICS-033D bounded `engine.Run()` driver helper to
  exercise the default recipe's `Pass.Forward.Surface → … → Pass.Present`
  chain through real Vulkan, asserts the same four-sample-point /
  zero-fallback-counter invariants, and is labelled
  `gpu;vulkan;graphics`. Unblocks `GRAPHICS-081` deletion of the
  `MinimalDebugSurface` scaffold by providing equivalent `gpu;vulkan`
  coverage on the canonical default recipe.

## Maturity

- Target: `Operational` on Vulkan-capable hosts (Slice D);
  `CPUContracted` on every host (Slices A–C).
- Slice A closes `Scaffolded → CPUContracted` for canonical `Pass.Present`.
  The `Operational` claim across both canonical passes is owned jointly
  by Slice D plus the GRAPHICS-081 scaffold-deletion gate.

## Goal
- Wire `Pass.DebugView` and the canonical `Pass.Present` (`src/graphics/renderer/Passes/Pass.Present.cpp:14`) into the renderer executor under the default recipe per `GRAPHICS-013B`/`013BQ` and the present contract from `GRAPHICS-013CQ`. Pipelines created at renderer init / `RebuildOperationalResources()`; executor branches added.

## Non-goals
- No ImGui overlay (`GRAPHICS-079`).
- No buffer-class debug visualization (out-of-scope per `GRAPHICS-013BQ`).
- No backend-native swapchain blit/copy fast path (rejected per `GRAPHICS-013CQ` for the contract finalization form).

> **Scaffold-retirement obligation.** Fulfilled by [`GRAPHICS-081`](GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md) on 2026-06-02. `GRAPHICS-076` authored the **default-recipe equivalent** of the `GRAPHICS-033D` `gpu;vulkan` visible-triangle smoke, and `GRAPHICS-081` deleted the bootstrap recipe fixture without reducing `gpu;vulkan` coverage.

## Context
- Status: Slices A–C landed; Slice D blocked on Vulkan-capable host (see `## Status`).
- Owner/layer: `graphics/renderer`.
- Planning anchors: `tasks/archive/GRAPHICS-013B-debug-view-and-render-target-inspection.md`, `tasks/archive/GRAPHICS-013BQ-debug-view-backend-clarifications.md`, `tasks/archive/GRAPHICS-013C-imgui-overlay-and-present.md`, `tasks/archive/GRAPHICS-013CQ-imgui-present-backend-clarifications.md`.
- Today: `Pass.DebugView.cpp` and `Pass.Present.cpp` exist as shells (Pass.Present has the `BindPipeline` + `Draw(3,1,0,0)` body but is not owned by `NullRenderer`); the executor lambda has no branches for either pass.
- The minimal-debug present (`Pass.Present.MinimalDebug` from `GRAPHICS-032C`) shares the fullscreen-triangle shape but exists as a separate class so the minimal recipe stays self-contained. Slice A keeps both classes side-by-side; GRAPHICS-081 deletes the minimal scaffold after Slice D ships the `gpu;vulkan` smoke.

## Required changes

Slice A (this slice):
- [x] Add a new fullscreen-triangle present shader pair
      `assets/shaders/present.vert` + `assets/shaders/present.frag` that
      samples `FrameRecipe.PresentSource` and writes to the imported
      backbuffer LDR target. No push constants.
- [x] Add `m_PresentPass` (`PresentPass`) and `m_PresentPipelineLease`
      members to `NullRenderer`; emplace/reset alongside the existing
      `m_MinimalDebugPresentPass` / `m_ForwardSurfacePass` patterns so a
      failed `Create()` leaves the pass in the fail-closed
      `SkippedUnavailable` state.
- [x] Introduce a static `BuildPresentPipelineDesc(colorFormat)` helper
      mirroring `BuildMinimalVisibleTrianglePipelineDesc(...)` but pointed
      at the new `present.{vert,frag}` shader pair and pinned to the
      backbuffer format default with `PushConstantSize = 0`,
      `DepthStencil.DepthTestEnable = false`, `Rasterizer.Culling = None`.
- [x] In `InitializeOperationalPassResources(device)`, reset the present
      lease, create the pipeline via
      `PipelineManager::Create(BuildPresentPipelineDesc(m_BackbufferFormat))`,
      and call `m_PresentPass.SetPipeline(...)`. Republish byte-identical
      from `RebuildOperationalResources()` so the dedupe registry returns
      the same device handle.
- [x] Add `RecordPresentPass(graphicsContext)` mirroring
      `RecordMinimalDebugPresentPass`: returns `SkippedNonOperational`
      when device is not operational, `SkippedUnavailable` when the
      pipeline lease is missing, otherwise calls
      `m_PresentPass.Execute(cmd)` and returns `Recorded`.
- [x] Add executor branch `else if (passName == std::string_view{"Present"})`
      routing to `RecordPresentPass(...)`. Branch placement: between the
      existing `"PostProcessAAResolvePass"` branch and the
      `kMinimalDebugPresentPassName` branch so the default-recipe and
      minimal-recipe present executors stay textually adjacent.
- [x] Update the existing renderer-frame-lifecycle assertions in
      `Test.RendererFrameLifecycle.cpp` to reflect `"Present"` now
      reporting `Recorded` under the default recipe and the
      `BindPipelineCalls` count incrementing by one (no new push
      constants — present pipeline carries a zero-byte push range).
- [x] Slice A follow-up: wire the recipe-side `"Present"` node as a
      real color-attachment pass. The initial Slice A landing only
      declared `Read(backbuffer, TextureUsage::Present)` +
      `SideEffect()` on the present pass, so the framegraph compiler
      emitted zero `CompiledRenderPassAttachment` entries for it,
      `BuildActiveRenderPassDesc` reported `HasAttachments = false`,
      and the executor issued `RecordPresentPass(...)`'s
      `BindPipeline + Draw(3, 1, 0, 0)` outside any render pass —
      invalid command-buffer usage on Vulkan that would surface as a
      validation error and a missing final blit to the backbuffer.
      `Graphics.FrameRecipe.cpp::BuildDefaultFrameRecipe` now declares
      `Write(backbuffer, TextureUsage::ColorAttachmentWrite)` +
      `SetRenderPass(...)` on the canonical `"Present"` node
      (mirroring the `Pass.Present.MinimalDebug` finalizer), and
      `DescribeDefaultFrameRecipe` lists `"Backbuffer"` under the
      present pass's writes rather than reads. The contract-test
      assertions in `Test.FrameRecipeContract.cpp` and
      `Test.ImGuiPresentContract.cpp` are updated to match.

Slice B (canonical `Pass.DebugView`, this slice):
- [x] Add `m_DebugViewSystem` (`std::optional<DebugViewSystem>`),
      `m_DebugViewPass` (`std::optional<DebugViewPass>`, bound to
      `*m_DebugViewSystem`), and `m_DebugViewPipelineLease` members
      with `Initialize(device)` emplacement and `Shutdown()` teardown
      mirroring the system-bound-pass patterns from
      `m_ForwardSurfacePass` / `m_PostProcessToneMapPass`.
- [x] Add `BuildDebugViewPipelineDesc()` pointing at
      `assets/shaders/debug_view.{vert,frag}` (graphics pipeline form
      chosen as the canonical CPU-contracted form). Pinned to
      `RGBA8_UNORM` color target (the recipe's `DebugViewRGBA`
      attachment format) and `PushConstantSize = sizeof(DebugViewPushConstants)`
      (16 bytes, matching the canonical 4-`uint32` packing from
      GRAPHICS-013BQ §"Shader visualization modes"). Created last in
      `InitializeOperationalPassResources()` (call #25, immediately
      after present at #24) so upstream `FailPipelineCreateCall`
      indices stay stable.
- [x] Wire the executor `"DebugViewPass"` branch through
      `RecordDebugViewPass(graphicsContext, camera)` with the
      `Recorded` / `SkippedNonOperational` / `SkippedUnavailable`
      taxonomy. Each frame `ExecuteFrame()` drives
      `m_DebugViewSystem->SetSettings({.Enabled = world.DebugOverlayEnabled ||
      world.DebugPrimitives.HasTransientDebug, ...})` followed by
      `ResolveSelection(recipeIntrospection)` immediately after the
      recipe introspection is built; the resolved selection's
      `UsedFallback` increments `RenderGraphFrameStats::DebugViewFallbackInvocationCount`
      whenever debug view is enabled and the requested resource
      resolved through the fallback path. The `Recorded` path also
      increments `DebugViewPassExecutions`.
- [x] Update `assets/shaders/debug_view.frag` push block from the
      legacy `(int Mode, float DepthNear, float DepthFar)` 12-byte
      layout to the canonical `(uint ResourceKind, uint ResourceClass,
      uint UsedFallback, uint Reserved)` 16-byte layout, deriving the
      visualization path from `ResourceClass` per the GRAPHICS-013BQ
      decision (no user-selectable mode field). Per-class pixel
      correctness on a real Vulkan device is owned by a follow-up
      operational slice; the CPU/null gate only validates the command
      shape + `PushConstantSize = 16u`.
- [x] Add `DebugViewPass::GetPipeline()` accessor mirroring
      `PresentPass::GetPipeline()` / `MinimalDebugPresentPass::GetPipeline()`
      so the renderer's fail-closed `RecordDebugViewPass` prerequisite
      check observes the same shape on every canonical default-recipe
      path.
- [x] Add `IRenderer::SetDebugViewRequestedResourceName(...)` /
      `GetDebugViewRequestedResourceName()` public seam so runtime
      and contract tests can drive `RequestedResourceName` (the
      `Enabled` field stays renderer-driven from world state, so a
      stale `Enabled = true` cannot keep the pass live across
      overlay-off frames). Per GRAPHICS-013BQ §"UI-name to
      FrameRecipeIntrospection mapping", runtime translates editor
      strings to canonical resource names before calling this seam.

Slice C (render-graph validation negative test, this slice):
- [x] Add `RenderGraphValidation.CompileBackbufferWrittenByNonFinalizerReportsStructuredFinding`
      to `tests/contract/graphics/Test.RenderGraphValidation.cpp`,
      exercising `RenderGraphCompiler::Compile(...)` end-to-end with an
      imported `Backbuffer` written by both a non-finalizer
      `"EarlyComposite"` pass and the canonical finalizer `"Present"`
      pass. Asserts that exactly one `BackbufferWrittenByNonFinalizer`
      error finding is produced, attributed to the non-finalizer pass,
      and that the finding is mirrored on both
      `compiled->ValidationFindings` and
      `RenderGraphCompiler::GetLastCompileValidationResult()`. Pins the
      compile-path leg of the contract that
      `ImportedBackbufferNonFinalizerWriteReportsError` pins at the
      `ValidateCompiledGraph`-direct level.

Slice D (default-recipe `gpu;vulkan` visible-triangle smoke, fixture
skeleton landed 2026-05-26; graduate-from-SKIP gated on upstream
default-recipe Vulkan bring-up):
- [x] Add `tests/integration/graphics/Test.DefaultRecipeSurfaceGpuSmoke.cpp`
      under `gpu;vulkan;graphics` labels with the recipe-selector
      assertion shape (Operational status, Compile/Execute succeeded,
      `CommandRecords["Present"]` recorded, MinimalDebug counters
      all zero, Vulkan fallback / init / validation / gate counters
      stable across the operational frame). Pre-checks the operational
      gate immediately after `Engine::Initialize()` and emits a
      structured `GTEST_SKIP` reason when the gate is red so the
      fixture skips in ~2 seconds on bring-up-incomplete hosts rather
      than paying the ~35-second `vkWaitForFences`/`DeviceLost`
      wall-time penalty.
- [x] Wire `Test.DefaultRecipeSurfaceGpuSmoke.cpp` into
      `_graphics_vulkan_smoke_test_files` in `tests/CMakeLists.txt`
      so it builds into `IntrinsicGraphicsVulkanSmokeTests` with the
      sibling `gpu vulkan graphics` labels.
- [x] Graduate the fixture from `Skipped` to `Passed` on a
      Vulkan-capable host. The 2026-05-28 change removes the default-recipe
      cold-gate pre-check and makes post-run non-operational status a failure;
      focused `DefaultRecipeSurfaceGpuSmoke` verification passed normally in
      7.17s under `build/ci-vulkan`.
- [x] Defer four-sample-point default-recipe pixel-readback parity to a named
      follow-up instead of leaving it as an ambiguous Slice D blocker:
      [`GRAPHICS-076E`](GRAPHICS-076E-default-recipe-pixel-readback.md).
      That follow-up owns either (a) generalizing the existing
      `SetMinimalDebugBackbufferReadbackBuffer(...)` /
      `MinimalDebugBackbufferReadbackCopyCount` seam to accept
      `FrameRecipeKind::Default`, or (b) adding a sibling
      `SetDefaultRecipeBackbufferReadbackBuffer(...)` /
      `DefaultRecipeBackbufferReadbackCopyCount` pair for diagnostic clarity.

## Tests

Slice A (this slice):
- [x] `contract;graphics` — `PresentPassContract.ExecuteRecordsBindPipelineThenFullscreenDrawInOrder`:
      direct `PresentPass` invocation against a `RecordingCommandContext`
      asserts the `BindPipeline + Draw(3, 1, 0, 0)` shape and the
      pipeline-unset short-circuit.
- [x] `contract;graphics` — `PresentPassContract.RendererRoutesAndRecordsPresentPass`:
      renderer-integrated default-recipe frame records `"Present"` as
      `Recorded` and observes the bind/draw shape on the device
      command context.
- [x] `contract;graphics` — `PresentPassContract.MissingPresentPipelineLeaseSkipsUnavailable`:
      a `MockDevice` that fails the present-pipeline `Create()` call
      yields `"Present" = SkippedUnavailable` while the rest of the
      default recipe still records.
- [x] `contract;graphics` — `PresentPassContract.NonOperationalDeviceSkipsNonOperational`:
      mirrors the `MinimalDebugPresentPassContract.NonOperationalDeviceSkipsNonOperational`
      shape so the default-recipe present taxonomy stays symmetric with
      the minimal recipe.
- [x] `contract;graphics` — `Test.RendererFrameLifecycle` refreshed:
      `kSoftSkippedPasses` no longer contains `"Present"`,
      `kRoutedPasses` gains it, and the default-recipe `BindPipelineCalls`
      assertion increments by one to match the new bind.

Slice B (canonical `Pass.DebugView`, this slice):
- [x] `contract;graphics` — `DebugViewPassContract.RendererRoutesAndRecordsDebugViewPass`:
      `RenderFrameInput::DebugOverlayEnabled = true` flips
      `features.EnableDebugView`, the executor records `"DebugViewPass"`
      with `Status = Recorded`, a 16-byte
      `DebugViewPushConstants`-sized push reaches the device command
      context, and the bind-count exceeds the Slice A baseline by at
      least one. `DebugViewPassExecutions == 1u`.
- [x] `contract;graphics` — `DebugViewPassContract.DebugOverlayDisabledKeepsDebugViewOutOfRecipe`:
      default world omits `"DebugViewPass"` from
      `RenderGraphFrameStats::CommandRecords` entirely, and both
      `DebugViewPassExecutions` / `DebugViewFallbackInvocationCount`
      stay at zero — the renderer's per-frame DebugView driving
      cannot leak into the executor stats when the world has not
      requested an overlay.
- [x] `contract;graphics` — `DebugViewPassContract.MissingDebugViewPipelineLeaseSkipsUnavailable`:
      `MockDevice::FailPipelineCreateCall = 25` fails the DebugView
      pipeline create (call #25, immediately after present at #24),
      yielding `"DebugViewPass" = SkippedUnavailable` while the
      `"Present"` pass still records (upstream pipelines unaffected).
- [x] `contract;graphics` — `DebugViewPassContract.NonOperationalDeviceSkipsNonOperational`:
      `device.Operational = false` after Initialize yields
      `"DebugViewPass" = SkippedNonOperational`, mirroring the
      symmetric shape across present/debug-view paths.
- [x] `contract;graphics` — `DebugViewPassContract.InvalidResourceFallsBackDeterministicallyAndIncrementsCounter`:
      `SetDebugViewRequestedResourceName("DebugViewRGBA")` (non-previewable
      per the GRAPHICS-013BQ inspection-table aliasing gate) routes
      `ResolveSelection` through fallback to the first previewable
      resource (typically `SceneColorHDR`); the pass still records
      `Recorded` (no silent failure), and
      `DebugViewFallbackInvocationCount` increments by exactly 1.
- [x] `contract;graphics` — `DebugViewPassContract.DefaultRequestedResourceDoesNotIncrementFallbackCounter`:
      the default `"FrameRecipe.PresentSource"` sentinel routes
      through the canonical "show present source" path inside
      `ResolveSelection` and does NOT mark `UsedFallback = true`,
      keeping `DebugViewFallbackInvocationCount = 0` under the
      common "overlay on, no selection yet" UX state.
- [x] `contract;graphics` — direct `DebugViewPass::Execute` shape +
      disabled-selection / missing-pipeline short-circuits already
      pinned by the pre-existing `Test.DebugViewContract.cpp` tests
      (`DebugViewPassRecordsFullscreenPreviewForResolvedSelection`,
      `DebugViewPassSkipsDisabledSelectionAndMissingPipeline`).

Slice C–D tests are written and wired.

## Docs
- [x] (Slice A) Update `src/graphics/renderer/README.md` to record
      canonical `Pass.Present` as operationally wired and to point at
      this task as the upstream retirement gate for the `MinimalDebug`
      present scaffold.
- [x] (Slice B) Update the same README to record canonical
      `Pass.DebugView` as operationally wired, including the
      `DebugViewSystem` per-frame driving (settings + resolved
      selection), the new diagnostic counters
      (`DebugViewPassExecutions`, `DebugViewFallbackInvocationCount`),
      and the `IRenderer::SetDebugViewRequestedResourceName(...)`
      public seam.
- [x] (Slice D) Record the default-recipe smoke graduation in this task file
      and move default-recipe pixel-readback parity into
      [`GRAPHICS-076E`](GRAPHICS-076E-default-recipe-pixel-readback.md).

## Acceptance criteria

Slice A:
- [x] `Pass.Present` records `BindPipeline + Draw(3, 1, 0, 0)` in the
      operational state and increments the `Recorded` count in
      `RenderGraphCommandPassStats`.
- [x] No regression in CPU/null tests; the lifecycle-test refresh accounts
      for the new operational pass deterministically.
- [x] No silent failure when the present pipeline lease is missing — the
      executor reports `SkippedUnavailable` and records no draw.

Slice B:
- [x] Canonical `Pass.DebugView` records `BindPipeline +
      PushConstants(16) + Draw(3, 1, 0, 0)` in the operational state
      when the world has the debug overlay enabled, and
      `DebugViewPassExecutions` increments by 1.
- [x] No silent failure when `DebugViewSettings::RequestedResourceName`
      requests a non-previewable / missing / disabled resource: the
      `DebugViewSystem` falls back deterministically to the first
      previewable resource (typically `SceneColorHDR`), the pass still
      records `Recorded`, and the renderer surfaces
      `DebugViewFallbackInvocationCount += 1` so runtime/editor can
      observe the diagnostic.
- [x] The default `"FrameRecipe.PresentSource"` sentinel does NOT
      increment `DebugViewFallbackInvocationCount` (canonical "show
      present source" path, not a fallback).
- [x] Default world omits `"DebugViewPass"` from
      `RenderGraphFrameStats::CommandRecords` entirely; both
      diagnostic counters stay at zero.

Full task:
- [x] Both canonical passes record commands in the operational state (Slice B).
- [x] No silent failure when `DebugViewSettings` requests an invalid
      resource (must fall back deterministically and surface a
      diagnostic) — Slice B.
- [x] Non-present writes to `Backbuffer` produce a render-graph
      validation finding (Slice C).
- [x] Default-recipe `gpu;vulkan` smoke green on Vulkan-capable hosts
      with zero fallback counters (Slice D recipe-selector proof).
- [x] No regression in CPU/null tests across slices.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding ImGui overlay code (reserved for `GRAPHICS-079` + `RUNTIME-090`).
- Adding backend-native swapchain blit/copy fast path.
- Mixing mechanical file moves with semantic refactors.
- Touching `Pass.DebugView` wiring inside Slice A — explicitly deferred
  to Slice B so reviewers see one new executor branch per slice.

## Next verification step
- After Slice A (landed PR #921 + Slice A follow-up PR #922): built
  `IntrinsicGraphicsContractTests` on a `clang-20` host and ran the
  default-recipe CPU/null gate
  (`ctest --test-dir build/ci -L contract -LE 'gpu|vulkan|slow|flaky-quarantine'`).
- After Slice B (landed PR #923): built
  `IntrinsicGraphicsContractCpuTests` on a `clang-20` host and ran the
  default-recipe CPU/null gate
  (`ctest --test-dir build/ci -L contract -LE 'gpu|vulkan|slow|flaky-quarantine'`).
- After Slice C (landed PR #924): built
  `IntrinsicGraphicsContractCpuTests` on a `clang-20` host and ran the
  default-recipe CPU/null gate
  (`ctest --test-dir build/ci -L contract -LE 'gpu|vulkan|slow|flaky-quarantine'`).
- Slice D (fixture skeleton landed 2026-05-26; graduate-from-SKIP
  local change 2026-05-28): focused verification completed with
  `cmake --preset ci-vulkan -DCMAKE_C_COMPILER=/usr/bin/clang-22 -DCMAKE_CXX_COMPILER=/usr/bin/clang++-22 -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps-22`,
  `cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests`,
  and
  `LSAN_OPTIONS=suppressions=/home/alex/Documents/IntrinsicEngine/lsan.supp ctest --test-dir build/ci-vulkan --output-on-failure -R 'DefaultRecipeSurfaceGpuSmoke' --timeout 120`
  → 1/1 passed (`DefaultRecipeSurfaceGpuSmoke.RecipeSelectorReachesOperationalVulkanCommandStream`
  `Passed`, not `Skipped`),
  `LSAN_OPTIONS=suppressions=/home/alex/Documents/IntrinsicEngine/lsan.supp ctest --test-dir build/ci-vulkan -L 'gpu' -LE 'slow|flaky-quarantine' --output-on-failure --timeout 120`
  → 4/4 passed,
  `cmake --build --preset ci --target IntrinsicGraphicsContractTests`,
  `ctest --test-dir build/ci --output-on-failure -L 'contract' -L 'graphics' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  → 253/253 passed, and
  `cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke`,
  `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  → 2297/2297 passed.
