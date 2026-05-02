# GRAPHICS-025 — Hybrid, transparent, and special-material forward path

## Goal
Define the follow-up architecture and implementation path for the real hybrid renderer beyond the current deferred-backed staging mode.

## Non-goals
- No implementation in the initial planning task.
- No shader changes.
- No clustered lighting, IBL, area-light, or full transparency/OIT implementation unless split into later subtasks.

## Context
`docs/architecture/rendering-three-pass.md` currently treats `Hybrid` as a deferred-backed staging mode. Future renderer work needs a clear owner for transparent materials, special forward-only materials, alpha blending/OIT decisions, and hybrid composition without bloating `GRAPHICS-008` or `GRAPHICS-009`. This task records that owner and keeps deferred, forward, and hybrid terminology unambiguous for future implementation work.

## Required changes
- Define hybrid path semantics:
  - deferred opaque base,
  - forward overlays for transparent and special materials,
  - line/point/debug overlays remain independent.
- Define material classification for:
  - opaque,
  - alpha-mask,
  - transparent,
  - unlit,
  - special forward-only.
- Define resource requirements for:
  - `SceneColorHDR`,
  - `SceneDepth`,
  - optional velocity/history buffers if future TAA or motion vectors are referenced,
  - optional OIT resources if selected by a later task.
- Define split points for later implementation subtasks if transparency/OIT scope grows.
- Cross-link decisions with `GRAPHICS-006`, `GRAPHICS-007`, `GRAPHICS-008`, `GRAPHICS-009`, and `GRAPHICS-013A`.
- Update the hybrid note in `docs/architecture/rendering-three-pass.md` to point here.

## Tests
- Initial planning work requires task policy and documentation link validation only.
- Future implementation subtasks must add `contract;graphics` tests for classification and recipe/resource contracts.
- Future GPU coverage should be optional `gpu;vulkan` smoke tests and must remain outside the default CPU gate.

## Docs
- Update `docs/architecture/rendering-three-pass.md` so the staged hybrid mode points to this task for real hybrid follow-up work.
- Update `docs/architecture/graphics.md` if material classification or pipeline ownership becomes public architecture.
- Update `docs/migration/nonlegacy-parity-matrix.md` when parity status changes.

## Acceptance criteria
- Hybrid is no longer an unowned TODO hidden in architecture prose.
- Deferred, forward, and hybrid terms are not ambiguous for future agent work.
- Transparent and special-material forward paths have a scoped owner without expanding `GRAPHICS-008` or `GRAPHICS-009`.

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
