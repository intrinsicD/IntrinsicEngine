---
id: PROC-024
theme: H
depends_on: []
---
# PROC-024 — Give the research/method track a theme and priority (draft for owner review)

## Goal

- Decide, with the repository owner, whether the paper/method implementation
  track (open `METHOD-*` and method-readiness `GEOM-*` tasks) gets its own
  convergence theme with an explicit priority, so session-start work
  selection stops structurally passing over the research mission.

## Non-goals

- No re-prioritization of existing Themes B/F/G without owner sign-off; this
  task is the decision record, drafted for review like `PROC-010`.
- No change to the theme mechanics in `generate_session_brief.py`.

## Context

- The done corpus shows 174 GRAPHICS tasks against 8 METHOD tasks, and every
  open `METHOD-*`/research `GEOM-*` task sits in the session brief's
  "Unthemed" section (`theme: none`) while engine plumbing holds Themes A–H
  with P0/P1 labels — the "scientifically rigorous research engine" mission
  (AGENTS.md §1) is structurally deprioritized by the picker rules.
- Related: `PROC-010` (open) encodes the research-engine invariants P1/P3/P5
  in the contract; this task owns the *scheduling* half of the same gap.
- Theme priorities and rationale live in `tasks/backlog/README.md`; the
  picker honors them via `docs/agent/prompt/prompt.md` §"Pick the next
  slice".

## Required changes

- [ ] Owner decision: dedicated theme letter + priority for the
      method/research track (or an explicit recorded decision to keep it
      unthemed and why).
- [ ] If themed: set `theme:` front-matter on the member tasks, add the theme
      section + rationale to `tasks/backlog/README.md`, and regenerate
      `tasks/SESSION-BRIEF.md`.

## Tests

- [ ] `python3 tools/agents/generate_session_brief.py --check` passes after
      regeneration.

## Docs

- [ ] `tasks/backlog/README.md` theme map updated (or decision recorded).

## Acceptance criteria

- [ ] The research track's scheduling status is an explicit recorded decision
      rather than a default.

## Verification

```bash
python3 tools/agents/generate_session_brief.py --check
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Changing theme priorities without the owner's sign-off recorded in this
  file.
