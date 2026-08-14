---
name: intrinsicengine-audit
description: Run an on-demand IntrinsicEngine audit sweep — output (window of agent-authored commits, nine failure-mode rows), drift (whole-tree state, nine rows), clean-workshop scorecard, or hints-ledger triage. Use when the operator asks for an audit, before release-shaped work, or as an unattended overnight job; audits have no fixed cadence and never run as ambient per-PR duties. Trigger phrases include "audit", "run the output audit", "drift audit", "sweep the hints", "check for drift".
argument-hint: "[output|drift|workshop|hints] [window or scope]"
---

# IntrinsicEngine Audit Sweeps

Deep review is a deliberate act. Run the requested sweep from
`docs/agent/review.md` §"Audit sweeps" (mirrored at
`../intrinsicengine-review/references/review.md`), then land findings — never
fix inside the audit.

## Which sweep

- **output** — window-scoped audit of agent-authored commits: the nine
  failure-mode rows (scope creep, decorative comments, premature abstraction,
  documented-but-not-tested, defensive validation, untracked shims, ceremony
  without shipped value, half-finished seams, aspirational docs). Default
  window: since the last output-audit report in `docs/reports/`, else 7 days.
  Budget ≤ 60 minutes.
- **drift** — whole-tree state audit: the nine drift rows (inventory drift,
  allowlist owners, active-task branches, stale `(planned)`, unmarked
  aspirational claims, dead seams, untracked TODO/shim markers, naming splits,
  anchor rot). Budget ≤ 45 minutes; sampling rows stay sampled.
- **workshop** — the clean-workshop scorecard for a named change or area
  (`tools/ci/run_clean_workshop_review.sh . --strict` plus the manual rows).
- **hints** — triage `tasks/HINTS.md`: delete resolved entries, promote
  >30-day entries to task files or drop them, keep the ledger under ~100
  lines.

With no argument, ask which sweep — or, when running unattended, do `output`
then `hints`.

## Output discipline

- Score rows `pass | findings | not-applicable`; keep stable row numbers.
- Write a dated report to `docs/reports/<YYYY-MM-DD>-<sweep>-audit.md`
  (window, per-row outcomes, follow-ups, elapsed time).
- Every finding lands as a `tasks/HINTS.md` entry or a backlog/`BUG-` task
  with evidence — no bare TODOs, no fixes inside the audit, no new note trees.
- `python3 tools/agents/check_audit_cadence.py` reports last-report dates on
  request; there is no enforced cadence to satisfy.
