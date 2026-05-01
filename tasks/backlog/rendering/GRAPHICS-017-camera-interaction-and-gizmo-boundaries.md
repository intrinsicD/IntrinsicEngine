# GRAPHICS-017 — Camera, interaction, and gizmo boundaries
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
- Define camera/view packet data consumed by graphics and input/update ownership outside graphics.
- Define camera frustum-plane extraction and view/projection snapshot fields consumed by culling and picking.
- Define transform-gizmo render/debug packets separately from mutation/application logic.
- Document which layer owns camera motion, picking requests, gizmo hit testing, and transform application.
## Tests
- Add unit/contract tests for camera packet defaults, projection/view validity, gizmo render packets, and invalid input handling.
- Add runtime integration tests for input-to-pick-request handoff when implementation lands.
## Docs
- Update graphics, runtime, and platform boundary docs for camera/interaction/gizmo ownership.
## Acceptance criteria
- Graphics receives camera/gizmo data as snapshots and never mutates scene ownership.
- Culling and picking inputs come from camera/view snapshots, not platform input state or live camera objects.
- Runtime/platform ownership of input and transform application is explicit.
- Legacy interaction behavior has a promoted-task path without layer violations.
## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Putting input polling or transform application inside `src/graphics`.
