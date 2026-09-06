---
id: BUG-171
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [repo.task-contract-discovery]
contract_review: "Completion-evidence command disposition is part of task authoring and retirement. This task does not change work-graph topology, actor separation, retry budgets, or source binding."
---
# BUG-171 — Required development receipts cannot be superseded by a passing rerun

## Goal
- Permit a documented, append-only reconciliation of a failed required command
  with a later successful execution of that same check, while retaining every
  receipt and preventing unrelated successful commands from hiding failures.

## Non-goals
- No receipt deletion, mutation, relabeling as optional, or blanket acceptance
  of failed commands. No weakening of independent review or source custody.

## Context
- Discovered during the operator-authorized METHOD-040 overnight run on
  2026-09-06. Its first two geometry builds failed during implementation and
  were mistakenly recorded as required final checks. The missing module imports
  were corrected; subsequent builds and focused tests passed.
- `workflow_evidence.py` generates `commands` from every JSON receipt in the
  task command directory and rejects every failed required receipt, regardless
  of later successful execution. There is no explicit supersession/reconciliation
  command. The failed receipts remain intact at
  `tasks/evidence/METHOD-040/commands/build-geometry-01.json` and
  `tasks/evidence/METHOD-040/commands/build-geometry-02.json`.
- This blocks METHOD-040's completion-evidence report, independently of its
  algorithm verdict. Its code/tests and negative experiment records must not
  be removed to satisfy the tool.

## Required changes
- [ ] Define and implement a hash-bound append-only supersession record, or an
      equally explicit reviewed reconciliation mechanism. Require the same
      task, normalized command and working directory, a later passing receipt,
      and a substantive reason; preserve the original failure in the report.
- [ ] Reject missing/tampered receipts, unrelated checks, reversed chronology,
      failed replacements, cycles, and contradictory reconciliation records.
- [ ] Keep unsuperseded required failures blocking and retain visible history.

## Tests
- [ ] Reproduce failure-then-success retirement with both original receipts
      preserved, and test every rejection above.
- [ ] Verify strict report generation/validation includes the reconciliation
      and continues to reject ordinary unsuperseded required failures.

## Docs
- [ ] Update the evidence policy and CLI documentation in the same change;
      synchronize generated skill references.

## Acceptance criteria
- [ ] A legitimate corrected check can unblock completion without altering or
      concealing its failed development attempts.
- [ ] A different successful check cannot bypass a required failure.
- [ ] Existing custody and retirement regression tests remain passing.

## Verification
```bash
python3 tests/regression/tooling/Test.WorkflowEvidence.py
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/workflow_evidence.py validate --root .
python3 tools/agents/sync_skills.py --check
```

## Forbidden changes
- Deleting or editing failed receipts, silently omitting them from generated
  evidence, accepting unrelated successful commands, or weakening custody gates.
