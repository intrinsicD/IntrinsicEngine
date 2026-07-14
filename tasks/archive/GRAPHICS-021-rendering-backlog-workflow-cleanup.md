# GRAPHICS-021 — Rendering backlog workflow cleanup

- Status: completed
- Completion date: 2026-05-03
- Commit / PR: local split commit; remote PR reference TBD.
- Current note: `GRAPHICS-001` was retired on 2026-06-06; current rendering
  task selection lives in `tasks/backlog/rendering/README.md`.

The cleanup follow-ups are tracked in structured task records, the rendering
backlog README/DAG exists, and the remaining implementation gate is
`GRAPHICS-016`.

## Goal
- Track the workflow, task-format, and doc-cleanup work that must complete on the rendering backlog before any further `GRAPHICS-002`+ implementation work proceeds. This is the canonical anchor for the rendering-backlog cleanup series.

## Non-goals
- No C++ behavior changes.
- No shader changes.
- No renderer pass implementation.
- No GPU-driven scene/world implementation work.
- No file moves of source code.
- No copying from `src/legacy` into promoted graphics layers.

## Context
- Owner: rendering backlog governance across `tasks/backlog/rendering/` and the cross-linked rendering docs.
- `GRAPHICS-001` is the canonical rendering task index and must remain the entry point for rendering implementation work; this task is a meta/cleanup task and does not replace it.
- Authoritative process docs: `AGENTS.md`, `docs/agent/contract.md`, `docs/agent/task-format.md`, and `docs/agent/review-checklist.md`.
- The rendering backlog now spans `GRAPHICS-001` through `GRAPHICS-020`, but several docs/tasks still contain stale references (`legacy-todo.md`, RORG-031B), ambiguous task IDs (`RORG-031A`/`RORG-031B` filename collision), missing dependency metadata (no rendering backlog README or DAG), naming drift (`CullingPass` vs picking/selection passes), an over-scoped `GRAPHICS-013`, and incomplete verification commands (no configure step before build/ctest).
- Several rendering work areas surfaced in legacy parity reviews still have no task home: rendergraph diagnostics/validation, shader/material/texture hot reload, overlays/presentation/editor handoff, and the hybrid/transparent/special-material forward path.
- The `GRAPHICS-016` runtime extraction handoff must land before further pass implementation so promoted graphics never sees live ECS ownership; that gating must be made explicit in the cleanup pass.

## Required changes
- [x] Keep this task as a single anchor that lists, in execution order, the cleanup follow-up work below. Each follow-up must be tracked by its own structured task file under `tasks/backlog/rendering/` (or relocated to `tasks/done/` once landed) and must conform to `docs/agent/task-format.md`.
- [x] Cleanup follow-up areas to track (do not implement renderer features in any of them):
  - [x] Resolve the `RORG-031A`/`RORG-031B` filename collision in `tasks/backlog/architecture/` and `tasks/backlog/rendering/`.
  - [x] Retire or cross-link the stale `RORG-031B` rendering seed so it cannot be mistaken for the canonical rendering plan; `GRAPHICS-001` remains the rendering backlog index.
  - [x] Fix the task validator (`tools/agents/validate_tasks.py`) to match the documented task format in `docs/agent/task-format.md`, including the `# <ID> — <title>` header rule used by existing GRAPHICS tasks.
  - [x] Add a configure step (e.g. `cmake --preset ci`) and explicit CTest label expectations (`-LE 'gpu|vulkan|slow|flaky-quarantine'`) to the verification blocks of GRAPHICS rendering tasks where they are missing.
  - [x] Fix stale `tasks/backlog/legacy-todo.md` references and other stale paths in rendering docs and architecture docs.
  - [x] Add a rendering backlog README under `tasks/backlog/rendering/README.md` that includes the dependency DAG between GRAPHICS tasks and points to `GRAPHICS-001` as the canonical index.
  - [x] Cross-link `GRAPHICS-016` from the runtime backlog so runtime owners see the extraction-handoff dependency.
  - [x] Update stale docs and the renderer README to match the current ownership contract (snapshots/views, no live ECS ownership, runtime-owned extraction sidecars).
  - [x] Reconcile `CullingPass` and Picking/Selection pass naming across `GRAPHICS-007`, `GRAPHICS-012`, and adjacent docs.
  - [x] Split the over-scoped `GRAPHICS-013` (post-process, debug view, ImGui, present) into smaller, separately reviewable tasks.
  - [x] Add missing rendering tasks where work has no task home today:
      - [x] `GRAPHICS-022` rendergraph diagnostics and validation.
      - [x] `GRAPHICS-023` shader/material/texture hot reload.
      - [x] `GRAPHICS-024` overlays, presentation adjacency, and editor handoff.
      - [x] `GRAPHICS-025` hybrid/transparent/special-material forward path.
  - [x] Make `GRAPHICS-016` the hard first implementation gate before any further `GRAPHICS-002`+ implementation, so promoted graphics consumes only snapshots/views and never live ECS ownership.
- [x] Mark this task `done` only when items 1–12 are individually tracked by structured task files (or already completed) and the follow-up tasks are linked from this entry.

## Tests
- [x] Run task policy validation after each cleanup step.
- [x] Run documentation link validation when adding or updating markdown links.
- [x] Do not run renderer or Vulkan tests under this task; this is a docs/task cleanup anchor.

## Docs
- [x] Cross-link this task from `GRAPHICS-001` so future agents see the cleanup gate before picking up implementation work.
- [x] Keep `docs/agent/task-format.md`, `docs/agent/contract.md`, and `docs/agent/review-checklist.md` as the authoritative references for task shape and review expectations.
- [x] Keep this entry consistent with `docs/architecture/graphics.md`, `docs/architecture/rendering-three-pass.md`, and `docs/migration/nonlegacy-parity-matrix.md` if they are touched by follow-up tasks.

## Acceptance criteria
- [x] `GRAPHICS-021` exists in `tasks/backlog/rendering/` and passes task policy validation in strict mode (no new findings introduced by this file).
- [x] The task is clearly scoped as a cleanup/meta-task and does not contain renderer implementation work.
- [x] The task points future agents to `GRAPHICS-001` as the canonical rendering backlog index.
- [x] The cleanup follow-up areas (items 1–12) are listed explicitly so each can be tracked by its own task file.
- [x] It is explicit that no `GRAPHICS-002`+ implementation work proceeds until the cleanup series and the `GRAPHICS-016` extraction gate have landed.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes
- No C++ behavior changes.
- No shader changes.
- No renderer implementation.
- No file moves of source code or `tasks/backlog/` task files except creating this `GRAPHICS-021` entry.
- No mixing of mechanical task moves with semantic task rewrites in the same change.
- No copying from `src/legacy` into promoted graphics layers.
