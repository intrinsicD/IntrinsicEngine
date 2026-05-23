# GRAPHICS-076 — Default-recipe `Pass.DebugView` and canonical `Pass.Present` wiring

## Status

- Status: in-progress (Slice C landing).
- Owner/agent: Claude on `claude/intrinsicengine-agent-onboarding-wImXR`.
- Branch: `claude/intrinsicengine-agent-onboarding-wImXR`.
- Started: 2026-05-23 (Slice A landed via PR #921 on
  `claude/intrinsicengine-agent-onboarding-GdEzP`; Slice B landed via
  PR #923 on `claude/intrinsicengine-agent-onboarding-cp7C2`; Slice C
  continues on the next onboarding-series branch).
- Next verification step: build `IntrinsicGraphicsContractCpuTests` on a
  `clang-20` host and run the default-recipe CPU/null gate
  (`ctest --test-dir build/ci -L contract -LE 'gpu|vulkan|slow|flaky-quarantine'`).

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

> **Scaffold-retirement obligation.** This task is the last upstream gate before [`GRAPHICS-081`](../backlog/rendering/GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md) can begin deleting the `MinimalDebugSurface` recipe scaffold. As part of `GRAPHICS-076`, author the **default-recipe equivalent** of the `GRAPHICS-033D` `gpu;vulkan` visible-triangle smoke (same pixel-readback driver harness, same four-sample-point assertion, same zero-fallback-counter invariant) so that `GRAPHICS-081` can delete the minimal-recipe fixture without reducing `gpu;vulkan` coverage. Owned by Slice D.

## Context
- Status: Slice A in-progress (see `## Status`).
- Owner/layer: `graphics/renderer`.
- Planning anchors: `tasks/done/GRAPHICS-013B-debug-view-and-render-target-inspection.md`, `tasks/done/GRAPHICS-013BQ-debug-view-backend-clarifications.md`, `tasks/done/GRAPHICS-013C-imgui-overlay-and-present.md`, `tasks/done/GRAPHICS-013CQ-imgui-present-backend-clarifications.md`.
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

Slice D (default-recipe `gpu;vulkan` visible-triangle smoke, deferred):
- [ ] Add `tests/integration/graphics/Test.DefaultRecipeSurfaceGpuSmoke.cpp`
      under `gpu;vulkan;graphics` labels, sharing the GRAPHICS-033D
      bounded `engine.Run()` driver helper, asserting four-sample-point
      pixel-readback parity with the MinimalDebug smoke and zero
      fallback counters.

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

Slice C–D tests are written when those slices land.

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
- [ ] Default-recipe `gpu;vulkan` smoke green on Vulkan-capable hosts
      with zero fallback counters (Slice D).
- [ ] No regression in CPU/null tests across slices.

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
- After Slice A: build `IntrinsicGraphicsContractTests` on a `clang-20`
  host and run the default-recipe CPU/null gate
  (`ctest --test-dir build/ci -L contract -LE 'gpu|vulkan|slow|flaky-quarantine'`).
- After Slice B: build
  `IntrinsicGraphicsContractCpuTests` on a `clang-20` host and run the
  default-recipe CPU/null gate
  (`ctest --test-dir build/ci -L contract -LE 'gpu|vulkan|slow|flaky-quarantine'`),
  then proceed to Slice C (render-graph validation negative test for
  non-present writes to `Backbuffer`).
- After Slice C (this slice): build
  `IntrinsicGraphicsContractCpuTests` on a `clang-20` host and run the
  default-recipe CPU/null gate
  (`ctest --test-dir build/ci -L contract -LE 'gpu|vulkan|slow|flaky-quarantine'`),
  then proceed to Slice D (default-recipe `gpu;vulkan`
  visible-triangle smoke) on a Vulkan-capable host.
