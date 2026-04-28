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
- **Required changes:** concrete file-level work.
- **Tests:** required verification commands and new/updated tests.
- **Docs:** documentation updates required by the task.
- **Acceptance criteria:** objective done-state checklist.
- **Verification:** exact commands to run.
- **Forbidden changes:** things this task must not do.

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
- `git mv docs/old/path.md docs/new/path.md`
- Update links in `docs/index.md`

## Tests
- `python3 tools/docs/check_doc_links.py --root .`

## Docs
- Update migration index with new location.

## Acceptance criteria
- All links resolve.
- No code files changed.

## Verification
- `python3 tools/docs/check_doc_links.py --root .`

## Forbidden changes
- No C++ behavior edits.
```
