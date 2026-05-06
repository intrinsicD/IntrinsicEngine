# GRAPHICS-009Q — Lighting and shadow clarification follow-ups

## Status
- State: in-progress.
- Owner/agent: local agent workflow.
- Activated: 2026-05-06 after `GRAPHICS-008Q` retirement cleared `tasks/active/`.
- Branch: `claude/agentic-workflow-session-ft1YJ`.
- Next verification step: run `python3 tools/agents/check_task_policy.py --root . --strict` and `python3 tools/docs/check_doc_links.py --root .` after recording decisions and syncing them into `docs/architecture/rendering-three-pass.md`.

## Goal
- Clarify remaining non-code decisions from `GRAPHICS-009` before Vulkan/backend-specific lighting and shadow work depends on them.

## Non-goals
- No C++ behavior changes.
- No Vulkan-only implementation.
- No new light types, clustered lighting, IBL, or area lights.

## Context
- Owner: graphics renderer architecture docs and future backend integration notes.
- `GRAPHICS-009` established CPU/null-testable contracts for light packet diagnostics, shadow params/cascade metadata, shadow-pass command gating, and fullscreen deferred lighting command recording.
- The remaining questions are documentation/decision records so later backend work can wire resources without changing ownership boundaries.

## Required changes
- Decide whether frame-recipe shadow atlas sizing remains viewport-sized until backend integration or receives a typed shadow-sizing input separate from `FrameRecipeFeatures`.
- Clarify the exact backend binding seam for `ShadowAtlas` sampler state and comparison mode.
- Clarify runtime/shadow extraction ownership for texel-snapped cascade view-projection calculation and missing shadow-caster diagnostics.
- Clarify whether deferred lighting push constants remain scene-table-only or gain a typed debug/lighting-mode field in a later task.

## Tests
- Documentation/checker only; no C++ tests required unless docs tooling changes.

## Docs
- Update `docs/architecture/rendering-three-pass.md` and any backend integration doc that references shadow atlas sizing or binding.

## Acceptance criteria
- Follow-up decisions are captured without changing implementation behavior.
- Later Vulkan/backend tasks can consume the documented shadow/lighting seams without live ECS access or graphics-layer ownership violations.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing docs-only clarification with semantic C++ behavior edits.
- Reintroducing live ECS light or shadow-caster access into graphics.
- Making GPU/Vulkan tests part of the default CPU-supported gate.

