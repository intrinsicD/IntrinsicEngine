# GRAPHICS-010 — Lines, points, and transient debug primitives

## Status
- State: in-progress.
- Owner/agent: local agent workflow.
- Activated: 2026-05-03 after `GRAPHICS-009` completion.
- Current slice: promoted from backlog; implementation not started in this handoff.

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
- Fill `Pass.Forward.Line` and `Pass.Forward.Point` command/resource behavior.
- Consume the same canonical renderable-instance, transform, material, geometry, and bounds/culling contracts used by surface lanes where applicable.
- Define transient debug line/point/triangle packet APIs in the render snapshot contract if missing.
- Clamp widths/radii and reject invalid coordinates before command submission.
## Tests
- Add mock command-context tests for line buckets, point buckets, debug primitive packets, clamping, empty scenes, and invalid data.
- Add line/point bucket tests that verify canonical instance-slot indexing and transform/material lookup.
- Cover graph, mesh-helper, point-cloud, and transient debug use cases at the contract level.
- Label these CPU/mock-backend tests `contract;graphics` so they run in the default CPU gate.
## Docs
- Document transient debug rendering versus persistent overlay ownership.
## Acceptance criteria
- Lines, points, and transient debug primitives render through non-legacy pass contracts.
- Invalid primitive data is rejected or skipped deterministically.
- Tests prove overlays accumulate onto the expected HDR/depth resources.
## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Reintroducing live gameplay/editor ownership into graphics.
