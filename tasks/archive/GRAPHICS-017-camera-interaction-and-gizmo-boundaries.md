# GRAPHICS-017 — Camera, interaction, and gizmo boundaries

## Status
- State: done.
- Owner/agent: local agent workflow.
- Activated: 2026-05-03 after `GRAPHICS-015` completion.
- Current slice: CPU/null camera/view snapshot contracts, frustum-plane extraction, pick-ray derivation, and data-only transform-gizmo render packets. Prefer snapshot contracts before runtime/platform integration.
- Completed slices in active work: `Graphics.CameraSnapshots`, `RenderFrameInput` camera/pick data, `RenderWorld` camera/gizmo snapshots, null-renderer handoff, and contract tests.
- Completed: 2026-05-03.
- PR/commit: 769aac8.
- Follow-up questions: `tasks/backlog/rendering/GRAPHICS-017Q-camera-gizmo-runtime-clarifications.md`.

## Goal
- Re-home or reimplement camera, interaction helper, and transform-gizmo behavior behind architecture-compliant runtime/platform/graphics boundaries.
## Non-goals
- No editor UI feature expansion.
- No selection pass implementation beyond interfaces needed for interaction handoff.
- No graphics ownership of gameplay/editor mutation.
## Context
- Owner: `src/runtime` and `src/platform` for input/orchestration, with `src/graphics/renderer` owning only camera/view packets consumed by rendering.
- Legacy camera, interaction, and transform gizmo modules are references for behavior and UX expectations, not code sources.
## Required changes
- [x] Define camera/view packet data consumed by graphics and input/update ownership outside graphics.
- [x] Define camera frustum-plane extraction and view/projection snapshot fields consumed by culling and picking.
- [x] Define transform-gizmo render/debug packets separately from mutation/application logic.
- [x] Document which layer owns camera motion, picking requests, gizmo hit testing, and transform application.
## Tests
- [x] Add unit/contract tests for camera packet defaults, projection/view validity, gizmo render packets, and invalid input handling.
- [x] Add runtime integration tests for input-to-pick-request handoff when implementation lands.
- [x] Label graphics-only contract tests `contract;graphics` and runtime input/handoff integration tests `integration;runtime;graphics` so both run in the default CPU gate.
## Docs
- [x] Update graphics, runtime, and platform boundary docs for camera/interaction/gizmo ownership.
## Acceptance criteria
- [x] Graphics receives camera/gizmo data as snapshots and never mutates scene ownership.
- [x] Culling and picking inputs come from camera/view snapshots, not platform input state or live camera objects.
- [x] Runtime/platform ownership of input and transform application is explicit.
- [x] Legacy interaction behavior has a promoted-task path without layer violations.
## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
- Verified locally with targeted `RenderWorldContract` tests and the default CPU-supported correctness gate on 2026-05-03.
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Putting input polling or transform application inside `src/graphics`.

## Follow-up cross-link
`GRAPHICS-024` (overlays/presentation/editor handoff planning) confirms that
the camera/view, pick-ray, and transform-gizmo packet contracts established
here are the promoted handoff for the matrix's editor-input, camera, and
gizmo rows. Runtime/editor/app owns mutation, hit testing, and transform
application; graphics consumes data-only snapshots. Continued runtime
clarifications live in `GRAPHICS-017Q`. See the overlay / presentation /
editor handoff inventory in
`../../docs/migration/nonlegacy-parity-matrix.md` for the per-row owner matrix.
This appendix does not modify acceptance criteria or completion metadata.
