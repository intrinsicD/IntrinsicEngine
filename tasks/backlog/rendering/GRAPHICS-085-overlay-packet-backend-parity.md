# GRAPHICS-085 — Overlay packet backend parity

## Goal
- Prove backend consumption only for the runtime-produced overlay packet classes retained by `RUNTIME-104`, including selection and outline behavior where those overlays participate in current workflows.

## Non-goals
- No runtime/editor overlay creation API; `RUNTIME-104` owns producer lifecycle.
- No geometry algorithm execution; runtime/geometry/method tasks own algorithms.
- No live ECS access from graphics.
- No broad property-buffer residency; `GRAPHICS-084` owns that seam.

## Context
- Owner/layer: `graphics` consumes immutable overlay/debug/visualization packets and records backend commands.
- Existing pieces include transient debug and visualization overlay upload helpers, line/point/surface passes, selection outline, and render command routing. Legacy overlay parity still needs an end-to-end proof for persistent overlay packet classes, selection eligibility, and vector-field parent/child invariants supplied by runtime.
- This task is downstream of `RUNTIME-104` and may reuse its runtime fixtures.

## Value gate
- Current state: graphics already consumes transient debug and visualization packets, records render command stats, and has selection outline infrastructure.
- Improvement: retained persistent overlay packets get backend evidence without graphics importing runtime/ECS or reviving immediate GPU upload from overlay creation.
- Scope decision: implement no new packet class unless `RUNTIME-104` proves existing packet lanes cannot cover the workflow.

## Required changes
- [ ] Extend packet validation or pass routing only where existing visualization/debug packet types cannot represent required overlay classes.
- [ ] Add backend command-shape coverage for line, point, optional triangle, and vector-field overlays.
- [ ] Ensure overlay selection eligibility reaches selection/outline lanes through immutable packet data.
- [ ] Add diagnostics for malformed overlay packets without querying runtime/ECS.
- [ ] Add opt-in Vulkan proof if CPU/null command-shape tests cannot prove visual parity.

## Tests
- [ ] Add `contract;graphics` tests for overlay packet validation, pass routing, command stats, and selection/outline eligibility.
- [ ] Add `integration;runtime;graphics` tests using `RUNTIME-104` producer fixtures.
- [ ] Add labelled `gpu;vulkan` smoke for overlay visual/readback proof when a Vulkan-capable host is available.

## Docs
- [ ] Update `src/graphics/renderer/README.md`, `docs/architecture/graphics.md`, and `docs/migration/nonlegacy-parity-matrix.md`.
- [ ] Update `tasks/backlog/rendering/README.md`.
- [ ] Regenerate module inventory if public module surfaces change.

## Acceptance criteria
- [ ] Retained runtime-produced overlay packets are consumed by graphics without live ECS/runtime imports.
- [ ] Overlay selection and outline eligibility are visible in command stats or readback tests.
- [ ] Legacy `Graphics.OverlayEntityFactory` backend behavior is either represented by retained packet-consumption evidence or explicitly retired by `RUNTIME-104`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Overlay|Visualization|Selection|Outline' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing runtime/ECS/editor state into graphics.
- Reintroducing immediate GPU upload from runtime overlay creation.

## Maturity
- Target: `CPUContracted` for command-shape parity; `Operational` on Vulkan-capable hosts through opt-in `gpu;vulkan` smoke.
