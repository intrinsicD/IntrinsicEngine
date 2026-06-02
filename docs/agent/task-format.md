# Task Format

Use this template for all new task files under `tasks/`.

## Required structure

```md
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
