# Task 1 — Create rendering backlog cleanup tracker

- Status: in-progress
- Owner: Claude (claude/next-active-task-yNINC)
- Branch / PR: `claude/next-active-task-yNINC`
- Next verification step: run `python3 tools/agents/check_task_policy.py --root . --strict` and `python3 tools/docs/check_doc_links.py --root . --strict` after creating `GRAPHICS-021`.

GRAPHICS-021 has been added at `tasks/backlog/rendering/GRAPHICS-021-rendering-backlog-workflow-cleanup.md`. The new file passes task validator strict mode (zero new findings); pre-existing findings in the `tasks/active/` queue files are tracked by Task 3.

---

You are working in https://github.com/intrinsicD/IntrinsicEngine.

Create a new structured task file:

`tasks/backlog/rendering/GRAPHICS-021-rendering-backlog-workflow-cleanup.md`

## Goal

Create a single backlog cleanup task that tracks rendering-task workflow fixes before feature implementation continues.

## Context

The rendering backlog now has GRAPHICS-001 through GRAPHICS-020, but several docs/tasks still contain stale references, ambiguous task IDs, missing dependency metadata, and incomplete verification commands. GRAPHICS-001 is the canonical rendering task index. The AGENTS contract and `docs/agent/task-format.md` are authoritative for task shape and verification expectations.

## Required changes

- Add GRAPHICS-021 with the standard task sections:
  - Goal
  - Non-goals
  - Context
  - Required changes
  - Tests
  - Docs
  - Acceptance criteria
  - Verification
  - Forbidden changes
- Scope GRAPHICS-021 to workflow/task/doc cleanup only.
- Explicitly list follow-up cleanup areas:
  1. RORG-031A/RORG-031B filename collision.
  2. Retire or cross-link stale RORG-031B rendering seed.
  3. Add configure step to GRAPHICS verification blocks.
  4. Fix stale legacy-todo.md references in docs.
  5. Add dependency metadata / README for rendering backlog.
  6. Split over-scoped GRAPHICS-013.
  7. Add missing task homes for rendergraph diagnostics, hot reload, overlays/presentation, hybrid path.
  8. Make GRAPHICS-016/runtime extraction a hard first implementation gate.
- Do not implement renderer features.

## Acceptance criteria

- GRAPHICS-021 exists and passes task policy validation.
- It is clear that it is a cleanup/meta-task, not a rendering implementation task.
- It points future agents to GRAPHICS-001 as the rendering backlog index.

## Verification

```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes

- No C++ behavior changes.
- No shader changes.
- No renderer implementation.
- No task moves except creating GRAPHICS-021.

Why: this gives the whole cleanup series one canonical anchor before the backlog is modified.
