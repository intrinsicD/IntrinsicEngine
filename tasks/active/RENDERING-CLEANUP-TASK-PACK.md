# Rendering Backlog Cleanup — Codex Task Pack

This index lists the queued task pack for Codex. Hand the tasks to Codex one at a time, in the order below. The first nine entries are cleanup/governance work and must complete before any rendering implementation work.

## Status

- All entries are `planned` (queued). No task is yet `in-progress` or assigned to an owner.
- This pack lives under `tasks/active/` so reviewers and agents can see the queue. Tasks describe work to perform on `tasks/backlog/` and source files; they do not themselves modify renderer behavior.
- Authoritative process docs: `AGENTS.md`, `docs/agent/contract.md`, `docs/agent/task-format.md`, `docs/agent/review-checklist.md`.

## Recommended execution order

1. [Task 1 — Create rendering backlog cleanup tracker](task-01-graphics-021-rendering-backlog-cleanup-tracker.md)
2. [Task 2 — Resolve RORG-031 collision and stale RORG-031B seed](task-02-resolve-rorg-031-collision.md)
3. [Task 3 — Fix task validator to match documented task format](task-03-fix-task-validator.md)
4. [Task 4 — Add configure step and label expectations to rendering tasks](task-04-add-configure-step-and-test-labels.md)
5. [Task 5 — Add rendering backlog README with dependency DAG](task-05-rendering-backlog-readme-with-dag.md)
6. [Task 6 — Cross-link GRAPHICS-016 from runtime backlog](task-06-cross-link-graphics-016-from-runtime.md)
7. [Task 7 — Update stale docs and renderer README ownership contract](task-07-update-stale-docs-and-renderer-readme.md)
8. [Task 8 — Reconcile CullingPass and Picking/Selection pass naming](task-08-reconcile-culling-picking-naming.md)
9. [Task 9 — Split GRAPHICS-013 into smaller tasks](task-09-split-graphics-013.md)
10. [Task 10 — Add missing task: rendergraph diagnostics and validation](task-10-graphics-022-rendergraph-diagnostics.md)
11. [Task 11 — Add missing task: shader/material/texture hot reload](task-11-graphics-023-shader-material-texture-hot-reload.md)
12. [Task 12 — Add missing task: overlays, presentation adjacency, editor handoff](task-12-graphics-024-overlays-presentation-editor-handoff.md)
13. [Task 13 — Add missing task: hybrid/transparent/special-material forward path](task-13-graphics-025-hybrid-transparent-special-material-path.md)
14. [Task 14 — Hard implementation gate: remove live ECS ownership from promoted graphics](task-14-graphics-016-remove-live-ecs-ownership.md)

After Task 14 lands cleanly, Codex can proceed into GRAPHICS-002, GRAPHICS-003, GRAPHICS-004, etc., per the dependency DAG defined by Task 5.

## Operating rules for these tasks

- Hand Codex one task at a time, in order.
- Do not start pass implementation work before Tasks 1–8 complete and Task 14 has landed the GRAPHICS-016 extraction gate.
- Do not mix docs/task cleanup tasks with C++ behavior changes in a single PR.
- Do not copy legacy code into promoted graphics layers.
