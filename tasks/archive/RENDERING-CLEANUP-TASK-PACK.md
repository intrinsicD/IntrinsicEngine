# Rendering Backlog Cleanup — Codex Task Pack

This index lists the queued task pack for Codex. Hand the tasks to Codex one at a time, in the order below. The first nine entries are cleanup/governance work and must complete before any rendering implementation work.

## Status

- Status: completed / archived
- Completion date: 2026-05-02
- Commit / PR: local split branch `split/current-working-tree-2026-05-02`; remote PR reference TBD.
- Follow-ups: GRAPHICS-016 runtime-owned extraction follow-up is retired at `tasks/archive/GRAPHICS-016-runtime-extraction-handoff.md`; continue downstream rendering pass implementation through GRAPHICS-002+.
- Tasks 1–13 are completed as cleanup/governance work. Task 8 was reviewed on 2026-05-02 and corrected to keep the C++ source changes out of the docs-only naming reconciliation scope.
- Task 14 completed the Stage A promoted-graphics API boundary gate; runtime-owned extraction/wiring remains tracked by GRAPHICS-016.
- This pack is archived under `tasks/done/`; `tasks/active/` should contain only currently in-progress or blocked task files.
- Authoritative process docs: `AGENTS.md`, `docs/agent/contract.md`, `docs/agent/task-format.md`, `docs/agent/review-checklist.md`.

## Recommended execution order

- [x] [Task 1 — Create rendering backlog cleanup tracker](task-01-graphics-021-rendering-backlog-cleanup-tracker.md)
- [x] [Task 2 — Resolve RORG-031 collision and stale RORG-031B seed](task-02-resolve-rorg-031-collision.md)
- [x] [Task 3 — Fix task validator to match documented task format](task-03-fix-task-validator.md)
- [x] [Task 4 — Add configure step and label expectations to rendering tasks](task-04-add-configure-step-and-test-labels.md)
- [x] [Task 5 — Add rendering backlog README with dependency DAG](task-05-rendering-backlog-readme-with-dag.md)
- [x] [Task 6 — Cross-link GRAPHICS-016 from runtime backlog](task-06-cross-link-graphics-016-from-runtime.md)
- [x] [Task 7 — Update stale docs and renderer README ownership contract](task-07-update-stale-docs-and-renderer-readme.md)
- [x] [Task 8 — Reconcile CullingPass and Picking/Selection pass naming](task-08-reconcile-culling-picking-naming.md)
- [x] [Task 9 — Split GRAPHICS-013 into smaller tasks](task-09-split-graphics-013.md)
- [x] [Task 10 — Add missing task: rendergraph diagnostics and validation](task-10-graphics-022-rendergraph-diagnostics.md)
- [x] [Task 11 — Add missing task: shader/material/texture hot reload](task-11-graphics-023-shader-material-texture-hot-reload.md)
- [x] [Task 12 — Add missing task: overlays, presentation adjacency, editor handoff](task-12-graphics-024-overlays-presentation-editor-handoff.md)
- [x] [Task 13 — Add missing task: hybrid/transparent/special-material forward path](task-13-graphics-025-hybrid-transparent-special-material-path.md)
- [x] [Task 14 — Hard implementation gate: remove live ECS ownership from promoted graphics](task-14-graphics-016-remove-live-ecs-ownership.md)

After Task 14 lands cleanly, Codex can proceed into GRAPHICS-002, GRAPHICS-003, GRAPHICS-004, etc., per the dependency DAG defined by Task 5.

## Operating rules for these tasks

- Hand Codex one task at a time, in order.
- Do not start pass implementation work before Tasks 1–8 complete and Task 14 has landed the GRAPHICS-016 extraction gate.
- Do not mix docs/task cleanup tasks with C++ behavior changes in a single PR.
- Do not copy legacy code into promoted graphics layers.
