---
id: BUG-171
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "night-bug171"
branch: "claude/bug-171-receipt-supersession"
worktree: "/home/alex/Documents/IntrinsicEngine-night/bug-171"
claimed_at: "2026-10-23T23:47:10Z"
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
- [x] Define and implement a hash-bound append-only supersession record, or an
      equally explicit reviewed reconciliation mechanism. Require the same
      task, normalized command and working directory, a later passing receipt,
      and a substantive reason; preserve the original failure in the report.
- [x] Reject missing/tampered receipts, unrelated checks, reversed chronology,
      failed replacements, cycles, and contradictory reconciliation records.
- [x] Keep unsuperseded required failures blocking and retain visible history.

## Tests
- [x] Reproduce failure-then-success retirement with both original receipts
      preserved, and test every rejection above.
- [x] Verify strict report generation/validation includes the reconciliation
      and continues to reject ordinary unsuperseded required failures.

## Docs
- [x] Update the evidence policy and CLI documentation in the same change;
      synchronize generated skill references.

Ticked from the final independent Claude review (`reviews.jsonl` seq 1,
verdict `accepted`, revision `bc0cab45854934362c1c1c77d8d53b9b941168c2`,
content digest `87e9ab98…`): "Required changes 1-3, Tests 1-2 and Docs 1 are
implemented as written (identical argv/cwd stands in for 'normalized
command', operator default 1). They may be ticked in the retirement commit."

## Acceptance criteria
- [x] A legitimate corrected check can unblock completion without altering or
      concealing its failed development attempts.
- [x] A different successful check cannot bypass a required failure.
- [x] Existing custody and retirement regression tests remain passing.

Ticked from the round-1 reviews of `3e7c98e27793dd51fcb44a1460b37e99dd82aee4`
(report digest `9d859cd7…`). An independent Claude review and an independent
Codex review (thread `01a16b39-2409-7a92-90e9-9728d7b5cbcb`) both returned
`approve` and decided all three criteria `met`. They are recorded in the night
coordinator's review ledger, lane BUG-171, round 1. The ticks change the
reviewed surface, so the final independent review must bind the new digest.

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

## Resolution — 2026-10-24

Root cause (reproduced at `599f02ff1`): `validate_report` made every required
receipt with a nonzero exit an error, nothing linked a failure to a later
passing run, and `generate_report` lists every `commands/*.json`. Second gap:
validation never checked that a report lists every receipt on disk, so
dropping the failed entry from `report.yaml` gave `validate --require-complete`
exit 0. Logs: `tasks/evidence/BUG-171/repro-defect-599f02ff1.log`,
`repro-report-omission-599f02ff1.log`.

Change, all in `tools/agents/workflow_evidence.py`:
- `supersede-command --task-id --failed LABEL --replacement LABEL --reason`
  appends to the hash-chained `tasks/evidence/<ID>/supersessions.jsonl`. It
  reuses `_append_jsonl`, whose new optional `guard` re-checks the whole ledger
  inside the existing lock; the other callers are unchanged.
- The CLI, the generator and the validator share one checker. A record binds
  the SHA-256 of both receipt files. The checker rejects:
  - missing or tampered receipts, and receipts that fail receipt validation
    (task ID, log hashes);
  - a non-label path;
  - a failed receipt that is not a required failure;
  - a replacement that is not a required pass;
  - a different argv or cwd;
  - a replacement that did not start after the failure finished;
  - a reason under 20 non-blank characters;
  - a second record for the same failed receipt.
  Self-supersession and cycles fall to the status and time rules.
- The generator adds `superseded_by: {receipt, reason}` to the failed entry. The
  validator:
  - turns a validly superseded failure into a visible warning;
  - reports ledger defects and a report projection that differs from the
    ledger as errors;
  - requires a completion report to list every receipt on disk.
- Docs: `docs/agent/workflow-evidence.md` §"Superseding a failed required
  receipt", a line telling agents to record development runs with
  `--optional`, and the regenerated skill mirror.

Chosen defaults (the operator may veto them at review):
1. The same check means an identical recorded argv list and cwd. Flags are not
   normalized, so `-j 4` and `-j4` are different checks.
2. Later means the replacement's `started_at` is strictly after the failed
   receipt's `finished_at`. Receipts have one-second resolution, and an order
   within the same second fails closed.
3. A substantive reason has at least 20 non-blank characters.
4. Each failed receipt can have one record. One passing replacement may cover
   several failures.
5. A record has no actor field and no `task_id`. Its location and the receipts'
   own `task_id` checks bind the task.
6. The generator never refuses an invalid ledger. The validator is the single
   gate.
7. New completion-only gate: "report omits recorded receipt(s)". Draft reports
   are unaffected. It follows from the Forbidden changes ("silently omitting")
   and acceptance criterion 2.
8. Because the ledger is append-only, an invalid record cannot be removed. It
   keeps the task blocked until a human decides.

Verification, run 2026-10-24 (logs under `tasks/evidence/BUG-171/`):
- New tests run against the unchanged tool: 6 tests, 38 subtests, all
  `FAILED (failures=38)` (`new-tests-before-fix.log`).
- After the fix, `Test.WorkflowEvidence.py` passes 44 tests in 23.2 s
  (`tests-after-fix.log`). Before, it ran 38 tests in 12.4 s.
- `mutate.sh` disables one check at a time in a temporary mirror. All 17
  mutations are killed: argv, cwd, chronology, replacement pass,
  failed-is-failure, receipt hash, receipt existence, receipt validation,
  label, duplicate, reason length, CLI guard, ledger findings, completeness,
  projection match, warning downgrade and generator projection.
- `Test.TaskClaim.py` (3 s), `Test.ExperimentCustody.py` (15 s) and
  `Test.AgentWorkGraph.py` (35 s) pass.
- METHOD-040 dry run on a temporary copy (`method040-dry-run.sh`): 16 of 16
  asserted predictions pass.
  - Accepted: asan-ctest-final and asan-ctest-serial → asan-ctest-local-symbols,
    and cpu-ctest-final → cpu-ctest-reviewed.
  - Rejected: build-geometry-01 → cpu-build-final (different command) and
    build-geometry-01 → build-geometry-02 (failed replacement).
  - The draft report lists all 21 receipts. Command findings are exactly 2
    errors (build-geometry-01/02) and 3 warnings.
  - The original receipts match the hashes recorded beforehand.

Remaining:
- This change alone does not unblock METHOD-040. No receipt with argv
  `cmake --build --preset ci --target IntrinsicGeometryTests -j 4` and cwd `.`
  ever passed. METHOD-040 needs a fresh required receipt of exactly that argv
  and cwd on its final source, then one record per failure.
  - Which failures to supersede, and with what reasons, is for METHOD-040's
    owner and the operator to decide. This matters most for cpu-ctest-final
    (exit 8), which is tied to the open BUG-172 race. The dry-run reasons are
    illustrative only.
- Residual risk: receipts bind neither the source revision nor the
  environment or build-tree state. The lane that owns that is
  PROC-031/CI-012/CI-013.
- High-risk completion: the acceptance checkboxes are ticked from the
  round-1 reviews (see Acceptance criteria).
  - Final independent Claude review: verdict `accepted`, revision
    `bc0cab45854934362c1c1c77d8d53b9b941168c2`, content digest `87e9ab98…`
    (`reviews.jsonl` seq 1, sha256 `f44705ec…`; report.yaml `757bc73c…`,
    handoff.jsonl `f2bbe41e…`). The reviewer decided that Required changes
    1-3, Tests 1-2 and Docs 1 are implemented as written, and left the ticking
    to the retirement commit.
  - `validate --root . --require-complete BUG-171` exits 0: 0 errors, and 1
    warning for the justified skipped check `evidence-validate-bc0cab458`.
    The work graph `finalize` node succeeded, and the claim is released.
  - Retirement moves, ticks and index edits are left to the integrator.
    Operator defaults 1-8 above remain open for the operator's decision.

## Completion — 2026-10-24
Completed 2026-10-24. Commit: `3e7c98e27` (implementation, tests, docs), with
`bc0cab458` (acceptance ticks) and `a56818089` (final-review record), merged into
the night integration branch `claude/night-2026-10-24-bug-sweep`; not yet on
`main`, and hosted CI has not run on them.
Reviewer decisions:
- Round 1 on `3e7c98e27` (report digest `9d859cd7…`): independent Claude
  review `approve`, all three acceptance criteria `met`; independent Codex
  review (thread `01a16b39-2409-7a92-90e9-9728d7b5cbcb`) `approve`, all three
  `Met`.
- Final on `bc0cab458`: independent Claude review `accepted` (`reviews.jsonl`
  seq 1, content digest `87e9ab98…`); Required changes, Tests and Docs ticked
  above as it decided. `validate --root . --require-complete BUG-171` was
  rerun in the lane worktree before this move: 0 errors, 1 warning (the
  justified skipped check `evidence-validate-bc0cab458`).
Limitations the reviewers named, not changed here:
- Not exercised: the legacy `seal-report` path with a superseded receipt and
  concurrent `supersede-command` writers.
- Optional hardening, no owner yet: `recorded_at` and unknown record fields
  are unchecked; `schema_version` also accepts `True`; a receipt label is not
  bound to its file stem or log paths.
- Supersessions and `report.yaml` are not bound by the handoff/review digest.
  A record appended after acceptance plus a regenerated report would pass
  `--require-complete` unseen by a reviewer. Whether they must be
  review-bound is an operator decision, as are vetoes of defaults 1-8; a
  veto becomes a follow-up task.
- METHOD-040 stays blocked until it records a fresh passing receipt with the
  identical argv and cwd (see Remaining).
The high-risk report, handoff, review and raw logs are git-ignored and stay
in the `bug-171` lane worktree under `tasks/evidence/BUG-171/`; this
retirement deletes nothing.
