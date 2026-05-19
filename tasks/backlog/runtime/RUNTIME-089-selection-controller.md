# RUNTIME-089 — Runtime selection controller and snapshot handoff

## Goal
- Implement runtime/editor-owned selection control that converts platform/editor input into graphics pick requests, consumes graphics pick results, mutates runtime/editor selected/hovered state, and submits `RenderWorld.Selection` snapshots for outline rendering.

## Non-goals
- No graphics-side picking pass/readback implementation; that is `GRAPHICS-074`.
- No primitive refinement beyond consuming decoded primitive IDs; detailed CPU refinement is `RUNTIME-093`.
- No transform-gizmo hit testing; that is `RUNTIME-084`.
- No ImGui/editor panel implementation; UI consumes this controller through later UI tasks.
- No ECS mutation from graphics.

## Context
- Owner/layer: `runtime` (and editor-facing policy surfaces). Graphics reports pick results through `SelectionSystem`; runtime resolves them to live scene/editor state.
- Upstream graphics contract: `GRAPHICS-012` / `012Q` define ID packing, readback ownership, and runtime ownership of selection mutation. `GRAPHICS-074` wires the default-recipe selection passes/readback.
- ECS has `Selection::{SelectableTag, SelectedTag, HoveredTag}` and `StableId` payload support, but runtime must own any live lookup sidecar and editor selection policy.
- This task closes the handoff gap between `PickPixelRequest`, `SelectionSystem::GetLastPickResult()`, ECS/editor selection state, and `RenderWorld.Selection`.

## Required changes
- [ ] Add a runtime `SelectionController` module with input-facing APIs for hover pick, click pick, additive/toggle selection, clear selection, and programmatic selection by stable/entity ID.
- [ ] Wire runtime frame code to coalesce accepted pointer picks into `RenderFrameInput::Pick` / `HasPendingPick` before `IRenderer::ExtractRenderWorld()`.
- [ ] After renderer readback drains, consume `SelectionSystem::GetLastPickResult()` and resolve stable/entity IDs through a runtime-owned lookup seam (using `entt::entity` initially if `RUNTIME-092` is not landed, and upgrading to stable IDs when available).
- [ ] Mutate runtime/ECS/editor selection state only in runtime/editor code: update `SelectedTag` / `HoveredTag` or an editor-owned sidecar according to documented policy.
- [ ] Extend runtime snapshot submission to populate `RenderWorld.Selection.SelectedStableIds`, `HoveredStableId`, and `HasHovered` without graphics reading live ECS.
- [ ] Add diagnostics: pick requests submitted, readbacks consumed, hits, no-hits, stale entity hits, non-selectable hits rejected, selection changes emitted.
- [ ] Define default sandbox policy: single-select click, hover outline, additive modifier if input port exposes one, clear on background click.

## Tests
- [ ] Add `contract;runtime` coverage for click hit -> selected entity state -> `SelectionSnapshot` contains one selected stable/entity ID.
- [ ] Add `contract;runtime` coverage for hover hit -> hovered state without changing selected state.
- [ ] Add `contract;runtime` coverage for background/no-hit clearing hover and optionally selection per documented policy.
- [ ] Add stale/non-selectable hit rejection coverage.
- [ ] Add coalescing coverage so multiple same-frame pointer events produce the documented single `PickPixelRequest` shape.
- [ ] No `gpu`/`vulkan` test in this slice; graphics readback command coverage is `GRAPHICS-074`.

## Docs
- [ ] Update `src/runtime/README.md` with selection-controller ownership, policy defaults, and diagnostics.
- [ ] Update `docs/architecture/rendering-three-pass.md` only if the graphics/runtime selection boundary changes.
- [ ] Refresh `docs/api/generated/module_inventory.md` if new modules are added.

## Acceptance criteria
- [ ] Runtime owns the complete selection mutation path; graphics remains reporting-only.
- [ ] `RenderWorld.Selection` is populated from runtime/editor state and can drive `SelectionOutlinePass` once `GRAPHICS-074` records it.
- [ ] Pick request coalescing and stale/non-selectable rejection are deterministic and tested.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeTests IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract;runtime|contract;graphics' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Mutating ECS/editor selection state from `src/graphics/*`.
- Polling platform input from graphics.
- Implementing primitive CPU refinement in this controller task.
- Adding editor UI widgets or ImGui dependencies to the controller core.

## Maturity
- Target: `CPUContracted` for runtime selection policy and snapshot handoff.
- `Operational` outline/pick proof requires `GRAPHICS-074` plus the final sandbox acceptance task.

