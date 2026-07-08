---
id: PROC-021
theme: H
depends_on: []
---
# PROC-021 — Wire docs-sync and task-state-link validators into CI (or retire the promise)

## Goal

- Resolve the two validators that are documented as enforcement but run in no
  CI workflow: `tools/docs/check_docs_sync.py` (warning-mode "until enabling
  CI enforcement in later migration phases") and
  `tools/agents/check_task_state_links.py` (described by `PROC-008` as
  enforcing the README state/history split; currently warning-mode,
  local-only).

## Non-goals

- No changes to what the validators check; this task owns only their CI
  wiring and the documentation of their mode.
- No weakening of any check that already runs strict.

## Context

- `AGENTS.md` §10: "a newly introduced check may run in warning mode only
  while a referenced task ID owns its tightening; an untracked warning-mode
  check is a policy violation." `check_docs_sync.py`'s warning mode has no
  owning task ID — this task becomes that ID.
- `docs/agent/docs-sync-policy.md` promises `--strict` "when enabling CI
  enforcement in later migration phases" with no owner; either wire it into
  `ci-docs.yml` strict (fixing any current violations first) or rewrite the
  policy text to describe the on-demand model honestly.
- `check_task_state_links.py` is invoked neither by any workflow nor by
  `check_task_policy.py`; decide its home (likely `ci-docs.yml` alongside the
  other structural checks) and wire it, or document why it stays
  local-only.

## Required changes

- [ ] Run both validators strict locally; fix or task-track any existing
      violations they surface.
- [ ] Add both to `ci-docs.yml` (strict) or record the explicit decision not
      to, with rationale, in `docs/agent/docs-sync-policy.md`.
- [ ] Update `docs/agent/docs-sync-policy.md` wording to match the outcome.

## Tests

- [ ] `ci-docs.yml` passes on a clean tree with the new gates enabled.

## Docs

- [ ] `docs/agent/docs-sync-policy.md` reflects the actual enforcement mode.
- [ ] `python3 tools/agents/sync_skills.py --write` re-run after the policy
      doc edit.

## Acceptance criteria

- [ ] No validator is documented as enforcement while running nowhere.
- [ ] Any remaining warning-mode check names this task's successor as owner.

## Verification

```bash
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/agents/check_task_state_links.py --root .
python3 tools/agents/sync_skills.py --check
```

## Forbidden changes

- Weakening existing strict checks.
- Expanding validator scope beyond current rules.
