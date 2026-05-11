# GRAPHICS-013 — Post-process, debug view, ImGui, and present (umbrella)

## Status
- State: done (superseded planning umbrella).
- Owner/agent: local agent workflow.
- Retired: 2026-05-07.
- Branch: `claude/agentic-workflow-session-xssSg`.
- Retirement commit: pending (this commit moves the file from `tasks/backlog/rendering/` to `tasks/done/` and redirects the rendering backlog README link).
- Superseded by: split implementation tasks
  `tasks/done/GRAPHICS-013A-postprocess-chain.md`,
  `tasks/done/GRAPHICS-013B-debug-view-and-render-target-inspection.md`, and
  `tasks/done/GRAPHICS-013C-imgui-overlay-and-present.md`, each with their
  own retired clarification follow-up
  (`GRAPHICS-013AQ`, `GRAPHICS-013BQ`, `GRAPHICS-013CQ`). The split decision
  is recorded in `tasks/done/task-09-split-graphics-013.md`.
- Rationale for retirement: every split implementation task and its
  clarification follow-up have already been retired to `tasks/done/`; the
  umbrella file has no remaining executable scope and is preserved here only
  as a historical pointer to the split children. Keeping it in the rendering
  backlog would mislead agent next-task selection.
- Verification: `python3 tools/agents/check_task_policy.py --root . --strict`
  and `python3 tools/docs/check_doc_links.py --root .`.

## Goal
- Keep a high-level planning index for the formerly over-scoped rendering follow-up area.
- Delegate implementation planning to split tasks with narrow ownership boundaries.
## Non-goals
- No implementation details in this umbrella task.
- No renderer, shader, or pass code changes.
## Context
- This task was originally over-scoped and bundled multiple independent feature families.
- Split ownership now lives in GRAPHICS-013A/B/C for reviewability and execution sequencing.
## Required changes
- [x] Use this file as an index only.
- [x] Plan and execute implementation through:
  - [x] `GRAPHICS-013A` postprocess chain.
  - [x] `GRAPHICS-013B` debug-view and render-target inspection.
  - [x] `GRAPHICS-013C` ImGui overlay and present/finalization.
## Tests
- [x] N/A in this umbrella file; tests are defined by split tasks.
## Docs
- [x] Keep cross-links synchronized with GRAPHICS-001 and rendering README DAG/order.
## Acceptance criteria
- [x] GRAPHICS-013A/B/C collectively replace implementation scope previously attached to GRAPHICS-013.
- [x] Each split task owns one coherent feature family with explicit boundaries.
## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```
## Forbidden changes
- Re-expanding this umbrella into implementation detail.
- Mixing unrelated feature families back into one task.
