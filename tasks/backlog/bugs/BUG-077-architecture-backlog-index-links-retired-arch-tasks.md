---
id: BUG-077
theme: G
depends_on: []
---
# BUG-077 — Architecture backlog index links retired ARCH tasks (strict gate red)

## Goal
- Restore `check_task_state_links --strict` to green by moving the retired ARCH
  task links in `tasks/backlog/architecture/README.md` under a history-marked
  heading (or citing them as plain text).

## Non-goals
- No change to task content or retirement decisions; this is a docs-state fix.

## Context
- Owner/layer: `tasks`/docs.
- Merge `76528e6` retired `ARCH-010`, `ARCH-011`, `ARCH-012`, and `ARCH-013` to
  `tasks/done/` but left `tasks/backlog/architecture/README.md` linking them as
  active category entries (lines ~60, 65, 69, 76).
- `python3 tools/agents/check_task_state_links.py --root . --strict` fails on the
  merged tip with: "category index links retired task ... outside a
  history-marked heading; move the entry under a history section (e.g.
  '## Retired') or cite it as plain text." The same gate was **green** on
  pre-merge main (`c476ea6`), so this is a merge-introduced regression to a strict
  structural gate that CI (`ci-docs.yml`) treats as a merge blocker.

## Required changes
- [ ] Move the `ARCH-010`/`ARCH-011`/`ARCH-012`/`ARCH-013` entries in
      `tasks/backlog/architecture/README.md` under a `## Retired` (history)
      heading, or convert them to plain-text citations, per the validator's
      guidance.
- [ ] Re-run the gate to confirm green.

## Tests
- [ ] `python3 tools/agents/check_task_state_links.py --root . --strict` passes.

## Docs
- [ ] The change is itself the docs fix; confirm `check_doc_links` stays green.

## Acceptance criteria
- [ ] `check_task_state_links --strict` is green.
- [ ] No active-category link points at a retired task.

## Verification
```bash
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not un-retire the ARCH tasks to satisfy the gate.
- Mixing mechanical file moves with semantic refactors.
