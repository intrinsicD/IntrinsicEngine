---
id: GRAPHICS-113
theme: B
depends_on: []
maturity_target: Operational
---
# GRAPHICS-113 — Selection outline ID work pruning

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
- [ ] Evaluate whether outline-only ID rendering can be narrowed to selected/hovered candidates rather than all surface cull buckets; implement only if it remains within graphics snapshot ownership and has focused tests.
- [x] Expose renderer diagnostics that distinguish outline-only ID work, pending-pick primitive ID work, and readback copies.

## Tests
- [x] Update frame-recipe contract tests proving selected-outline-only frames declare `EntityId` but not `PrimitiveId` or `Picking.Readback`.
- [x] Update renderer frame lifecycle/command-route tests proving primitive subpasses and readback run only for pending picks.
- [x] Add tests for the outline-only pass attachment/pipeline shape if a separate pipeline or render-pass variant is introduced.
- [x] Run opt-in `gpu;vulkan` smoke on selected-outline and click-picking frames.

## Docs
- [x] Update `src/graphics/renderer/README.md` and any frame-recipe docs to describe the outline-only vs picking resource split.
- [x] Link this task from `RUNTIME-138` and the rendering backlog index as the renderer-owned selected-frame work-pruning follow-up.

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
python3 tools/repo/check_layering.py --root src --strict
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
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` for frame-recipe and command-route contracts.
- This task may retire at `CPUContracted` only if `Operational` owned by a follow-up task is explicitly named; otherwise the selected-frame Vulkan smoke is required.
- Current implementation state (2026-07-02): `Operational` for the outline-only
  vs pending-pick split. Frame-recipe, command-route, diagnostics, and
  pipeline-shape contracts are implemented, and targeted `gpu;vulkan` sandbox
  smokes passed for click-picking and selected-outline visibility. The
  candidate-narrowing evaluation remains open before retirement.
