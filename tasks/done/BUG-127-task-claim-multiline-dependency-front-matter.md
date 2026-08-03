---
id: BUG-127
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "Codex-GeometryE2E"
branch: "feature/lop-consolidation-e2e"
worktree: "/tmp/intrinsic-geometry-e2e.GJlhXS"
claimed_at: "2026-08-01T20:22:14Z"
---
# BUG-127 — Task claim corrupts multiline dependency front matter

## Status
- Completed on 2026-08-01. Claim metadata insertion now skips the complete
  multiline `depends_on` block before adding workflow fields; the eight-case
  task-claim suite covers multiline preservation alongside all existing
  concurrency, overlap, release, and recovery contracts.
- Commit: pending final evidence binding.

## Goal
- Preserve valid task YAML when `task_claim.py acquire` writes claim custody
  fields into a task whose `depends_on` sequence spans multiple lines.

## Non-goals
- Do not redesign task front matter or the claim ledger.
- Do not change task dependency semantics or claim concurrency policy.

## Context
- Symptom: acquiring `RUNTIME-175` inserted workflow and custody keys between
  `depends_on:` and its indented sequence entries, making the front matter
  invalid YAML until it was repaired manually.
- Expected behavior: claim metadata is inserted only after the complete
  `depends_on` scalar or block sequence, without reformatting unrelated task
  metadata.
- Impact: an otherwise successful claim can break strict task validation and
  generated session-brief tooling before implementation begins.

## Required changes
- [x] Make claim-field insertion aware of the complete multiline
      `depends_on` value.
- [x] Preserve the existing compact `depends_on: []` path and unrelated
      front-matter text.

## Tests
- [x] Add a regression that acquires a task with a multiline dependency list
      and validates the resulting YAML and dependency sequence.
- [x] Run the complete task-claim regression suite.

## Docs
- [x] Record the defect and verified correction in the bug index, session
      brief, and retirement log.

## Acceptance criteria
- [x] Acquiring tasks with empty, inline, and multiline dependency lists
      leaves valid front matter with the original dependencies intact.
- [x] Atomic, one-writer, overlap, release, and stale-recovery contracts remain
      green.

## Verification
```bash
python3 tests/regression/tooling/Test.TaskClaim.py
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
```

## Forbidden changes
- Reformatting or semantically rewriting unrelated task front matter while
  recording a claim.
- Weakening claim exclusivity or task validation to avoid the malformed YAML.
