# GRAPHICS-071 — Default-recipe `Pass.Forward.Line` and `Pass.Forward.Point` wiring

## Goal
- Wire the existing `Pass.Forward.Line` and `Pass.Forward.Point` pass classes into the renderer executor under the default recipe: pipeline creation at renderer init / `RebuildOperationalResources()`, instance ownership on `NullRenderer`, and per-pass routing through new `RecordForwardLinePass(...)` / `RecordForwardPointPass(...)` helpers consuming the `Lines` and `Points` cull buckets.

## Non-goals
- No transient debug-line/debug-point overlay (those are `GRAPHICS-077`, the per-frame host-visible upload helper).
- No vector-field / isoline overlay (`GRAPHICS-078`).
- No selection / picking (`GRAPHICS-074`).
- No new shaders; reuses `assets/shaders/line.{vert,frag}` and `assets/shaders/point*.{vert,frag}` per `GRAPHICS-010` decisions.

## Context
- Status: done.
- Completed: 2026-05-18.
- PR/commit: this local GRAPHICS-071 retirement change.
- Maturity: CPUContracted for the default CPU/mock command path; Vulkan-visible default-recipe parity remains owned by later GRAPHICS-076/081 smoke coverage.
- Owner/layer: `graphics/renderer`.
- Planning anchors: `tasks/archive/GRAPHICS-010-lines-points-debug-primitives.md`, `tasks/archive/GRAPHICS-010Q-transient-debug-backend-clarifications.md`.
- Today: `Pass.Forward.Line.cpp` and `Pass.Forward.Point.cpp` exist with command-body shells, but `NullRenderer` never owns them, never sets pipelines on them, and the executor lambda has no branch.
- The `Lines` and `Points` cull buckets are already reserved by `GRAPHICS-007` and emitted by the `CullingSystem`.
- This task targets retained `GpuRender_Line` / `GpuRender_Point` renderables only (the transient debug expansion path is `GRAPHICS-077`).

## Required changes
- [x] Add `m_ForwardLinePass`, `m_ForwardPointPass`, `m_ForwardLinePipelineLease`, `m_ForwardPointPipelineLease` members to `NullRenderer`.
- [x] In `InitializeOperationalPassResources(device)`, create:
  - line pipeline from `assets/shaders/line.vert` + `line.frag`,
  - point pipeline from the retained-renderable canonical variant (`point.vert` + `point_retained.frag`).
- [x] Republish both pipelines byte-identical from `RebuildOperationalResources()`.
- [x] Add default-recipe `"LinePass"` and `"PointPass"` branches in the executor lambda routing to `RecordForwardLinePass(...)` and `RecordForwardPointPass(...)` helpers with the `SkippedNonOperational` / `SkippedUnavailable` / `Recorded` taxonomy.

## Tests
- [x] `contract;graphics` test: retained line pass routing records the expected bind/draw shape through `LinePass`.
- [x] `contract;graphics` test: retained point pass routing records the expected bind/draw shape through `PointPass`.
- [x] `contract;graphics` test: unavailable cull output → both passes return `SkippedUnavailable`.
- [x] `contract;graphics` test: pipeline leases survive `RebuildOperationalResources()`.

## Docs
- [x] Update `src/graphics/renderer/README.md` to record both passes as operationally wired under the default recipe.

## Acceptance criteria
- [x] Both passes record draws in the operational state and increment `Recorded`.
- [x] No regression in transient debug packet handling (still routes through the planned `GRAPHICS-077` path).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Routing transient debug primitives through retained line/point cull buckets (per `GRAPHICS-010Q`).
- Mutating `BuildDefaultFrameRecipe` resource declarations.
- Mixing mechanical file moves with semantic refactors.

## Next verification step
- Next task in the rendering DAG is `GRAPHICS-073` before deferred `GRAPHICS-072`, because `GRAPHICS-072` depends on `GRAPHICS-073`.
