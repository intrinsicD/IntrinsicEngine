# GRAPHICS-076 — Default-recipe `Pass.DebugView` and canonical `Pass.Present` wiring

## Goal
- Wire `Pass.DebugView` and the canonical `Pass.Present` (`src/graphics/renderer/Passes/Pass.Present.cpp:14`) into the renderer executor under the default recipe per `GRAPHICS-013B`/`013BQ` and the present contract from `GRAPHICS-013CQ`. Pipelines created at renderer init / `RebuildOperationalResources()`; executor branches added.

## Non-goals
- No ImGui overlay (`GRAPHICS-079`).
- No buffer-class debug visualization (out-of-scope per `GRAPHICS-013BQ`).
- No backend-native swapchain blit/copy fast path (rejected per `GRAPHICS-013CQ` for the contract finalization form).

## Context
- Status: not started.
- Owner/layer: `graphics/renderer`.
- Planning anchors: `tasks/done/GRAPHICS-013B-debug-view-and-render-target-inspection.md`, `tasks/done/GRAPHICS-013BQ-debug-view-backend-clarifications.md`, `tasks/done/GRAPHICS-013C-imgui-overlay-and-present.md`, `tasks/done/GRAPHICS-013CQ-imgui-present-backend-clarifications.md`.
- Today: `Pass.DebugView.cpp` and `Pass.Present.cpp` exist as shells (Pass.Present has the `BindPipeline` + `Draw(3,1,0,0)` body but is not owned by `NullRenderer`); the executor lambda has no branches.
- The minimal-debug present (`Pass.Present.MinimalDebug` from `GRAPHICS-032C`) shares the fullscreen-triangle shape but exists as a separate class so the minimal recipe stays self-contained.

## Required changes
- [ ] Add `m_DebugViewPass`, `m_PresentPass`, `m_DebugViewPipelineLease`, `m_PresentPipelineLease` members to `NullRenderer`.
- [ ] In `InitializeOperationalPassResources(device)`, create:
  - debug-view pipeline from `assets/shaders/debug_view.vert` + `debug_view.frag` (and `debug_view.comp` for compute-driven aspects per `GRAPHICS-013B` decisions; the implementer chooses which form is canonical),
  - present pipeline from a fullscreen vertex (`assets/shaders/triangle.vert` or a dedicated `present.vert`) + a copy-to-LDR fragment that samples `FrameRecipe.PresentSource`.
- [ ] Add executor branches `"Pass.DebugView"` and `"Pass.Present"` routing through `RecordDebugViewPass(...)` and `RecordPresentPass(...)` helpers with the recorded taxonomy.
- [ ] Confirm `BuildDefaultFrameRecipe` declares `DebugViewRGBA` as a non-previewable transient and `Backbuffer` as the imported swapchain target finalized only by `Pass.Present`.
- [ ] Reject non-present writes to the imported `Backbuffer` via render-graph validation findings (verify the existing rejection surfaces an error, no silent success).

## Tests
- [ ] `contract;graphics` test: `Pass.Present` records `BindPipeline(present)` + `Draw(3, 1, 0, 0)`.
- [ ] `contract;graphics` test: `Pass.DebugView` records when `DebugViewSettings::RequestedResourceName` resolves to a previewable resource; falls back to current `PresentSource` deterministically when the request is invalid.
- [ ] `contract;graphics` test: a non-present write to `Backbuffer` produces a render-graph validation finding (negative test).
- [ ] `contract;graphics` test: pipeline leases survive `RebuildGpuResources()`.

## Docs
- [ ] Update `src/graphics/renderer/README.md` to record `Pass.DebugView` + canonical `Pass.Present` as operationally wired.

## Acceptance criteria
- [ ] Both passes record commands in the operational state.
- [ ] No silent failure when `DebugViewSettings` requests an invalid resource (must fall back deterministically and surface a diagnostic).
- [ ] No regression in CPU/null tests.

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

## Next verification step
- Create the pipelines, wire the executor routes + render-graph validation negative test, exercise the contract tests above.
