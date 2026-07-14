---
id: GRAPHICS-113
theme: B
depends_on: []
maturity_target: Operational
completed: 2026-07-04
---
# GRAPHICS-113 — Selection outline ID work pruning

## Completion
- Completed: 2026-07-04. Commit/PR: this retirement change.
- Maturity: `Operational` on Vulkan-capable hosts; `CPUContracted` on the
  default frame-recipe and command-route contracts.
- Summary: selected/hovered outline frames now use the one-target
  `Renderer.SelectionEntityId.OutlineOnly` path and declare/write only
  `EntityId`; pending click-pick frames keep the full `EntityId` +
  `PrimitiveId` + `Picking.Readback` route and primitive subpasses.
- Candidate narrowing evaluation: no additional selected/hovered-only draw
  narrowing landed in this slice. The current graphics-owned execution seam
  exposes whole `SurfaceOpaque` cull-bucket indirect buffers to
  `EntityIdPass::Execute(...)`, while selected/hovered stable IDs are CPU
  snapshot data consumed by outline push constants, not a GPU-readable filtered
  draw list. Narrowing ID rendering further would require a new graphics-owned
  filtered indirect buffer/cull bucket or shader-visible selected-ID data
  contract with its own tests, so it remains outside this task's safe local
  scope.

## Goal
- Reduce selected-but-not-picking renderer work so selection outline frames do only the ID/mask work actually required for hovered/selected highlighting, without carrying primitive-picking resources or full-scene primitive ID work when no click pick is pending.

## Non-goals
- Do not change runtime selection semantics, pick result encoding, primitive refinement, or `SelectionController` behavior.
- Do not remove selection outline or make it optional as a substitute for correct scheduling.
- Do not move ECS/runtime selection ownership into graphics; graphics continues to consume immutable `RenderWorld::Selection` snapshots.
- Do not change the UI/editor cache pipeline; `RUNTIME-138` owns main-thread selected-entity responsiveness.

## Context
- Owning subsystem/layer: `graphics/renderer` and graphics pass/frame-recipe code. Graphics may consume render snapshots and RHI resources but must not import runtime, ECS, platform, app, or live asset services.
- Current frame-recipe behavior enables `SelectionOutlinePass` whenever there is a hovered or selected stable id. It also enables the `PickingPass` producer for outline-only frames so `EntityId` exists.
- Current command routing gates face/edge/point primitive subpasses and readback on an actual pending pick, but outline-only frames still declare and allocate `PrimitiveId` alongside `EntityId` because the selection-ID pipeline/render-pass shape still treats them as sibling targets.
- This task focuses on GPU/framegraph work that remains after `RUNTIME-138` removes selected-entity editor CPU stalls. It may use `RUNTIME-138` timing data, but should have its own renderer tests.

## Required changes
- [x] Split outline-only ID production from click-picking production in the frame recipe so `PrimitiveId` and `Picking.Readback` are active only when a pick request is pending.
- [x] Adjust the selection entity-ID pipeline/pass descriptor, render-pass attachment setup, and framegraph resource declarations so outline-only frames write only the resources they consume.
- [x] Preserve the existing primitive face/edge/point ID subpasses and readback route for actual pending picks.
- [x] Evaluate whether outline-only ID rendering can be narrowed to selected/hovered candidates rather than all surface cull buckets; implement only if it remains within graphics snapshot ownership and has focused tests.
- [x] Expose renderer diagnostics that distinguish outline-only ID work, pending-pick primitive ID work, and readback copies.

## Tests
- [x] Update frame-recipe contract tests proving selected-outline-only frames declare `EntityId` but not `PrimitiveId` or `Picking.Readback`.
- [x] Update renderer frame lifecycle/command-route tests proving primitive subpasses and readback run only for pending picks.
- [x] Add tests for the outline-only pass attachment/pipeline shape if a separate pipeline or render-pass variant is introduced.
- [x] Run opt-in `gpu;vulkan` smoke on selected-outline and click-picking frames.

## Docs
- [x] Update `src/graphics/renderer/README.md` and any frame-recipe docs to describe the outline-only vs picking resource split.
- [x] Update `RUNTIME-138`, the rendering backlog index, and related review
      links so selected-frame work-pruning ownership reflects this retired
      renderer slice.

## Acceptance criteria
- [x] Selected-but-not-picking frames do not allocate, transition, write, or read `PrimitiveId`.
- [x] Pending click-picking frames still produce entity, primitive, and readback results with existing semantics.
- [x] Selection outline remains visually correct for hovered/selected entities.
- [x] Renderer diagnostics can show whether a selected frame ran outline-only ID work or full picking work.
- [x] Layering remains graphics/RHI-only for implementation changes.

## Verification
```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_test_layout.py --root . --strict
git diff --check
git grep -n "\\[DBG-" -- .
tools/ci/run_clean_workshop_review.sh . --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'FrameRecipe|RendererFrameLifecycle|Selection' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
# On a Vulkan-capable host:
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'Selection|Picking|Outline' --timeout 180
```

## Forbidden changes
- Adding runtime, ECS, platform, app, or ImGui imports to graphics.
- Disabling picking, primitive refinement, or selection outline to reduce work.
- Changing render-id/stable-id encoding contracts without a separate migration task.
- Mixing unrelated frame-recipe or pass rewrites into this work.

## Maturity
- Retired at `Operational` on Vulkan-capable hosts; `CPUContracted` for the
  default CPU frame-recipe and command-route contracts.
- The selected-frame Vulkan smoke remains the operational proof for the
  outline-only vs pending-pick split. Further selected/hovered-only draw-list
  narrowing is a separate graphics design/implementation task because it needs a
  new filtered indirect draw or shader-visible selected-ID contract.
