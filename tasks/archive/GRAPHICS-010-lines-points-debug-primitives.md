# GRAPHICS-010 — Lines, points, and transient debug primitives

## Status
- State: done.
- Owner/agent: local agent workflow.
- Activated: 2026-05-03 after `GRAPHICS-009` completion.
- Completed: 2026-05-03.
- PR/commit: pending local commit.
- Completed slice: line/point bucket command contracts, transient debug line/point/triangle packet snapshots, width/radius/coordinate sanitization, explicit line/point frame recipe cull resources, docs, and CPU/mock contract tests.
- Follow-up questions: `tasks/backlog/rendering/GRAPHICS-010Q-transient-debug-backend-clarifications.md`.

## Goal
- Complete line, point, and transient debug primitive rendering contracts through non-legacy pass APIs.
## Non-goals
- No selection/picking implementation.
- No persistent editor overlay entity factory work.
- No direct ECS-owned debug state in graphics.
## Context
- Owner: `src/graphics/renderer` and `src/graphics/renderer/Passes`.
- Legacy line, point, and debug draw modules are behavioral references for primitive coverage and robustness expectations.
## Required changes
- [x] Fill `Pass.Forward.Line` and `Pass.Forward.Point` command/resource behavior.
- [x] Consume the same canonical renderable-instance, transform, material, geometry, and bounds/culling contracts used by surface lanes where applicable.
- [x] Define transient debug line/point/triangle packet APIs in the render snapshot contract if missing.
- [x] Clamp widths/radii and reject invalid coordinates before command submission.
## Tests
- [x] Add mock command-context tests for line buckets, point buckets, debug primitive packets, clamping, empty scenes, and invalid data.
- [x] Add line/point bucket tests that verify canonical instance-slot indexing and transform/material lookup.
- [x] Cover graph, mesh-helper, point-cloud, and transient debug use cases at the contract level.
- [x] Label these CPU/mock-backend tests `contract;graphics` so they run in the default CPU gate.
## Docs
- [x] Document transient debug rendering versus persistent overlay ownership.
## Acceptance criteria
- [x] Lines, points, and transient debug primitives render through non-legacy pass contracts.
- [x] Invalid primitive data is rejected or skipped deterministically.
- [x] Tests prove overlays accumulate onto the expected HDR/depth resources.
## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests -j 4
ctest --test-dir build/ci --output-on-failure -R 'RenderWorldContract|FrameRecipeContract|GraphicsLinePointPassContracts' --timeout 60
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Reintroducing live gameplay/editor ownership into graphics.

## Follow-up cross-link
`GRAPHICS-024` (overlays/presentation/editor handoff planning) confirms that the
transient debug line/point/triangle packet contracts established here are the
canonical path for non-persistent overlays. Persistent overlay entities are
owned by runtime/editor/app and are extracted as `OverlayLine/Point/Triangle`
snapshots routed through `Graphics.VisualizationPackets`, not through the
transient debug primitive path defined in this task. See the overlay /
presentation / editor handoff inventory in
`../../docs/migration/nonlegacy-parity-matrix.md` for the per-row owner matrix.
This appendix does not modify acceptance criteria or completion metadata.
