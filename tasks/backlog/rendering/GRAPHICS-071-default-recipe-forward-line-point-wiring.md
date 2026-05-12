# GRAPHICS-071 — Default-recipe `Pass.Forward.Line` and `Pass.Forward.Point` wiring

## Goal
- Wire the existing `Pass.Forward.Line` and `Pass.Forward.Point` pass classes into the renderer executor under the default recipe: pipeline creation at renderer init / `RebuildOperationalResources()`, instance ownership on `NullRenderer`, and per-pass routing through new `RecordForwardLinePass(...)` / `RecordForwardPointPass(...)` helpers consuming the `Lines` and `Points` cull buckets.

## Non-goals
- No transient debug-line/debug-point overlay (those are `GRAPHICS-077`, the per-frame host-visible upload helper).
- No vector-field / isoline overlay (`GRAPHICS-078`).
- No selection / picking (`GRAPHICS-074`).
- No new shaders; reuses `assets/shaders/line.{vert,frag}` and `assets/shaders/point*.{vert,frag}` per `GRAPHICS-010` decisions.

## Context
- Status: not started.
- Owner/layer: `graphics/renderer`.
- Planning anchors: `tasks/done/GRAPHICS-010-lines-points-debug-primitives.md`, `tasks/done/GRAPHICS-010Q-transient-debug-backend-clarifications.md`.
- Today: `Pass.Forward.Line.cpp` and `Pass.Forward.Point.cpp` exist with command-body shells, but `NullRenderer` never owns them, never sets pipelines on them, and the executor lambda has no branch.
- The `Lines` and `Points` cull buckets are already reserved by `GRAPHICS-007` and emitted by the `CullingSystem`.
- This task targets retained `GpuRender_Line` / `GpuRender_Point` renderables only (the transient debug expansion path is `GRAPHICS-077`).

## Required changes
- [ ] Add `m_ForwardLinePass`, `m_ForwardPointPass`, `m_ForwardLinePipelineLease`, `m_ForwardPointPipelineLease` members to `NullRenderer`.
- [ ] In `InitializeOperationalPassResources(device)`, create:
  - line pipeline from `assets/shaders/line.vert` + `line.frag`,
  - point pipeline from the recorded canonical point variant (`point.vert` + `point_retained.frag` or `point_flatdisc.frag` per `GRAPHICS-010` decisions; the implementer picks the canonical variant and documents it).
- [ ] Republish both pipelines byte-identical from `RebuildOperationalResources()`.
- [ ] Add `"Pass.Forward.Line"` and `"Pass.Forward.Point"` branches in the executor lambda routing to `RecordForwardLinePass(...)` and `RecordForwardPointPass(...)` helpers with the `SkippedNonOperational` / `SkippedUnavailable` / `Recorded` taxonomy.

## Tests
- [ ] `contract;graphics` test: with retained line renderables, the executor records `Pass.Forward.Line` with the expected bind/draw shape.
- [ ] `contract;graphics` test: with retained point renderables, the executor records `Pass.Forward.Point` similarly.
- [ ] `contract;graphics` test: empty cull buckets → both passes return `SkippedUnavailable`.
- [ ] `contract;graphics` test: pipeline leases survive `RebuildGpuResources()`.

## Docs
- [ ] Update `src/graphics/renderer/README.md` to record both passes as operationally wired under the default recipe.

## Acceptance criteria
- [ ] Both passes record draws in the operational state and increment `Recorded`.
- [ ] No regression in transient debug packet handling (still routes through the planned `GRAPHICS-077` path).

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
- Land the pipelines + executor routes, exercise the contract tests above.
