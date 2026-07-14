# GRAPHICS-025 — Hybrid, transparent, and special-material forward path

- Status: completed (2026-06-03; planning-only; `Scaffolded`).
- Owner / agent: graphics renderer architecture planning.
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired.

## Goal
Define the follow-up architecture and implementation path for the real hybrid renderer beyond the current deferred-backed staging mode.

## Non-goals
- No implementation in the initial planning task.
- No shader changes.
- No clustered lighting, IBL, area-light, or full transparency/OIT implementation unless split into later subtasks.
- No implementation child task is opened here; this slice intentionally stops
  after recording the ownership contract and split points.

## Context
`docs/architecture/rendering-three-pass.md` currently treats `Hybrid` as a deferred-backed staging mode. Future renderer work needs a clear owner for transparent materials, special forward-only materials, alpha blending/OIT decisions, and hybrid composition without bloating `GRAPHICS-008` or `GRAPHICS-009`. This task records that owner and keeps deferred, forward, and hybrid terminology unambiguous for future implementation work.

## Required changes
- [x] Define hybrid path semantics:
  - [x] deferred opaque base,
  - [x] forward overlays for transparent and special materials,
  - [x] line/point/debug overlays remain independent.
- [x] Define material classification for:
  - [x] opaque,
  - [x] alpha-mask,
  - [x] transparent,
  - [x] unlit,
  - [x] special forward-only.
- [x] Define resource requirements for:
  - [x] `SceneColorHDR`,
  - [x] `SceneDepth`,
  - [x] optional velocity/history buffers if future TAA or motion vectors are referenced,
  - [x] optional OIT resources if selected by a later task.
- [x] Define split points for later implementation subtasks if transparency/OIT scope grows.
- [x] Cross-link decisions with `GRAPHICS-006`, `GRAPHICS-007`, `GRAPHICS-008`, `GRAPHICS-009`, and `GRAPHICS-013A`.
- [x] Update the hybrid note in `docs/architecture/rendering-three-pass.md` to point here.

## Tests
- [x] Initial planning work requires task policy and documentation link validation only.
- [x] Future implementation subtasks must add `contract;graphics` tests for classification and recipe/resource contracts.
- [x] Future GPU coverage should be optional `gpu;vulkan` smoke tests and must remain outside the default CPU gate.

## Docs
- [x] Update `docs/architecture/rendering-three-pass.md` so the staged hybrid mode points to this task for real hybrid follow-up work.
- [x] Update `docs/architecture/graphics.md` if material classification or pipeline ownership becomes public architecture.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` when parity status changes.

## Acceptance criteria
- [x] Hybrid is no longer an unowned TODO hidden in architecture prose.
- [x] Deferred, forward, and hybrid terms are not ambiguous for future agent work.
- [x] Transparent and special-material forward paths have a scoped owner without expanding `GRAPHICS-008` or `GRAPHICS-009`.

## Completion
- Completed: 2026-06-03.
- Commit reference: this task-retirement commit.
- Maturity: `Scaffolded`. This is the intended stop state because the task is
  planning-only and forbids renderer/shader implementation.
- Decisions:
  - Hybrid remains deferred-backed in the current implementation. The future
    real hybrid path starts with the deferred opaque base, then adds a forward
    surface overlay for transparent and special forward-only materials.
  - Line, point, transient debug, visualization, ImGui, debug-view, postprocess,
    selection-outline, and present overlays keep their existing owners and are
    not merged into the future hybrid surface overlay.
  - Material classifications are `Opaque`, `AlphaMask`, `Transparent`, `Unlit`,
    and `SpecialForwardOnly`.
  - `SceneColorHDR` and `SceneDepth` are the only fixed resources for the future
    hybrid surface overlay. Velocity/history and OIT resources remain optional
    child-task decisions.
  - Later implementation split points are material classification/runtime
    validation, frame-recipe/pass declaration, transparency sorting or OIT
    policy, and transparent/special-forward selection eligibility.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes
- No implementation.
- No shader changes.
- No lighting feature expansion.
- No broad renderer pipeline rewrite.
