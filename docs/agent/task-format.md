# Task Format

Use this template for all new task files under `tasks/`.

## Required structure

```md
---
id: <TASK-ID>
theme: <theme letter, or `none`>
depends_on: []
---
# <TASK-ID> <Title>

## Goal

## Non-goals

## Context

## Required changes

## Tests

## Docs

## Acceptance criteria

## Verification

## Forbidden changes
```

## Field guidance

- **Goal:** one specific outcome.
- **Non-goals:** explicit exclusions to prevent scope creep.
- **Context:** architectural owner/layer and relevant constraints.
- **Required changes:** concrete file-level work as markable checkbox todos.
- **Tests:** required verification commands and new/updated tests as markable checkbox todos.
- **Docs:** documentation updates required by the task as markable checkbox todos.
- **Acceptance criteria:** objective done-state checklist as markable checkbox todos.
- **Verification:** exact commands to run.
- **Forbidden changes:** things this task must not do.

Use plain bullets for context, non-goals, and forbidden changes. Use markdown
checkboxes (`- [ ]` for open work, `- [x]` for completed work) in actionable
sections so task status is visible at a glance. Completed task files under
`tasks/done/` should not contain unchecked actionable todos; unresolved work
belongs in a follow-up task.

## Front-matter (open tasks)

Open tasks under `tasks/active/` and `tasks/backlog/` must start with a YAML
front-matter block; it is the machine-readable home of dependency edges and
feeds the generated `tasks/SESSION-BRIEF.md`:

- `id` (required) — must equal the title-line task ID.
- `theme` (required) — convergence-theme letter from
  `tasks/backlog/README.md`, or `none` for unthemed work.
- `depends_on` (required, may be `[]`) — task IDs this task is gated by;
  every entry must resolve to a task file under `tasks/active|backlog|done`.
  A dependency is satisfied when the referenced task is in `tasks/done/`.
- `maturity_target` (optional) — intended stop-state per
  [`task-maturity.md`](task-maturity.md).

`tools/agents/validate_tasks.py --strict` enforces the schema for open tasks;
retired tasks under `tasks/done/` are exempt. After opening, retiring, or
re-gating a task, regenerate the session brief with
`python3 tools/agents/generate_session_brief.py`.

## Optional `## Control surfaces` and `## Backends` fields

For feature work that introduces or changes user/agent control, add a
`## Control surfaces` section before `## Required changes` and name which
surfaces can drive the behavior:

```md
## Control surfaces
- Config: `EngineConfig.render.default_recipe_config_path`
- UI: Sandbox editor recipe panel
- Agent/CLI: `Engine::LoadAndApplyRenderRecipeConfigFile(...)`
```

Use `N/A` only when the task cannot be externally controlled by design, for
example a purely internal mechanical migration.

For parallelizable engine algorithms or backend-facing features, add a short
`## Backends` section:

```md
## Backends
- Backend axis: CPU reference now; GPU deferred to `GRAPHICS-108`.
```

This field is non-enforcing, like `## Maturity`; it is an authoring prompt so
reviewers can see whether a CPU/GPU backend hook is present or intentionally
deferred to a named task.

## Retiring a task

When a task completes:

1. Mark all checkbox todos `- [x]` (unresolved work moves to a follow-up
   task) and add a completion note with the date (`YYYY-MM-DD`) and a
   commit/PR reference.
2. `git mv` the file to `tasks/done/`.
3. Append a short retirement narrative (what landed, maturity, what remains
   owned elsewhere) to the top of
   [`tasks/done/RETIREMENT-LOG.md`](../../tasks/done/RETIREMENT-LOG.md).
   Do **not** add it to `tasks/active/README.md` or the backlog README —
   those indexes describe current state only, and
   `tools/agents/check_task_state_links.py` rejects links into `tasks/done/`
   from them.
4. Remove the task's entry from the open-member lists in
   `tasks/backlog/README.md` and update its category README. Category
   READMEs may keep retired entries, but only under a history-marked
   heading (one whose text contains retired/history/closed/completed/
   resolved/verified/done — e.g. `## Retired`); open lists cite retired
   tasks as plain code spans, not links. `check_task_state_links.py`
   enforces this for `tasks/backlog/*/README.md` and
   `tasks/backlog/bugs/index.md`; sections that interleave done
   prerequisites with open work by design (the rendering dependency DAG)
   opt out with a `<!-- state-link-guard: allow-done-links -->` comment
   directly below their heading.

## ID allocation

Task IDs must be unique across `tasks/active/`, `tasks/backlog/`, and
`tasks/done/`; `tools/agents/validate_tasks.py` enforces this in strict mode
(a small set of pre-2026-06-09 collisions is grandfathered in place). Before
opening `<PREFIX>-<N>`, take the highest existing number for that prefix
across **all three** directories and add one:

```bash
grep -rhoE '^# <PREFIX>-[0-9]+' tasks/active tasks/backlog tasks/done | sort -V | tail -1
```

Letter-suffixed child slices (e.g. `GRAPHICS-033A`) extend their parent's
number and do not claim a new one.

Prefixes come from the canonical list in `tasks/README.md` §"Task ID
prefixes"; do not invent a new prefix without adding it there in the same
change (historical `tasks/done/` entries contain retired prefix variants —
they are not precedent).

When **batch-seeding** several tasks (e.g. converting review findings into a
task series), allocate the whole contiguous range up front and run
`python3 tools/agents/validate_tasks.py --root .` locally before committing —
CI enforces uniqueness, but a concurrent session may claim the same numbers
in flight (`GEOM-027` collided this way and had to be renumbered by
`PROC-012`). If two branches race, the first one merged keeps the numbers and
the later branch renumbers.

## Optional `## Maturity` field

For tasks where the stop-state is ambiguous — typically rendering, Vulkan,
asset ingest, hot reload, pass command bodies, runtime composition, and
legacy retirement — an optional `## Maturity` section makes the intended
endpoint explicit. The section is not required and the validator does not
enforce it, but reviewers should ask for it when a task could plausibly stop
at multiple levels.

Suggested shape:

```md
## Maturity

- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` everywhere
  else.
- Slice 1 closes `Scaffolded → CPUContracted`; `Operational` owned by `<TASK-ID>`.
- For CPU/null-only endpoints: no `Operational` follow-up is owed.
```

See [`task-maturity.md`](task-maturity.md) for the taxonomy and the
`Scaffolded` closure rule that applies even when the field is absent. Open
backend-facing task files with `CPUContracted` maturity must use one of the
accepted `Operational` follow-up statements above.

## Example

```md
# RORG-999 Example mechanical move

## Goal
Move subsystem docs into canonical location.

## Non-goals
- No semantic code changes.

## Context
Owned by docs/migration layer.

## Required changes
- [ ] `git mv docs/old/path.md docs/new/path.md`
- [ ] Update links in `docs/index.md`

## Tests
- [ ] `python3 tools/docs/check_doc_links.py --root .`

## Docs
- [ ] Update migration index with new location.

## Acceptance criteria
- [ ] All links resolve.
- [ ] No code files changed.

## Verification
- `python3 tools/docs/check_doc_links.py --root .`

## Forbidden changes
- No C++ behavior edits.
```
