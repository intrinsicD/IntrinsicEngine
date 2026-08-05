---
id: BUG-130
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "Codex-RejectedTaskSeal"
branch: "bug/BUG-130-rejected-run-task-seal"
worktree: "/tmp/intrinsic-bug130.8PDH4l/worktree"
claimed_at: "2026-08-05T04:33:00Z"
contract_schema: 1
contracts: [repo.task-contract-discovery]
---
# BUG-130 — Rejected experiment runs require their historical task seal

## Status
- Implementation complete on 2026-08-05; retirement is pending fixed-surface
  independent review. METHOD-020 run-001 now validates as retained negative
  evidence after the task advanced to its corrected run-002 surface.

## Goal
- Preserve immutable, independently rejected claim-grade runs when their task
  legitimately evolves, while keeping accepted runs bound to the current task.

## Non-goals
- Do not relax current-task binding for accepted evidence.
- Do not accept unaudited, dirty-source, or fabricated rejected runs.
- Do not rewrite or delete METHOD-020 run-001.

## Context
- Symptom: repository-wide experiment custody rejects METHOD-020 run-001 with
  `task changed after official run initialization` even though that run has a
  valid independent rejected audit and the later task edit exists to execute a
  corrected run.
- Expected behavior: a clean-source rejected run may verify its recorded task
  digest against the exact historical task blob at its frozen source revision;
  accepted runs remain current-task-bound.
- Impact: retaining required negative evidence prevents the corrected
  METHOD-020 run-002 from satisfying otherwise valid completion custody.

## Required changes
- [x] Add a bounded historical task-seal fallback for independently rejected,
      clean-source runs only.
- [x] Keep accepted, unaudited, dirty-source, and malformed rejected runs
      fail-closed against current task drift.

## Tests
- [x] Add regressions for a valid historical rejected run, accepted-run drift,
      and forged or incomplete rejection evidence.
- [x] Run the complete experiment-custody regression suite and global custody.

## Docs
- [x] Document the negative-run historical task-seal rule in the canonical
      workflow evidence policy and regenerate its skill mirror.
- [ ] Update the bug index, session brief, and retirement log on closure.

## Acceptance criteria
- [x] METHOD-020 run-001 remains structurally valid rejected evidence after the
      task advances, and a later accepted run may satisfy completion.
- [x] Accepted evidence cannot use a historical task seal.
- [ ] Fixed-surface independent review finds no blocker.

## Verification
```bash
python3 tests/regression/tooling/Test.ExperimentCustody.py
python3 tools/agents/experiment_custody.py validate --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Treating a rejected audit as positive or claim-authorizing evidence.
- Permitting accepted evidence to bind anything except the current task bytes.
- Rewriting historical experiment artifacts to make validation pass.
