# LEGACY-002 — Seed retirement tasks for remaining `src/legacy/` subtrees

## Goal
- Open a structured retirement task in `tasks/backlog/architecture/` for each `src/legacy/<Subsystem>/` subtree that does not yet have one, so the layering allowlist (re-bound by [`HARDEN-069`](../../done/HARDEN-069-rebind-legacy-layering-allowlist-to-active-retirement-tasks.md)) can name a concrete owning task per subtree.

## Non-goals
- Do not delete any code under `src/legacy/` in this task. This is a backlog-seeding task; deletions land under each per-subtree LEGACY-* successor.
- Do not change `tools/repo/layering_allowlist.yaml` here; rebinding is owned by `HARDEN-069`.
- Do not author exhaustive deletion plans for every subtree. Each seed task should follow the [`LEGACY-001`](LEGACY-001-delete-src-legacy-interface.md) shape: scope, prerequisites (consumer-grep gate), and verification template.
- Do not retire `src/legacy/Interface/` here — that subtree is already owned by `LEGACY-001`.

## Context
- Status (2026-06-06): deliverables landed — `LEGACY-003`..`LEGACY-010` were
  seeded, `tasks/backlog/architecture/README.md` gained a "Legacy retirement"
  section with dependency-ordered hints, and the
  `docs/migration/legacy-retirement.md` sequencing section now links every
  seeded task. This task intentionally remains in `tasks/backlog/` (not retired)
  because ~70 `tools/repo/layering_allowlist.yaml` rows still name `LEGACY-002`
  as their open umbrella owner and rebinding the allowlist is an explicit
  non-goal here. A metadata-only rebinding follow-up (mirroring `HARDEN-069`)
  must move each subtree's allowlist rows to its specific `LEGACY-00N` task
  before `LEGACY-002` can retire to `tasks/done/`. Ordering rationale note: the
  README hint was authored as consumer-leaves-first / foundation-last; that
  refines this file's parenthetical "Apps likely retires last" example, which is
  inverted relative to the actual dependency DAG (`Apps` is a pure leaf binary,
  `Core` is the foundation that retires last).
- Owning subsystem/layer: legacy retirement program; new tasks land in `tasks/backlog/architecture/`.
- `AGENTS.md` §3 mandates that `src/legacy/` "must shrink over time"; today it still holds ~330 files across 9 subsystem subtrees.
- Subtrees needing seeds (Interface already has `LEGACY-001`):
  - `src/legacy/Apps/`
  - `src/legacy/Asset/`
  - `src/legacy/Core/`
  - `src/legacy/ECS/`
  - `src/legacy/EditorUI/`
  - `src/legacy/Graphics/`
  - `src/legacy/RHI/`
  - `src/legacy/Runtime/`
- Each per-subtree task must inventory promoted equivalents (e.g. `Apps/` -> `src/app/` + `src/platform/`; `RHI/` -> `src/graphics/rhi/`; `ECS/` -> `src/ecs/`) and include a consumer-grep prerequisite gate before promotion to `tasks/active/`.
- Use [`LEGACY-001`](LEGACY-001-delete-src-legacy-interface.md) as the structural template.

## Required changes
- [x] Create `tasks/backlog/architecture/LEGACY-003-delete-src-legacy-apps.md` modeled on `LEGACY-001`.
- [x] Create `tasks/backlog/architecture/LEGACY-004-delete-src-legacy-asset.md`.
- [x] Create `tasks/backlog/architecture/LEGACY-005-delete-src-legacy-core.md`.
- [x] Create `tasks/backlog/architecture/LEGACY-006-delete-src-legacy-ecs.md`.
- [x] Create `tasks/backlog/architecture/LEGACY-007-delete-src-legacy-editorui.md`.
- [x] Create `tasks/backlog/architecture/LEGACY-008-delete-src-legacy-graphics.md`.
- [x] Create `tasks/backlog/architecture/LEGACY-009-delete-src-legacy-rhi.md`.
- [x] Create `tasks/backlog/architecture/LEGACY-010-delete-src-legacy-runtime.md`.
- [x] Update [`tasks/backlog/architecture/README.md`](README.md) to list the new tasks under a "Legacy retirement" section, with explicit ordering hints (e.g. `Apps` likely retires last because it is the top-level binary).
- [x] Cross-link the new tasks from [`docs/migration/legacy-retirement.md`](../../../docs/migration/legacy-retirement.md) (or whichever migration index currently tracks legacy retirement).

## Tests
- [x] `python3 tools/agents/validate_tasks.py --root tasks --strict` passes (all new files conform to `docs/agent/task-format.md`).
- [x] `python3 tools/agents/check_task_policy.py --root . --strict` passes.
- [x] `python3 tools/docs/check_doc_links.py --root .` passes (all cross-links resolve).

## Docs
- [x] [`tasks/backlog/architecture/README.md`](README.md) updated with the new LEGACY-* index.
- [x] [`docs/migration/legacy-retirement.md`](../../../docs/migration/legacy-retirement.md) sequencing section lists the seeded tasks in dependency order.

## Acceptance criteria
- [x] Eight new backlog task files exist with IDs `LEGACY-003` through `LEGACY-010`, one per remaining `src/legacy/<Subsystem>/` subtree.
- [x] Each new task has a consumer-grep prerequisite block matching the `LEGACY-001` pattern (gate that exits non-zero while external consumers remain).
- [x] Each new task names the promoted destination layer(s) that must already exist before the retirement can proceed.
- [x] No code under `src/legacy/` is modified by this commit.

## Verification
```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .

# Sanity: confirm each subtree has exactly one owning LEGACY-* task in backlog.
for sub in Apps Asset Core ECS EditorUI Graphics RHI Runtime; do
    if ! ls tasks/backlog/architecture/LEGACY-*-delete-src-legacy-$(echo "$sub" | tr 'A-Z' 'a-z').md >/dev/null 2>&1; then
        echo "MISSING retirement task for src/legacy/$sub/" >&2
        exit 1
    fi
done
echo "OK: every remaining src/legacy/ subtree has a retirement task."
```

## Forbidden changes
- Touching files under `src/legacy/`.
- Editing `tools/repo/layering_allowlist.yaml` (owned by `HARDEN-069`).
- Mixing this seeding with semantic refactors elsewhere in the repo.
- Marking any new task as `active` in this commit; they enter `tasks/active/` only when their per-task prerequisites are met.
