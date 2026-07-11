---
id: PROC-024
theme: H
depends_on: []
---
# PROC-024 — Give the research/method track a theme and priority (draft for owner review)

## Status

- Completed 2026-07-11 on branch `claude/agentic-workflow-tasks-xakyf9`, after
  owner approval of the proposed decision below (create Theme I at P1, including
  the `GEOM-*` method-readiness seams).
- Maturity: `Retired` (task-map/tooling only; no engine code).
- Commit: this local apply-and-retirement commit.
- Applied: added `"I": "Research method implementation"` to `THEME_NAMES` in
  `tools/agents/generate_session_brief.py`; set `theme: I` front-matter on the
  16 member tasks; added the Theme I section (with rationale) to
  `tasks/backlog/README.md`; regenerated `tasks/SESSION-BRIEF.md`. All 16 tasks
  now render under `## Theme I` and the `Unthemed` research section is empty.
- Coordinated with `PROC-010`, whose superseded Theme I freed the `I` letter.

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

## Proposed decision (for review)

Recommendation: **create a dedicated theme for the research/method track**
rather than leaving it `Unthemed`. Drafted for owner approval; nothing below is
applied yet.

- **Theme letter:** `I` (freed by `PROC-010`'s superseded Theme I).
- **Name:** *Research method implementation*.
- **Priority:** **P1** — co-equal with Theme B (rendering modernization) and
  Theme C (physics), not above them. Rationale: `AGENTS.md` §1 names a
  "scientifically rigorous engine for graphics, geometry processing, and
  method-driven research integration" as the mission; the P0 foundation themes
  (A visible-geometry, D ECS, E geometry-IO) are complete, so the research track
  belongs alongside the remaining P1 engine work, not structurally beneath it.
- **Proposed `tasks/backlog/README.md` section (draft):**

  > ### Theme I — Research method implementation (P1)
  > Implement the paper/method reference-backend track per the method workflow
  > (`AGENTS.md` §6): CPU reference backend first, correctness tests, benchmark
  > harness, then optional optimized/GPU parity. Also covers the geometry
  > method-readiness seams that unblock those methods. Members carry their own
  > `depends_on` edges; the picker takes the earliest unblocked member.
  > Members (16 open): `METHOD-003`, `METHOD-004`, `METHOD-005`, `METHOD-006`
  > (blocked by `GEOM-024`), `METHOD-007`, `METHOD-014`, `METHOD-015` (blocked by
  > `GEOM-058`), `METHOD-016`; `GEOM-013`, `GEOM-014`, `GEOM-019`, `GEOM-024`,
  > `GEOM-058`, `GEOM-059`, `GEOM-060`, `GEOM-061`.

- **If approved:** add `"I": "Research method implementation"` to `THEME_NAMES`
  in `tools/agents/generate_session_brief.py`; set `theme: I` front-matter on the
  16 member tasks; add the section above to `tasks/backlog/README.md`; regenerate
  `tasks/SESSION-BRIEF.md`.
- **Open sub-decisions for the owner:** (1) confirm P1 vs a different priority;
  (2) confirm all 8 open research `GEOM-*` seams belong in Theme I, or scope the
  theme to `METHOD-*` only and leave the `GEOM-*` seams in the geometry backlog;
  (3) confirm the theme name.

## Required changes

- [x] Owner decision: dedicated theme letter + priority for the
      method/research track (or an explicit recorded decision to keep it
      unthemed and why).
- [x] If themed: set `theme:` front-matter on the member tasks, add the theme
      section + rationale to `tasks/backlog/README.md`, and regenerate
      `tasks/SESSION-BRIEF.md`.

## Tests

- [x] `python3 tools/agents/generate_session_brief.py --check` passes after
      regeneration.

## Docs

- [x] `tasks/backlog/README.md` theme map updated (or decision recorded).

## Acceptance criteria

- [x] The research track's scheduling status is an explicit recorded decision
      rather than a default.

## Verification

```bash
python3 tools/agents/generate_session_brief.py --check
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Changing theme priorities without the owner's sign-off recorded in this
  file.
