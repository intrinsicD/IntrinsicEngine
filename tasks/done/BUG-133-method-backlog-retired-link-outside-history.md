---
id: BUG-133
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
owner: "codex-root"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-06T06:33:05Z"
evidence_skip_reason: "Single-line task-index link correction with no engine, workflow-policy, or behavior change; strict task-state and task validators are the complete proof."
template: micro
contract_schema: 1
contracts: []
contract_review: "Reviewed the full catalog; this defect is confined to task-index lifecycle labeling and changes no engine, data-domain, publication, configuration, runtime, or UI contract."
---
# BUG-133 — Method backlog links a retired task outside history

## Status

- Completed on 2026-08-06 as a micro documentation repair.
- Implementation commit: `312f32fe`.

## Goal

- Restore the strict task-state-link gate by citing retired task `METHOD-020`
  as non-link prose in the live method-family guidance without changing its
  factual retirement narrative.

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

- [x] Replace the retired task link with a lifecycle-safe task-ID citation
      while preserving the surrounding method-family guidance.
- [x] Keep every remaining linked task lifecycle and path truthful.

## Tests

- [x] The strict task-state-link checker passes on the complete repository.
- [x] Existing task-policy and task-format validators remain green.

## Docs

- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening and retiring this bug.

## Acceptance criteria

- [x] The exact reported `METHOD-020` finding no longer reproduces.
- [x] No task is reopened, moved, or assigned a different dependency.
- [x] No checker rule is weakened or allowlisted.

## Verification

```bash
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/generate_session_brief.py --check
git diff --check
```

Executed on 2026-08-06: all five commands passed. The task-state checker
reported no findings across the repository, and no validator policy or task
lifecycle was changed.

## Forbidden changes

- Moving a retired task back into the open task tree.
- Weakening lifecycle-link classification or adding a special-case exemption.
- Combining unrelated method or task-policy changes with the index repair.
