---
id: GRAPHICS-085
theme: F
depends_on: [RUNTIME-104]
---
# GRAPHICS-085 — Overlay packet backend parity

## Goal
- Prove backend consumption only for the runtime-produced overlay packet lanes retained after `RUNTIME-104`, including selection and outline behavior where those lanes participate in current workflows.

## Non-goals
- No runtime/editor overlay creation API; `RUNTIME-104` owns producer lifecycle.
- No geometry algorithm execution; runtime/geometry/method tasks own algorithms.
- No live ECS access from graphics.
- No broad property-buffer residency; `GRAPHICS-084` owns that seam.

## Context
- Owner/layer: `graphics` consumes immutable overlay/debug/visualization packets and records backend commands.
- Existing pieces include transient debug and visualization overlay upload helpers, line/point/surface passes, selection outline, and render command routing. `RUNTIME-104` retired a separate persistent derived-overlay producer for current workflows: mesh/graph/point child overlays map to ordinary `GeometrySources` entities or mesh primitive-view sidecars, and vector-field/isoline overlays use data-only visualization packets without child ECS entities.
- This task is downstream of `RUNTIME-104` and scopes against that classification rather than adding a new runtime/editor overlay creation API.

## Value gate
- Current state: graphics already consumes transient debug and visualization packets, records render command stats, and has selection outline infrastructure.
- Improvement: retained overlay-like packet lanes get backend evidence without graphics importing runtime/ECS or reviving immediate GPU upload from overlay creation.
- Scope decision: implement no new packet class unless a current workflow proves existing visualization/debug packet lanes cannot cover it.

## Required changes
- [ ] Extend packet validation or pass routing only where existing visualization/debug packet types cannot represent required overlay classes.
- [ ] Add backend command-shape coverage for retained transient debug line, point, triangle, visualization vector-field, and isoline overlay lanes.
- [ ] Ensure selection/outline eligibility for ordinary renderable/primitive-view overlays remains covered by immutable runtime snapshots; packet-only visualization overlays remain visual-only unless this task adds immutable selection metadata.
- [ ] Add diagnostics for malformed overlay packets without querying runtime/ECS.
- [ ] Add opt-in Vulkan proof if CPU/null command-shape tests cannot prove visual parity.

## Tests
- [ ] Add `contract;graphics` tests for overlay packet validation, pass routing, command stats, and selection/outline eligibility.
- [ ] Add `integration;runtime;graphics` tests using the `RUNTIME-104` classification and existing runtime visualization/primitive-view fixtures.
- [ ] Add labelled `gpu;vulkan` smoke for overlay visual/readback proof when a Vulkan-capable host is available.

## Docs
- [ ] Update `src/graphics/renderer/README.md`, `docs/architecture/graphics.md`, and `docs/migration/nonlegacy-parity-matrix.md`.
- [ ] Update `tasks/backlog/rendering/README.md`.
- [ ] Regenerate module inventory if public module surfaces change.

## Acceptance criteria
- [ ] Retained runtime-produced overlay/debug/visualization packets are consumed by graphics without live ECS/runtime imports.
- [ ] Selection and outline eligibility for retained selectable overlay-like renderables is visible in command stats or readback tests; packet-only visualization overlays are explicitly visual-only if no selection metadata is added.
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
