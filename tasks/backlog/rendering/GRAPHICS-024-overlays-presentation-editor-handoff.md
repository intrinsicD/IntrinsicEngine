# GRAPHICS-024 — Overlays, presentation adjacency, and editor handoff

## Goal
Assign promoted ownership for legacy overlay entity factory behavior, presentation adjacency, and editor/UI rendering handoff without putting editor mutation or platform/window ownership into graphics.

## Non-goals
- No editor feature expansion.
- No platform/window/swapchain policy transfer into `src/graphics/renderer`.
- No legacy code copying.
- No renderer implementation in this planning task.

## Context
Legacy modules include overlay and presentation-adjacent behavior that touches rendering, runtime/editor mutation, platform presentation, selection, and visualization. Promoted graphics must consume snapshots/views and render packets only; runtime/editor/app owns scene and editor mutation, and platform owns window/surface concerns. This task gives that boundary a dedicated backlog owner and links it to `GRAPHICS-010`, `GRAPHICS-011`, `GRAPHICS-014`, `GRAPHICS-017`, and `GRAPHICS-020`.

## Required changes
- Inventory legacy overlay, presentation, and editor-adjacent rendering modules and behaviors.
- Decide owner for each behavior:
  - runtime/editor/app owns overlay creation and mutation;
  - graphics owns immutable overlay render packets, passes, and GPU resources;
  - platform owns window/input/surface policy.
- Define overlay snapshot packets for:
  - line overlays,
  - point overlays,
  - triangle/debug overlays if needed,
  - vector-field overlays.
- Define destruction/lifecycle invariants for:
  - parent/child cleanup,
  - dirty-domain propagation,
  - deterministic extraction ordering,
  - selection-outline eligibility.
- Cross-link ownership decisions with `GRAPHICS-010`, `GRAPHICS-011`, `GRAPHICS-014`, `GRAPHICS-017`, and `GRAPHICS-020`.

## Tests
- Add `contract;runtime;graphics` or `integration;runtime;graphics` tests for deterministic overlay extraction.
- Add `contract;graphics` tests for overlay packet defaults and invalid packet diagnostics.
- Do not add editor UI tests unless existing UI test seams make them small and deterministic.
- Keep the default gate CPU-only; Vulkan/GPU smoke tests are optional.

## Docs
- Update `docs/architecture/vectorfield-overlay-lifecycle-invariants.md` if ownership or lifecycle invariants change.
- Update `docs/architecture/graphics.md` if new promoted overlay packet contracts become public.
- Update `docs/migration/nonlegacy-parity-matrix.md` when overlay/presentation parity status changes.

## Acceptance criteria
- Overlay, presentation-adjacent, and editor-adjacent legacy behavior has clear promoted ownership.
- Graphics does not own editor mutation, ECS mutation, platform presentation policy, or window/input state.
- `GRAPHICS-020` can map legacy overlay/presentation modules to this task during retirement gating.
- Runtime/editor-to-graphics handoff remains snapshot/packet based and deterministic.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- No editor feature expansion.
- No platform/window ownership in graphics.
- No legacy code copying.
- No live ECS ownership in promoted graphics APIs.
