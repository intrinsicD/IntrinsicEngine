---
id: PROC-021
theme: H
depends_on: []
---
# PROC-021 — Wire docs-sync and task-state-link validators into CI (or retire the promise)

## Status

- Completed 2026-07-10 on branch `main` in this workspace.
- Maturity: `Operational`.
- Commit: this local enforcement-and-retirement commit.
- Outcome: both validators run as strict `ci-docs` merge gates; docs-sync uses
  the pull request's base SHA with full checkout history.

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

- [x] Run both validators strict locally; fix or task-track any existing
      violations they surface.
- [x] Add both to `ci-docs.yml` (strict) or record the explicit decision not
      to, with rationale, in `docs/agent/docs-sync-policy.md`.
- [x] Update `docs/agent/docs-sync-policy.md` wording to match the outcome.

## Tests

- [x] `ci-docs.yml` passes on a clean tree with the new gates enabled.

## Docs

- [x] `docs/agent/docs-sync-policy.md` reflects the actual enforcement mode.
- [x] `python3 tools/agents/sync_skills.py --write` re-run after the policy
      doc edit.

## Acceptance criteria

- [x] No validator is documented as enforcement while running nowhere.
- [x] Warning mode remains only an explicit local preview; CI enforcement is
      strict, so no successor owner is required.

## Verification

```bash
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/sync_skills.py --check
```

Local closure evidence on 2026-07-10:

- Strict diff-mode docs synchronization passed for the complete branch delta;
  strict task-state links indexed 678 task IDs with zero findings.
- `Test.CheckTaskStateLinks.py` passed 4/4, including a static contract that
  pins full-history checkout, PR-base selection, both strict commands, and the
  test's own `ci-docs` wiring. Enabling that test exposed and corrected one
  stale fixture that placed a retired link in the state-only top-level backlog
  index; the accepted fixture now uses a category `## Retired` section.
- The canonical docs-sync policy, generated reference mirror, and the
  hand-maintained skill routing summary all describe current strict CI
  enforcement. `sync_skills.py --check` passes.
- The local equivalent of the complete `docs-validation` job, strict task
  policy/test layout, documentation links, workflow YAML parsing, Ruff, and
  session-brief freshness pass.

## Completion

- Completed: 2026-07-10. Commit: this local enforcement-and-retirement commit.
- Maturity: `Operational`; the gates run in the repository's documentation CI
  workflow and their wiring is covered by a static regression test.
- Both previously orphaned validators are merge-blocking `ci-docs` steps; no
  warning-mode enforcement promise remains and no successor task is required.

## Forbidden changes

- Weakening existing strict checks.
- Expanding validator scope beyond current rules.
