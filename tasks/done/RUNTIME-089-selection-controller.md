# RUNTIME-089 — Runtime selection controller and snapshot handoff

## Status
- State: `done` — retired 2026-05-31 at maturity `CPUContracted`.
- Owner/agent: `claude` (onboarding session).
- Branch: Slice A landed on `claude/intrinsicengine-agent-onboarding-Vyaea`
  (merged via PR #955); Slice B landed on
  `claude/intrinsicengine-agent-onboarding-VBuRD`.
- PR/commit: Slice A — commit `e246880` + `05b71fb` (PR #955). Slice B —
  commit: _pending push to `claude/intrinsicengine-agent-onboarding-VBuRD`_.
  PR: _TBD_.
- Maturity reached: `CPUContracted`. Slice A landed the standalone
  `Extrinsic.Runtime.SelectionController` module + pure-CPU `contract;runtime`
  tests (`Scaffolded`). Slice B wired the controller into the real runtime frame
  path (`Engine::RunFrame` pick drain → `SelectionSystem::RequestPick` before
  extraction, readback consume in the maintenance phase, and the controller
  snapshot mirror into `RenderWorld.Selection` via
  `RenderExtractionCache::ExtractAndSubmit(..., const SelectionController*)`),
  closing `Scaffolded → CPUContracted`.
- Verification (Slice B session): `cmake --preset ci`;
  `cmake --build --preset ci --target IntrinsicRuntimeContractTests
  IntrinsicGraphicsContractTests`;
  `ctest --test-dir build/ci -L contract -LE 'gpu|vulkan|slow|flaky-quarantine'
  --timeout 60` → 253/253 (221 runtime + 32 graphics), including 28
  `SelectionController`/`SelectionSnapshotExtraction` cases; layering,
  test-layout, doc-links, task-policy, and module-inventory (no diff) checks
  clean.
- `Operational` outline/pick proof remains owned by `GRAPHICS-074` plus the
  final working-sandbox acceptance task (`RUNTIME-095`); the real input→pick
  binding (mouse button/modifier policy) is owned by a later editor/UI task.

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
- [x] _(Slice B)_ Wire runtime frame code to coalesce accepted pointer picks into `RenderFrameInput::Pick` / `HasPendingPick` before `IRenderer::ExtractRenderWorld()`.
- [x] _(Slice B)_ After renderer readback drains, consume `SelectionSystem::GetLastPickResult()` and resolve stable/entity IDs through a runtime-owned lookup seam (using `entt::entity` initially if `RUNTIME-092` is not landed, and upgrading to stable IDs when available).
- [x] _(Slice A)_ Mutate runtime/ECS/editor selection state only in runtime/editor code: update `SelectedTag` / `HoveredTag` or an editor-owned sidecar according to documented policy.
- [x] _(Slice B)_ Extend runtime snapshot submission to populate `RenderWorld.Selection.SelectedStableIds`, `HoveredStableId`, and `HasHovered` without graphics reading live ECS. _(Slice A produces the controller-owned snapshot buffers consumed here.)_
- [x] _(Slice A)_ Add diagnostics: pick requests submitted, readbacks consumed, hits, no-hits, stale entity hits, non-selectable hits rejected, selection changes emitted.
- [x] _(Slice A)_ Define default sandbox policy: single-select click, hover outline, additive modifier if input port exposes one, clear on background click.

## Tests
- [x] _(Slice A)_ Add `contract;runtime` coverage for click hit -> selected entity state -> snapshot buffer contains one selected stable/entity ID.
- [x] _(Slice A)_ Add `contract;runtime` coverage for hover hit -> hovered state without changing selected state.
- [x] _(Slice A)_ Add `contract;runtime` coverage for background/no-hit clearing hover and optionally selection per documented policy.
- [x] _(Slice A)_ Add stale/non-selectable hit rejection coverage.
- [x] _(Slice A)_ Add coalescing coverage so multiple same-frame pointer events produce the documented single pending-pick shape.
- [x] _(Slice B)_ Add `contract;runtime` coverage that the populated `RenderWorld.Selection` snapshot mirrors controller state through `ExtractAndSubmit` (`Test.SelectionSnapshotExtraction.cpp`: selected/hovered/additive mirror, null-controller empty, cleared-empty).
- [x] _(Slice A)_ No `gpu`/`vulkan` test in this slice; graphics readback command coverage is `GRAPHICS-074`.

## Docs
- [x] Update `src/runtime/README.md` with selection-controller ownership, policy defaults, and diagnostics. _(Slice A added the module-table row; Slice B updated it plus the canonical frame-loop phases with the pick drain / readback consume / snapshot mirror handoff points.)_
- [x] Update `docs/architecture/rendering-three-pass.md` only if the graphics/runtime selection boundary changes. _(No change: the boundary — runtime owns mutation, graphics reporting-only — is the design GRAPHICS-012/074 already documented; Slice B implements it without moving the boundary.)_
- [x] Refresh `docs/api/generated/module_inventory.md` if new modules are added. _(No new modules; regenerated, no diff.)_

## Acceptance criteria
- [x] Runtime owns the complete selection mutation path; graphics remains reporting-only.
- [x] `RenderWorld.Selection` is populated from runtime/editor state and can drive `SelectionOutlinePass` once `GRAPHICS-074` records it.
- [x] Pick request coalescing and stale/non-selectable rejection are deterministic and tested.

## Slice plan
- **Slice A (landed).** Standalone `Extrinsic.Runtime.SelectionController`
  module (`.cppm` + `.cpp`) under `src/runtime/`: input-facing hover/click/
  programmatic APIs, per-frame pick coalescing into one pending pixel pick
  (click supersedes hover; latest position wins), a bounded FIFO of
  `Sequence`-tagged in-flight picks so multiple readbacks in flight
  (frames-in-flight GPU picking) each replay their own request's kind/mode —
  `ConsumeHit`/`ConsumeNoHit` correlate by `Sequence` (with oldest-first
  convenience overloads and an untracked-readback fallback), since
  `Graphics.Renderer::DrainCompletedPickingSlots` publishes per-slot into the
  single-result `SelectionSystem` holder and is not issue-ordered, ECS
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
- Target: `CPUContracted` for runtime selection policy and snapshot handoff. **Reached.**
- Slice A closed `Scaffolded`: the standalone controller exists and is
  CPU-tested in isolation, but was not yet wired into a real runtime frame path.
- Slice B closed `Scaffolded → CPUContracted`: the controller is wired into
  `Engine::RunFrame` and `RenderExtractionCache::ExtractAndSubmit`, and the
  `contract;runtime` snapshot-mirror coverage proves the runtime drives the
  selected/hovered handoff end-to-end through the extraction path on the default
  CPU/null gate (graphics reporting-only, no live ECS read from graphics).
- `Operational` outline/pick proof requires `GRAPHICS-074` plus the final sandbox acceptance task (`RUNTIME-095`).

