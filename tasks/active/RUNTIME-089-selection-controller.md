# RUNTIME-089 — Runtime selection controller and snapshot handoff

## Status
- State: `in-progress`.
- Owner/agent: `claude` (onboarding session).
- Branch: `claude/intrinsicengine-agent-onboarding-Vyaea`.
- Maturity reached: `Scaffolded` (Slice A) — standalone
  `Extrinsic.Runtime.SelectionController` module + pure-CPU `contract;runtime`
  tests landed; no `Engine::RunFrame` / `RenderExtractionCache` wiring yet.
- Next verification step: Slice B — wire the controller into `Engine::RunFrame`
  (coalesce into `RenderFrameInput::Pick` / `SelectionSystem::RequestPick`
  before extraction; drain `SelectionSystem::GetLastPickResult()` after
  readback) and populate `RenderWorld.Selection.{SelectedStableIds,
  HoveredStableId,HasHovered}` from the controller snapshot during
  `RenderExtractionCache::ExtractAndSubmit`, closing `Scaffolded → CPUContracted`.

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
- [x] _(Slice A)_ Add a runtime `SelectionController` module with input-facing APIs for hover pick, click pick, additive/toggle selection, clear selection, and programmatic selection by stable/entity ID.
- [ ] _(Slice B)_ Wire runtime frame code to coalesce accepted pointer picks into `RenderFrameInput::Pick` / `HasPendingPick` before `IRenderer::ExtractRenderWorld()`.
- [ ] _(Slice B)_ After renderer readback drains, consume `SelectionSystem::GetLastPickResult()` and resolve stable/entity IDs through a runtime-owned lookup seam (using `entt::entity` initially if `RUNTIME-092` is not landed, and upgrading to stable IDs when available).
- [x] _(Slice A)_ Mutate runtime/ECS/editor selection state only in runtime/editor code: update `SelectedTag` / `HoveredTag` or an editor-owned sidecar according to documented policy.
- [ ] _(Slice B)_ Extend runtime snapshot submission to populate `RenderWorld.Selection.SelectedStableIds`, `HoveredStableId`, and `HasHovered` without graphics reading live ECS. _(Slice A produces the controller-owned snapshot buffers consumed here.)_
- [x] _(Slice A)_ Add diagnostics: pick requests submitted, readbacks consumed, hits, no-hits, stale entity hits, non-selectable hits rejected, selection changes emitted.
- [x] _(Slice A)_ Define default sandbox policy: single-select click, hover outline, additive modifier if input port exposes one, clear on background click.

## Tests
- [x] _(Slice A)_ Add `contract;runtime` coverage for click hit -> selected entity state -> snapshot buffer contains one selected stable/entity ID.
- [x] _(Slice A)_ Add `contract;runtime` coverage for hover hit -> hovered state without changing selected state.
- [x] _(Slice A)_ Add `contract;runtime` coverage for background/no-hit clearing hover and optionally selection per documented policy.
- [x] _(Slice A)_ Add stale/non-selectable hit rejection coverage.
- [x] _(Slice A)_ Add coalescing coverage so multiple same-frame pointer events produce the documented single pending-pick shape.
- [ ] _(Slice B)_ Add `contract;runtime` coverage that the populated `RenderWorld.Selection` snapshot mirrors controller state through `ExtractAndSubmit`.
- [x] _(Slice A)_ No `gpu`/`vulkan` test in this slice; graphics readback command coverage is `GRAPHICS-074`.

## Docs
- [ ] Update `src/runtime/README.md` with selection-controller ownership, policy defaults, and diagnostics.
- [ ] Update `docs/architecture/rendering-three-pass.md` only if the graphics/runtime selection boundary changes.
- [ ] Refresh `docs/api/generated/module_inventory.md` if new modules are added.

## Acceptance criteria
- [ ] Runtime owns the complete selection mutation path; graphics remains reporting-only.
- [ ] `RenderWorld.Selection` is populated from runtime/editor state and can drive `SelectionOutlinePass` once `GRAPHICS-074` records it.
- [ ] Pick request coalescing and stale/non-selectable rejection are deterministic and tested.

## Slice plan
- **Slice A (landed).** Standalone `Extrinsic.Runtime.SelectionController`
  module (`.cppm` + `.cpp`) under `src/runtime/`: input-facing hover/click/
  programmatic APIs, per-frame pick coalescing into one pending pixel pick
  (click supersedes hover; latest position wins), readback consumption
  (`ConsumeHit`/`ConsumeNoHit`) keyed by the in-flight pick kind, ECS
  `SelectedTag`/`HoveredTag` mutation via the documented Replace/Add/Toggle
  policy, stale-entity and non-selectable rejection, the runtime-owned
  `uint32 ↔ entt::entity` lookup seam (cast, RUNTIME-092 upgrade point),
  controller-owned `SelectedStableIds`/`HoveredStableId`/`HasHovered` snapshot
  buffers, and the diagnostics counter block. Pure-CPU `contract;runtime`
  tests in `tests/contract/runtime/Test.SelectionController.cpp`
  (`IntrinsicRuntimeContractTests`). Preserves the default CPU gate. **Defers**
  all `Engine`/`SelectionSystem`/`RenderWorld` wiring to Slice B; closes at
  `Scaffolded`.
- **Slice B.** Wire the controller into `Engine::RunFrame`: drain the pending
  pick into `RenderFrameInput::Pick`/`HasPendingPick` and
  `SelectionSystem::RequestPick` before `ExtractRenderWorld`; after readback,
  feed `SelectionSystem::GetLastPickResult()` into `ConsumeHit`/`ConsumeNoHit`;
  populate `RenderWorld.Selection.{SelectedStableIds,HoveredStableId,HasHovered}`
  from the controller snapshot during `RenderExtractionCache::ExtractAndSubmit`.
  Adds the snapshot-mirror `contract;runtime` integration test. Closes
  `Scaffolded → CPUContracted`.

## Verification
Slice A (this slice — ran in the authoring session):
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'SelectionController' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```
Full task (Slice B closure):
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
- Slice A closes `Scaffolded`: the standalone controller exists and is
  CPU-tested in isolation, but is not yet wired into a real runtime frame path,
  so it does not yet prove the engine performs selection end-to-end. Slice B
  owns `Scaffolded → CPUContracted`.
- `Operational` outline/pick proof requires `GRAPHICS-074` plus the final sandbox acceptance task (`RUNTIME-095`).

