# Task 12 — Add missing task: overlays, presentation adjacency, editor handoff

- Status: completed (2026-05-02)
- Owner: Codex (current branch)
- Branch / PR: current branch / TBD
- Completion date: 2026-05-02
- Commit / PR: local split branch `split/current-working-tree-2026-05-02`; remote PR reference TBD.
- Follow-ups: implementation remains in `tasks/backlog/rendering/GRAPHICS-024-overlays-presentation-editor-handoff.md`.
- Next verification step: `python3 tools/agents/check_task_policy.py --root . --strict` and `python3 tools/docs/check_doc_links.py --root . --strict`.

---

You are working in https://github.com/intrinsicD/IntrinsicEngine.

Create a dedicated task for legacy overlay/presentation/editor-adjacent rendering ownership.

Create:

`tasks/backlog/rendering/GRAPHICS-024-overlays-presentation-editor-handoff.md`

## Goal

Assign promoted ownership for legacy overlay entity factory, presentation adjacency, and editor/UI rendering handoff without putting editor mutation or platform/window ownership into graphics.

## Context

Legacy modules include `Graphics.OverlayEntityFactory` and `Graphics.Presentation`. Existing docs include vectorfield/overlay lifecycle invariants. The promoted task chain does not yet clearly assign overlay factory and presentation/editor-UI handoff.

## Required scope

- Inventory legacy overlay/presentation/editor-adjacent modules.
- Decide owner for each:
  - runtime/editor/app owns overlay creation and mutation;
  - graphics owns render packets/passes/resources only;
  - platform owns window/input/surface.
- Define overlay snapshot packets:
  - line overlays
  - point overlays
  - triangle/debug overlays if needed
  - vector-field overlays
- Define destruction/lifecycle invariants:
  - parent/child cleanup
  - dirty-domain propagation
  - deterministic extraction ordering
  - selection-outline eligibility
- Cross-link:
  - GRAPHICS-010 debug primitives
  - GRAPHICS-011 spatial visualizers
  - GRAPHICS-014 visualization overlays
  - GRAPHICS-017 camera/interaction/gizmo
  - GRAPHICS-020 retirement gates

## Tests

- `contract;runtime;graphics` or `integration;runtime;graphics` tests for deterministic overlay extraction.
- `contract;graphics` tests for overlay packet defaults.
- No editor UI test implementation unless existing UI test seams make it small.

## Docs

- Update `docs/architecture/vectorfield-overlay-lifecycle-invariants.md` if ownership changes.
- Update `docs/migration/nonlegacy-parity-matrix.md`.

## Acceptance criteria

- Overlay/presentation/editor-adjacent legacy behavior has a clear promoted owner.
- Graphics does not own editor mutation, ECS mutation, or platform presentation policy.
- GRAPHICS-020 can map overlay/presentation modules to this task.

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
