---
id: RUNTIME-150
theme: F
depends_on:
  - ARCH-007
  - ARCH-008
maturity_target: Operational
---
# RUNTIME-150 — Split the frame-loop hook adapters out of Runtime.Engine.cpp

## Goal
- Move the per-frame plumbing that currently pads `Runtime.Engine.cpp` —
  the `Core.FrameLoop` hook adapter structs and the per-frame helper
  functions used only by `RunFrame()` — into a dedicated module partition
  (`Extrinsic.Runtime.Engine:FrameLoop`) with its own implementation unit,
  so the main implementation file holds lifecycle + facade code only.

## Non-goals
- No change to the frame shape or phase ordering documented in
  `Runtime.Engine.cppm` ("Frame shape" block) — this is a file split, not
  a scheduling change.
- No new registries or extension points.
- No `RunFrame()` rewrite; it stays in the `Engine` class and keeps its
  current structure, calling the moved helpers.
- No interface (`.cppm`) surface changes (owned by `RUNTIME-151`).

## Context
- Owning subsystem/layer: `runtime`. The repo already uses module
  partitions for internal decomposition (`Extrinsic.Core.Tasks:Internal`,
  `Extrinsic.Core.Memory:*`, `Extrinsic.Core.Dag.Scheduler:DomainGraph`),
  so a non-exported partition is the established mechanism for sharing
  internal helpers across implementation units of one module.
- What moves, from the anonymous namespace of
  `src/runtime/Runtime.Engine.cpp`:
  - Hook adapters implementing `Core.FrameLoop` interfaces:
    `PlatformFrameHooks`, `OperationalTransitionHooks`,
    `RuntimeRenderFrameHooks`, `TransferHooks`, `StreamingHooks`,
    `AssetHooks`, plus `RuntimeFrameContext` and the frame-timing helpers
    (`ElapsedMicros`, `Delta`).
  - Per-frame helper functions: the selection pick lane
    (`BuildPickReadbackContextForFrame`,
    `RememberPickReadbackContextForFrame`,
    `DrainPendingSelectionPickForFrame`,
    `ApplySelectionReadbackToController`,
    `RefineSelectionReadbackForFrame`,
    `DrainCompletedSelectionReadbacksForFrame`), gizmo/selection input
    (`SubmitViewportSelectionClickForFrame`,
    `RebuildSelectedGizmoEntities`, `DriveGizmoInteractionForFrame`,
    `DriveGizmoAndSelectionInputForFrame`), simulation/camera
    (`RunFixedStepSimulationTicks`, `PopulateMainCameraForFrame`,
    `HasPendingPreRenderTransformFlush`), and input-action dispatch
    (`RuntimeInputActionTriggered`, `DispatchRuntimeInputActionsForFrame`).
- Helpers that are only used by import/scene/config code move with
  `RUNTIME-147..149` instead; if this task lands first, leave those in
  place. The series is intentionally order-independent; each task takes
  only its own helpers.
- Anonymous-namespace entities are TU-local, so moving them to a partition
  changes their linkage to module linkage. That is the intended mechanism;
  nothing may be exported from the partition.
- Part of the `Runtime.Engine` decomposition series (`RUNTIME-146..151`).

## Required changes
- [ ] Add `src/runtime/Runtime.Engine.FrameLoop.cppm` declaring
      `module Extrinsic.Runtime.Engine:FrameLoop` (non-exported partition)
      with the moved declarations, and move the definitions into a matching
      implementation unit (or define them in the partition unit if that
      matches the existing `Core.Tasks:Internal` pattern).
- [ ] Remove the moved code from `Runtime.Engine.cpp`; the primary
      implementation unit imports the partition.
- [ ] Register the new unit(s) in `src/runtime/CMakeLists.txt`
      (`FILE_SET CXX_MODULES` for the partition interface).
- [ ] Trim the import list of `Runtime.Engine.cpp` to what its remaining
      code needs; the partition carries the imports the moved helpers need.

## Tests
- [ ] No test changes expected; this is TU reorganization. The frame-loop
      contract tests (`Test.RuntimeSandboxAcceptance`,
      selection/gizmo/input-action contract tests,
      `Test.RenderWorldPoolEngineWiring`) pass unchanged.
- [ ] Default CPU gate stays green:
      `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine'`.

## Docs
- [ ] Regenerate module inventories per `intrinsicengine-docs-sync` if the
      inventory tracks partitions.
- [ ] Update `tasks/backlog/runtime/README.md` status line on retirement.

## Acceptance criteria
- [ ] `Runtime.Engine.cpp` contains no `Core.FrameLoop` hook adapter
      structs and none of the listed per-frame helpers.
- [ ] Nothing is exported from the `:FrameLoop` partition.
- [ ] CPU gate and layering check pass; a clean rebuild of the `ci` preset
      succeeds with clang-scan-deps resolving the partition.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Mixing this mechanical split with any behavior change to frame pacing,
  pick handling, gizmo interaction, or input dispatch.
- Exporting anything from the new partition.
- Touching code owned by `RUNTIME-146..149`/`RUNTIME-151`.

## Maturity
- Target: `Operational` — the frame loop is the engine's primary
  operational path and must stay green throughout. No new capability
  follow-up is owed.
