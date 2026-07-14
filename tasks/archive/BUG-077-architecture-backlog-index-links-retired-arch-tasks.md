---
id: BUG-077
theme: G
depends_on: []
---
# BUG-077 — Architecture backlog index links retired ARCH tasks (strict gate red)

## Status
- Completed 2026-07-10 on branch `main` in this workspace.
- Fix commit: `09183ea1` (`BUG-077: mark retired seam tasks under a history
  heading`).
- Retirement scope: verification and task-state synchronization only; the
  one-line documentation fix was already present on the current branch.

## Goal
- Restore `check_task_state_links --strict` to green by moving the retired ARCH
  task links in `tasks/backlog/architecture/README.md` under a history-marked
  heading (or citing them as plain text).

## Non-goals
- No change to task content or retirement decisions; this is a docs-state fix.

## Context
- Owner/layer: `tasks`/docs.
- Merge `76528e6` retired the ADR-0024 kernel seam tasks `ARCH-007`, `ARCH-008`,
  `ARCH-009`, `ARCH-010`, `ARCH-011`, `ARCH-012`, and `ARCH-013` to `tasks/done/`
  but left `tasks/backlog/architecture/README.md` linking **all seven** under the
  active `## Tasks` heading (lines 46, 50, 54, 60, 65, 69, 76).
- The section has a plain-text `Retired seam tasks:` lead-in (line 44), but a
  plain-text line is **not** a history-marked heading, so the validator flags
  every one of the seven entries — not just ARCH-010..013.
- `python3 tools/agents/check_task_state_links.py --root . --strict` fails on the
  merged tip with seven findings: "category index links retired task ... outside a
  history-marked heading; move the entry under a history section (e.g.
  '## Retired') or cite it as plain text." The same gate was **green** on
  pre-merge main (`c476ea6`), so this is a merge-introduced regression to a strict
  structural gate that CI (`ci-docs.yml`) treats as a merge blocker.

## Required changes
- [x] Resolve **all seven** flagged links — `ARCH-007`, `ARCH-008`, `ARCH-009`,
      `ARCH-010`, `ARCH-011`, `ARCH-012`, `ARCH-013` (lines 46/50/54/60/65/69/76)
      — in `tasks/backlog/architecture/README.md`. Do not stop at ARCH-010..013;
      partial scope leaves the strict gate red.
- [x] Promote the plain-text `Retired seam tasks:` lead-in (line 44) to a
      history-marked markdown heading (e.g. `### Retired seam tasks` or
      `## Retired`) so all seven entries sit under a recognized history section,
      or convert the seven links to plain-text citations — per the validator's
      accepted forms.
- [x] Re-run `check_task_state_links --strict` and confirm **zero** findings (not
      merely that the ARCH-010..013 lines are gone).

## Tests
- [x] `python3 tools/agents/check_task_state_links.py --root . --strict` passes.

## Docs
- [x] The change is itself the docs fix; confirm `check_doc_links` stays green.

## Acceptance criteria
- [x] `check_task_state_links --strict` reports **zero** findings.
- [x] None of `ARCH-007`..`ARCH-013` (nor any other retired task) is linked under
      an active (non-history) heading.

## Verification
```bash
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

Retirement verification on 2026-07-10:

- `tasks/backlog/architecture/README.md` contains the seven retired links under
  the recognized `### Retired seam tasks` history heading introduced by
  `09183ea1`.
- `python3 tools/agents/check_task_state_links.py --root . --strict` passed with
  zero findings.
- `python3 tools/docs/check_doc_links.py --root .` and
  `python3 tools/agents/check_task_policy.py --root . --strict` passed.
- `python3 tools/agents/generate_session_brief.py --check` passed after task
  retirement synchronization.

## Completion

- Completed: 2026-07-10. Fix commit: `09183ea1`; retirement commit: this local
  task-state commit.
- Outcome: the strict task-state-link gate is green and all seven retired ARCH
  seams remain historical rather than active backlog work.
- Follow-up: none.

## Forbidden changes
- Do not un-retire the ARCH tasks to satisfy the gate.
- Mixing mechanical file moves with semantic refactors.
