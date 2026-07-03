---
id: RUNTIME-144
theme: F
depends_on: []
---
# RUNTIME-144 — Post-import processor and import UX-policy seam

## Goal
- Add an extension point to the import pipeline so post-import processing
  and import-time UX policy are registered by features/apps instead of
  hardcoded in `Runtime.Engine`: the direct-mesh normal-bake step, the
  camera-focus/auto-select behavior, entity authoring defaults, and the
  `F`-key focus binding all move behind registrable seams.

## Non-goals
- No change to the normal-bake algorithm or its scheduling domain — the
  CPU→GPU re-domaining is owned by `RUNTIME-129`; this task changes *who
  registers* the step, not what it does. Coordinate: if `RUNTIME-129` lands
  first, its GPU bake request enqueues through this seam; if this lands
  first, `RUNTIME-129` re-domains the registered processor.
- No import format/decode changes (`RUNTIME-142` owns async IO routing).
- No keybinding system beyond routing the existing `F` command through a
  registrable input-action/command seam.

## Context
- Owner/layer: `runtime` (import materialization, frame-loop input
  handling); registrations move to the feature/app side (today the sandbox
  editor; `ARCH-006` may relocate the registrant later).
- Hardcoded today:
  - `QueueDirectMeshPostProcess` runs for every direct mesh import and
    bakes a generated normal texture with fixed options
    (`options.SourcePropertyName = "v:normal"`, 64×64) and hardcoded
    object-space material bindings
    (`src/runtime/Runtime.Engine.cpp:1293-1373, 1435-1560, 1720-1728`).
  - Import materialization applies editor UX policy: camera focus +
    auto-select on every import (`:3866-3873, 4266-4272`) and authoring
    defaults (`SelectableTag`, `RenderSurface`, white `VisualizationConfig`,
    `:1696-1728`).
  - `RunFrame` hardcodes the `F`-key focus-on-selection edge
    (`:2938-2945, 653-680`).
- Origin: `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  findings R4, R8.

## Required changes
- [ ] Add a post-import processor registry: ordered processors registered
      per geometry family, invoked from the materialization path with the
      decoded payload/entity, executing over the existing
      `StreamingExecutor` deferral (the current post-process already runs
      there — the seam formalizes registration, not a new lane).
- [ ] Move the direct-mesh generated-normal step into a registered
      processor owned by the feature; default sandbox composition registers
      it so behavior is unchanged.
- [ ] Add an import-completed event/callback carrying the created entities;
      move camera-focus/auto-select into a sandbox-registered handler;
      make authoring defaults a registrable policy with the current values
      as sandbox defaults.
- [ ] Route the `F` focus command through a registered input-action/command
      binding (sandbox registers `F` → `FocusCameraOnSelection`); `RunFrame`
      keeps only the generic dispatch.

## Tests
- [ ] Contract: with sandbox registrations, import behavior is unchanged
      (normal bake queued, focus/selection applied, defaults present) —
      pinned against existing BUG-044/BUG-048/BUG-050 regressions.
- [ ] Contract: with no registrations, an import materializes geometry with
      no bake, no focus/selection mutation, and minimal authoring defaults.
- [ ] Contract: processor ordering is deterministic and a failing processor
      fail-closes its own step without corrupting the import.

## Docs
- [ ] Update `src/runtime/README.md` import-pipeline extension contract.
- [ ] Update `docs/architecture/runtime.md` (frame-order step 4 wording for
      the focus command becomes "dispatch registered input actions").

## Acceptance criteria
- [ ] `Runtime.Engine` import materialization contains no method-specific
      bake options, no editor UX policy, and no hardcoded key checks
      (grep-verified for the moved identifiers).
- [ ] Sandbox behavior byte-identical for the fixture imports.
- [ ] Default CPU gate green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Changing bake outputs, focus math, or default component values while
  moving them.
- Blocking import on processor completion (deferral semantics preserved).
- Inventing a general keybinding/config system beyond the single routed
  command.
