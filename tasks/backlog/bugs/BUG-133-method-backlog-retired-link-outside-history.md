---
id: BUG-133
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: "Reviewed the full catalog; this defect is confined to task-index lifecycle labeling and changes no engine, data-domain, publication, configuration, runtime, or UI contract."
---
# BUG-133 — Method backlog links a retired task outside history

## Goal

- Restore the strict task-state-link gate by placing the retired `METHOD-020`
  entry in `tasks/backlog/methods/README.md` under an explicitly history-marked
  heading without changing its factual retirement narrative.

## Non-goals

- No method, benchmark, runtime, task-lifecycle, or validator-policy change.
- No reopening or rewriting of `METHOD-020` or its evidence.

## Context

- Symptom: `python3 tools/agents/check_task_state_links.py --root . --strict`
  reports `tasks/backlog/methods/README.md:180` because a category index links
  the retired `tasks/done/METHOD-020-lop-family-gpu-vulkan-compute-backend.md`
  outside a history-marked heading.
- Expected behavior: links to retired tasks in category indexes appear under a
  heading recognized as retired/history context, or are non-link prose.
- Impact: the repository-wide strict task-state-link structural gate fails on
  an otherwise unrelated change.

## Required changes

- [ ] Move or relabel the containing method-index section so its retired task
      link is unambiguously historical while preserving the surrounding
      method-family guidance.
- [ ] Keep every linked task lifecycle and path truthful.

## Tests

- [ ] The strict task-state-link checker passes on the complete repository.
- [ ] Existing task-policy and task-format validators remain green.

## Docs

- [ ] Regenerate `tasks/SESSION-BRIEF.md` after opening and retiring this bug.

## Acceptance criteria

- [ ] The exact reported `METHOD-020` finding no longer reproduces.
- [ ] No task is reopened, moved, or assigned a different dependency.
- [ ] No checker rule is weakened or allowlisted.

## Verification

```bash
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/generate_session_brief.py --check
git diff --check
```

## Forbidden changes

- Moving a retired task back into the open task tree.
- Weakening lifecycle-link classification or adding a special-case exemption.
- Combining unrelated method or task-policy changes with the index repair.
