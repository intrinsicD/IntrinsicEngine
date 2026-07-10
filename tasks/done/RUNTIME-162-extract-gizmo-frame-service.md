---
id: RUNTIME-162
theme: F
depends_on: [RUNTIME-161]
maturity_target: Operational
completed: 2026-07-09
---
# RUNTIME-162 — Extract gizmo frame service out of Engine

## Status
- Retired on 2026-07-09 at maturity `Operational`.
- `Extrinsic.Runtime.GizmoFrameService` owns live transform-gizmo interaction,
  undo storage, selected-entity scratch, gizmo/selection pointer interlock, and
  transform-gizmo packet building.
- `Runtime.Engine` keeps frame phase ordering and the public
  `GetGizmoInteraction()` / `GetGizmoUndoStack()` compatibility facades while
  delegating raw gizmo frame state and input policy through the service.
- Verification passed: focused gizmo / runtime sandbox CPU coverage,
  `RuntimeEngineLayering` integration coverage, strict task/docs/layering/
  test-layout checks, `IntrinsicTests`, and the default CPU-supported CTest
  gate at 3646/3646.
- Warning-mode root/task-state findings remain pre-existing and unchanged:
  retired `ARCH-007`..`ARCH-013` index links, root `ara/`, and root
  `imgui.ini`.
- PR/commit: pending.

## Goal
- Move transform-gizmo frame state, selected-entity scratch, gizmo/selection
  pointer interlock, and transform-gizmo packet building out of
  `Runtime.Engine.cppm` / `Runtime.Engine.cpp` into a focused runtime gizmo
  service while preserving the existing frame order and public compatibility
  accessors.

## Non-goals
- Changing gizmo hit-test, drag, snap, undo, packet, or rendering semantics.
- Changing camera-controller update, selection readback drain, refined primitive
  cache, or renderer extraction ordering.
- Moving the full render-extraction cache or visualization adapter facades.
- Changing editor toolbar/API callers that use `Engine::GetGizmoInteraction()`
  or `Engine::GetGizmoUndoStack()`.

## Context
- Owner: `runtime`; this is composition glue around the promoted
  `Extrinsic.Runtime.GizmoInteraction` module.
- `Runtime.Engine.cppm` still imports `Extrinsic.Runtime.GizmoInteraction` and
  stores `GizmoInteraction`, `GizmoUndoStack`,
  `TransformGizmoRenderPacketBuilder`, and selected-entity scratch directly.
- `Runtime.Engine.cpp` still drives the frame-loop helper with those raw
  members and directly builds the transform-gizmo packet span for render
  extraction.
- This follows the `RUNTIME-146` through `RUNTIME-161` decomposition pattern:
  `Engine` remains the concrete composition root and frame-order owner, while
  subsystem-local state and policy move behind runtime-owned modules.

## Required changes
- [x] Add `Extrinsic.Runtime.GizmoFrameService` under `src/runtime/Gizmos/`
  owning `GizmoInteraction`, `GizmoUndoStack`,
  `TransformGizmoRenderPacketBuilder`, and selected-entity scratch.
- [x] Move per-frame selected-entity rebuild, gizmo input drive,
  gizmo-vs-selection mouse interlock, and transform-gizmo packet build behind
  the service.
- [x] Update `Runtime.Engine.cppm` to store the service instead of the four raw
  gizmo members and to stop directly importing `GizmoInteraction`.
- [x] Update `Runtime.Engine.cpp` so RunFrame delegates gizmo driving and packet
  production through the service while preserving frame phase ordering.
- [x] Keep `GetGizmoInteraction()` and `GetGizmoUndoStack()` as delegating
  compatibility facades.
- [x] Add the new module to `src/runtime/CMakeLists.txt`.

## Tests
- [x] Add runtime source-contract coverage proving gizmo interaction state,
  selected scratch, and packet-builder ownership no longer live in
  `Runtime.Engine.cppm` / `Runtime.Engine.cpp`.
- [x] Preserve existing gizmo interaction, Engine layering, and runtime sandbox
  acceptance coverage.
- [x] Run the default CPU-supported correctness gate before retirement.

## Docs
- [x] Update `src/runtime/README.md` to document
  `Extrinsic.Runtime.GizmoFrameService` and revise the Engine/gizmo current
  state wording.
- [x] Update `tasks/backlog/runtime/README.md` with the factual decomposition
  state.
- [x] Update `tasks/backlog/README.md` if the Theme F Engine-decomposition
  summary changes.
- [x] Regenerate `docs/api/generated/module_inventory.md`.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening.

## Acceptance criteria
- [x] `Runtime.Engine.cppm` no longer imports or stores `GizmoInteraction`,
  `GizmoUndoStack`, `TransformGizmoRenderPacketBuilder`, or the gizmo selected
  entity scratch vector directly.
- [x] `Runtime.Engine.cpp` no longer calls
  `DriveGizmoAndSelectionInputForFrame(...)` or
  `TransformGizmoRenderPacketBuilder::Build(...)` directly.
- [x] Existing behavior remains unchanged: gizmo drags still suppress selection
  clicks, ImGui capture still cancels/suppresses gizmo input, selected entities
  still produce the same transform-gizmo packets, and public Engine gizmo
  accessors still work.
- [x] Strict task, docs, layering, and test-layout checks pass, aside from
  pre-existing warning-mode root/task-state findings if unchanged by this
  slice.

## Verification
```bash
python3 tools/agents/generate_session_brief.py
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'Gizmo|RuntimeEngineLayering|RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
cmake --build --preset ci --target IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeEngineLayering|RuntimeSandboxAcceptance' --timeout 180
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_state_links.py --root .
python3 tools/repo/check_root_hygiene.py --root .
git diff --check
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Changing gizmo math, selected-entity semantics, packet contents, or public
  gizmo accessors.
- Changing selection readback, refined primitive cache, render extraction, or
  camera-controller behavior.
- Moving visualization adapter bindings or render-extraction cache ownership.
- Reverting unrelated dirty worktree changes.

## Maturity
- Target: `Operational` for this cleanup slice.
- This slice closes at `Operational` when live Engine frame processing delegates
  transform-gizmo state and packet production to the new runtime service and
  focused gizmo/layering coverage plus the default CPU gate pass.
