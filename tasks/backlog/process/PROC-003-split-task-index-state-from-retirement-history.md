# PROC-003 — Split task index state from retirement history

## Goal
- Reduce the mandatory session-start reading to current state only: `tasks/active/README.md` describes only currently active tasks, `tasks/backlog/README.md` lists only open work per theme, and all retirement narratives move to a single append-only `tasks/done/RETIREMENT-LOG.md`.

## Non-goals
- No deletion of any retirement narrative — every existing "Previously-active" entry moves verbatim into the retirement log.
- No changes to theme definitions, priorities, or dependency semantics (only their presentation: open vs. history).
- No structured front-matter or generated sections (that is `PROC-004`).
- No changes to task files themselves.

## Context
- Owner/layer: task indexes (`tasks/active/README.md`, `tasks/backlog/README.md`, `tasks/done/README.md`) and the retirement procedure in `docs/agent/task-format.md`. No engine code.
- Measured problem (2026-06-09): `tasks/active/README.md` is 532 lines with zero active tasks — ~520 lines are retirement changelog; `tasks/backlog/README.md` is 334 lines whose Theme A member lists are ~90% `(done)` links and whose dependency anchors are mostly marked "Satisfied". Every session pays this reading cost, it grows monotonically, and history formatted as actionable state invites misreading retired tasks as open.
- The onboarding prompt (`docs/agent/prompt/prompt.md`) mandates reading both READMEs every session, so this is the highest-leverage context-cost fix that needs no new tooling.
- This is a mechanical doc move plus a small procedure edit; per repo policy they land as separate commits in one PR.
- Depends on `PROC-002` (ID uniqueness protects the follow-up ecosystem); blocks `PROC-004` (the generator should target the cleaned index shape).

## Required changes
- [ ] Create `tasks/done/RETIREMENT-LOG.md` (append-only, newest first) and move every "Previously-active" narrative block from `tasks/active/README.md` into it verbatim, preserving links and dates.
- [ ] Cut `tasks/active/README.md` to: the placement/format rules, the "Currently active" section, and a single link to the retirement log (~20 lines).
- [ ] In `tasks/backlog/README.md`, per theme: keep the rationale paragraph and **open** members only; replace retired member lists and satisfied anchors with one "History" line linking to the retirement log and the relevant `tasks/done/` files. Theme A collapses to a short "complete for the scoped acceptance; see history" block.
- [ ] In the "Cross-domain dependency anchors" section, keep only anchors with at least one open endpoint; move fully satisfied anchors to the history link.
- [ ] Update the retirement procedure in `docs/agent/task-format.md`: retiring a task appends the summary to `tasks/done/RETIREMENT-LOG.md` instead of editing `tasks/active/README.md`, and removes the task's entry from index member lists rather than annotating it `(done)`.
- [ ] Extend `tools/agents/check_task_state_links.py` with the regrowth guard: a member-list entry in `tasks/active/README.md` or a backlog README "Members"/"Tasks" list that links into `tasks/done/` is a strict finding (the dedicated "History"/"Recently retired" lines and the retirement log itself are exempt).

## Tests
- [ ] `python3 tools/agents/check_task_state_links.py --root . --strict` passes on the restructured tree.
- [ ] Add a throwaway `(done)`-link member entry to a backlog README, confirm the extended checker fails, then remove it.
- [ ] `python3 tools/docs/check_doc_links.py --root .` passes (all moved links still resolve).
- [ ] `python3 tools/agents/check_task_policy.py --root . --strict` passes.

## Docs
- [ ] `tasks/done/README.md` links the retirement log and states the append-only convention.
- [ ] `docs/agent/task-format.md` retirement procedure updated as above.
- [ ] Re-run the skill mirror sync so `intrinsicengine-task-workflow` references match.

## Acceptance criteria
- [ ] `tasks/active/README.md` ≤ 30 lines and contains no retired-task narratives.
- [ ] `tasks/backlog/README.md` member lists contain no `(done)` entries; the file drops to roughly half its current length while every theme keeps its rationale.
- [ ] Every pre-existing retirement narrative is findable verbatim in `tasks/done/RETIREMENT-LOG.md`.
- [ ] The state-link checker mechanically prevents `(done)` rot from regrowing in member lists.

## Verification
```bash
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
wc -l tasks/active/README.md tasks/backlog/README.md
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Dropping, rewording, or summarizing any retirement narrative during the move (verbatim relocation only; the checker change is the separate semantic commit).
- Changing theme priorities, gates, or dependency edges.
