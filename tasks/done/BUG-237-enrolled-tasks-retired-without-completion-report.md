---
id: BUG-237
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive fix; evidence is the diff and the strict validator run
contract_schema: 1
contracts: [repo.task-contract-discovery]
---
# BUG-237 — Enrolled tasks retired without a completion report fail workflow-evidence validation

## Goal
- `python3 tools/agents/workflow_evidence.py validate --root .` passes on `main` again, and an
  interactive retirement of an enrolled `standard`/`high-risk` task can no longer reach `main`
  with this error unnoticed.

## Context
- Symptom (2026-10-08, `main` at `7f154fe1a`, identical at `e0362e4ff`): the validator exits 1
  with six errors, `tasks/evidence/<ID>/report.yaml: required completion report is missing`, for
  BUG-160, CORE-010, GEOM-024, METHOD-015, RUNTIME-277 and RUNTIME-288. All six are in
  `tasks/done/` with `workflow_schema: 1`, `workflow_profile: standard` (GEOM-024: `high-risk`),
  `evidence: required`, and none has a `tasks/evidence/<ID>/` directory.
- They were retired between 2026-09-26 and 2026-10-02 (`d4d58076e`, `0ad27fec2`, `b6277e373`,
  `f010ebf33`, `349101058`, `9d2a0af78`) without any evidence artifacts. New tasks seeded from
  `tasks/templates/task.md` or `bug-task.md` enroll as `standard` by default; work done in the
  interactive micro lane produces no report.
- Why unnoticed: `docs/agent/task-format.md` retirement step 3 already requires
  `workflow_evidence.py validate --require-complete <ID>` for enrolled non-micro work, but the
  docs/task-only verification route in `docs/agent/prompt/prompt.md` §Verification does not run
  the validator (owed only "when overnight evidence or custody state is touched"). `ci-docs` has
  no push trigger on `main` (`pull_request`, `merge_group`, `workflow_dispatch` only); per
  `gh run list` its last run was 2026-09-23, before the first of these retirements. The next pull
  request will fail its "Validate enrolled workflow evidence and experiment custody" step for
  reasons unrelated to it.
- Ruled out: not caused by the agent-workflow docs change `18e3c5f67`/`7f154fe1a` (same six
  errors on `e0362e4ff`); validator code unchanged since the retirements.

## Decision (operator, 2026-10-08): option 1
Options considered for how to clear the six historical errors; each changes the retired records differently:
1. Re-profile the six done tasks to `template: micro`, `workflow_profile: micro`,
   `evidence: not_applicable` with a concrete reason (edits retired task front-matter after the
   fact; normally this switch happens before retirement).
2. Generate reports after the fact (`workflow_evidence.py generate-report` at each retirement
   commit). This clears the errors only with successful required command receipts, which do
   not exist; GEOM-024 (`high-risk`) also needs the final handoff and an accepted independent
   review bound to the report digest.
3. A validator exception that names exactly these six IDs with this note as its reason (code
   change, keeps the task bytes); not a blanket cut-off date, which could hide later errors.

## Acceptance criteria
- [x] The six errors are cleared by the chosen option; `workflow_evidence.py validate` exits 0.
- [x] Recurrence is prevented: retiring an enrolled task runs `workflow_evidence.py validate`
      (or switches the task to the micro profile with a reason first), stated in
      `docs/agent/prompt/prompt.md` §Verification and the `intrinsicengine-task-workflow` skill.

## Verification
```bash
python3 tools/agents/workflow_evidence.py validate --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
python3 tools/agents/sync_skills.py --check
```

## Completion
Completed 2026-10-08. Commit: the BUG-237 retirement commit. The six done tasks carry `template: micro`,
`workflow_profile: micro`, `evidence: not_applicable` and an `evidence_skip_reason` naming this
task; their bodies are unchanged. `workflow_evidence.py validate` exits 0 (0 errors, 105
pre-existing warnings). `docs/agent/prompt/prompt.md` §Verification runs the validator when a
task retires and states the micro switch; the `intrinsicengine-task-workflow` skill says the same.
The `ci-docs` push trigger on `main` is unchanged (out of scope).
