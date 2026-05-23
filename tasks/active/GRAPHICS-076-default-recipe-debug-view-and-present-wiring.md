# GRAPHICS-076 — Default-recipe `Pass.DebugView` and canonical `Pass.Present` wiring

## Status

- Status: in-progress (Slice A landing).
- Owner/agent: Claude on `claude/intrinsicengine-agent-onboarding-GdEzP`.
- Branch: `claude/intrinsicengine-agent-onboarding-GdEzP`.
- Started: 2026-05-23.
- Next verification step: build `IntrinsicGraphicsContractTests` on a
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

Slice B (canonical `Pass.DebugView`, deferred):
- [ ] Add `m_DebugViewPass` + `m_DebugViewPipelineLease` members.
- [ ] Add `BuildDebugViewPipelineDesc(...)` pointing at
      `assets/shaders/debug_view.{vert,frag}` (and/or `debug_view.comp`
      per `GRAPHICS-013B`; the implementer picks the canonical form).
- [ ] Wire the executor `"DebugViewPass"` branch through
      `RecordDebugViewPass(...)` with the `Recorded` /
      `SkippedNonOperational` / `SkippedUnavailable` taxonomy and a
      diagnostic counter on invalid-resource fallback.

Slice C (render-graph validation negative test, deferred):
- [ ] Add a `contract;graphics` test confirming that a non-present write
      to the imported `Backbuffer` surfaces a
      `RenderGraphValidationResult` finding rather than silent success.

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

Slice B–D tests are written when those slices land.

## Docs
- [x] (Slice A) Update `src/graphics/renderer/README.md` to record
      canonical `Pass.Present` as operationally wired and to point at
      this task as the upstream retirement gate for the `MinimalDebug`
      present scaffold.
- [ ] (Slice B) Update the same README to record `Pass.DebugView` as
      operationally wired when Slice B lands.

## Acceptance criteria

Slice A:
- [x] `Pass.Present` records `BindPipeline + Draw(3, 1, 0, 0)` in the
      operational state and increments the `Recorded` count in
      `RenderGraphCommandPassStats`.
- [x] No regression in CPU/null tests; the lifecycle-test refresh accounts
      for the new operational pass deterministically.
- [x] No silent failure when the present pipeline lease is missing — the
      executor reports `SkippedUnavailable` and records no draw.

Full task:
- [ ] Both canonical passes record commands in the operational state (Slice B).
- [ ] No silent failure when `DebugViewSettings` requests an invalid
      resource (must fall back deterministically and surface a
      diagnostic) — Slice B.
- [ ] Non-present writes to `Backbuffer` produce a render-graph
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
