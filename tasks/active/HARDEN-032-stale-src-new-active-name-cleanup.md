# HARDEN-032 — Rename stale active task/doc names containing `src-new`

## Goal
- Remove stale `src_new` / `src-new` wording from active canonical docs and task-navigation text so the post-reorganization structure is represented factually.

## Non-goals
- Do not migrate, reduce, or retire `src/legacy/`.
- Do not perform source-code refactors or runtime/graphics behavior changes.
- Do not rewrite historical migration/archive records that intentionally preserve past terminology.

## Context
- `tasks/active/0001-post-reorganization-hardening-tracker.md` marks `HARDEN-032` as not-started and scopes it to docs/tasks cleanup.
- `docs/migration/src-new-reference-audit.md` classifies the active-stale references that must be cleaned up by this task.

## Required changes
- Update active source-layer README files to describe current `src/<layer>` ownership without `src_new` terminology.
- Update canonical documentation navigation (`docs/index.md`, architecture cross-links, roadmap) to avoid stale `src_new` naming and links.
- Update backlog navigation/docs entries that still reference `tasks/backlog/src-new/`.
- Update `tasks/active/0001-post-reorganization-hardening-tracker.md` status/evidence for `HARDEN-032`.

## Tests
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root . --strict`

## Docs
- `docs/index.md`
- `docs/architecture/task-graphs.md`
- `docs/roadmap.md`
- `docs/architecture/patterns.md`
- Layer README docs under `src/*/README.md`
- `tasks/backlog/README.md`
- `tasks/backlog/legacy-todo.md`
- `tasks/backlog/rendering/RORG-031-rendering-pipeline.md`
- `tasks/active/0001-post-reorganization-hardening-tracker.md`

## Acceptance criteria
- Active-stale `src_new`/`src-new` references listed for these files in `docs/migration/src-new-reference-audit.md` are removed or renamed to current terminology.
- Strict task policy validation passes.
- Strict docs-link validation passes.
- `HARDEN-032` status is updated with concrete evidence in the hardening tracker.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
rg -n "src_new|src-new|src new" src/app/README.md src/core/README.md src/ecs/README.md src/graphics/renderer/README.md src/platform/README.md src/runtime/README.md docs/index.md docs/architecture/patterns.md docs/architecture/task-graphs.md docs/roadmap.md tasks/backlog/README.md tasks/backlog/legacy-todo.md tasks/backlog/rendering/RORG-031-rendering-pipeline.md
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
