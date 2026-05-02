# Task 2 — Resolve RORG-031 collision and stale RORG-031B seed

- Status: completed
- Owner: Claude (claude/next-active-task-CaTBY)
- Branch / PR: `claude/next-active-task-CaTBY`
- Completion date: 2026-05-02
- Commit / PR: local split branch `split/current-working-tree-2026-05-02`; remote PR reference TBD.
- Follow-ups: none for this queue item; superseded rendering seed is archived as `tasks/done/RORG-031B-rendering-pipeline-backlog-seed.md`.
- Next verification step: run `python3 tools/agents/check_task_policy.py --root . --strict` and `python3 tools/docs/check_doc_links.py --root . --strict` after renames and link updates.

The architecture seed has been renamed to `tasks/backlog/architecture/RORG-031A-architecture-foundation.md` and the rendering seed has been retired to `tasks/done/RORG-031B-rendering-pipeline-backlog-seed.md` with completion metadata. GRAPHICS-001 now records RORG-031B as a superseded historical seed. Both validators pass without new findings; pre-existing findings in the `tasks/active/` queue files remain tracked by Task 3.

---

You are working in https://github.com/intrinsicD/IntrinsicEngine.

Fix the RORG-031A/RORG-031B task collision and stale rendering seed.

## Problem

There are two files with the bare filename prefix `RORG-031`:

- `tasks/backlog/architecture/RORG-031-architecture-foundation.md`
- `tasks/backlog/rendering/RORG-031-rendering-pipeline.md`

Their headings disambiguate them as RORG-031A and RORG-031B, but filenames do not. The rendering seed is also superseded by GRAPHICS-001 through GRAPHICS-020.

## Required changes

1. Rename the architecture seed file to:

   `tasks/backlog/architecture/RORG-031A-architecture-foundation.md`

2. Rename or retire the rendering seed:

   Preferred option:
   - Move `tasks/backlog/rendering/RORG-031-rendering-pipeline.md` to:
     `tasks/done/RORG-031B-rendering-pipeline-backlog-seed.md`
   - Add completion metadata in the done task:
     - completion date
     - commit reference placeholder
     - note that it is superseded by GRAPHICS-001 through GRAPHICS-020.

   Acceptable alternative:
   - Rename it to `tasks/backlog/rendering/RORG-031B-rendering-pipeline.md`
   - Add a clear note that GRAPHICS-001 is the canonical rendering index.

3. Update every markdown link/reference to the old filenames.
4. Ensure GRAPHICS-001 references the final disposition of RORG-031B:
   - If moved to done: mention it as superseded historical seed.
   - If kept in backlog: mention it as parent seed only, not executable implementation work.

## Acceptance criteria

- No file under `tasks/backlog` starts with ambiguous bare `RORG-031-`.
- RORG-031B can no longer be mistaken for the active rendering implementation plan.
- All doc links resolve.

## Verification

```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes

- No C++ changes.
- No rendering behavior changes.
- Do not rewrite the GRAPHICS task chain except for references/cross-links.

This directly fixes items 1 and 2 of the cleanup series.
