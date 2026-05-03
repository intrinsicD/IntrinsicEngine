# GRAPHICS-006Q — Material registry clarification backlog

## Goal
Track open clarification questions that should be resolved before completing the remaining `GRAPHICS-006` material-slot and shader-asset ownership slices.

## Non-goals
- No code changes in this clarification task.
- No material editor UI decisions.
- No Vulkan-only shader compilation policy changes.

## Context
- Owner: `src/graphics/renderer` and `src/graphics/rhi` policy boundaries.
- Created from the 2026-05-03 `GRAPHICS-006` CPU-only `RHI::PipelineRegistry` slice.

## Required changes
- Decide whether promoted material-slot allocation should live in a renderer-level registry or remain in `Graphics.MaterialSystem` with additional layout helpers.
- Decide the canonical material parameter layout versioning policy for texture/bindless references before `GRAPHICS-015` GPU texture residency lands.
- Decide whether shader identity should remain path-based or migrate to `Asset.Registry` IDs after asset hot-reload ownership is finalized.

## Tests
- No tests required for this clarification-only backlog task.

## Docs
- Update `docs/architecture/graphics.md` and dependent rendering backlog tasks after decisions are made.

## Acceptance criteria
- Each question has a documented decision or a follow-up implementation task.
- `GRAPHICS-006` remaining scope can be completed without adding cross-layer ownership ambiguity.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes
- No C++ behavior changes.
- No mechanical moves.

