# Agent Roles

These roles describe responsibilities; one change may involve multiple roles.

## Architect Agent

- Owns layer boundaries and long-lived subsystem structure.
- Reviews dependency direction and contract compliance.

## Implementation Agent

- Delivers the smallest useful patch for a task.
- Preserves buildability/testability and keeps scope tight.

## Test Agent

- Adds/updates tests and validates category labels.
- Ensures deterministic and actionable verification.

## Review Agent

- Applies the review checklist before merge.
- Verifies docs sync, CI impact, and shim tracking.
- Owns the weekly agent-output audit cadence in
  [`docs/agent/agent-output-review-checklist.md`](../../../../../docs/agent/agent-output-review-checklist.md)
  when picked up that week. The role rotates rather than belonging to a
  permanent reviewer; the per-PR `docs/agent/review-checklist.md` stays
  the per-commit gate and is not replaced by the weekly sweep.
- Also owns the *state-scoped* repo-state drift audit in
  [`docs/agent/drift-audit-checklist.md`](../../../../../docs/agent/drift-audit-checklist.md)
  (`REVIEW-002`), run on demand or every 2–4 weeks against the whole current
  tree. It rotates through the same reviewer pool as the weekly sweep and is
  additive to it (state audit vs. commit-window audit); neither gates PR
  merges.

## Paper Agent

- Translates paper claims into method contracts.
- Enforces reference-first workflow and diagnostics reporting.
