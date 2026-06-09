---
id: PROC-004
theme: H
depends_on: [PROC-002, PROC-003]
---
# PROC-004 — Structured task front-matter and generated session brief

## Goal
- Make "what is open, what is unblocked, and what gates what" machine-derived: open backlog tasks carry YAML front-matter (`id`, `theme`, `depends_on`), and a committed, CI-freshness-checked `tasks/SESSION-BRIEF.md` (~60 lines) replaces most of the mandatory session-start reading.

## Non-goals
- No front-matter migration of the ~346 retired tasks under `tasks/done/` — the generator only needs their existence, which filenames already provide.
- No removal of theme rationale prose from `tasks/backlog/README.md` in this task; retiring the hand-written dependency-anchor prose is the explicitly skippable Slice C.
- No generate-on-demand-only mode: the brief is committed so bare API clients that can only read files (per the `AGENTS.md` skills note) still benefit.
- No scheduler/assignment logic — the brief reports state, it does not pick work.

## Context
- Owner/layer: task tooling (`tools/agents/`), task files under `tasks/backlog/`, `tasks/templates/task.md`, onboarding prompt. No engine code.
- Today the dependency DAG lives as hand-maintained prose in `tasks/backlog/README.md` ("Cross-domain dependency anchors"); every retirement requires multi-list manual edits, and nothing validates the prose against task-file reality.
- The committed-artifact + freshness-check idiom already exists for `docs/api/generated/module_inventory.md` in `ci-docs.yml`; this task reuses it.
- `tools/agents/validate_tasks.py` currently expects the task title as the first non-empty line; front-matter support requires the parser to skip a leading `---` YAML block. `ci-docs.yml` already installs `pyyaml`.
- Depends on `PROC-002` (ID uniqueness — IDs become foreign keys) and `PROC-003` (clean index shape to generate into). `PROC-006` consumes the brief as its surface.
- Slice plan: Slice A (front-matter + validation), Slice B (generator + brief + prompt update), Slice C (optional: retire prose anchors). Promote to `tasks/active/` when starting, per multi-slice policy.

## Required changes
- [x] Slice A: define the front-matter schema — required `id` (must equal the filename/title ID), `theme` (letter key from the backlog README), `depends_on` (list of task IDs, possibly empty); optional `maturity_target`. Document it in `docs/agent/task-format.md` and add a commented example to `tasks/templates/task.md`.
- [x] Slice A: add front-matter to all open task files under `tasks/backlog/` (~43 files) and any under `tasks/active/`, deriving `depends_on` from each file's existing Context/dependency statements and the backlog README anchors.
- [x] Slice A: extend `tools/agents/validate_tasks.py` to skip a leading YAML block when locating the title, and to validate on open tasks: parseable YAML, `id` matches filename and title, `depends_on` entries resolve to an existing task file in `active/`, `backlog/`, or `done/`.
- [x] Slice B: add `tools/agents/generate_session_brief.py` producing `tasks/SESSION-BRIEF.md` with: currently active tasks; per theme, open tasks whose `depends_on` all resolve to `tasks/done/` ("unblocked") and blocked tasks with their first unmet dependency; the regeneration command in the header. Deterministic output (no timestamp — a timestamp would defeat the freshness check); landed at 70 lines for 45 open tasks.
- [x] Slice B: add a `Validate session brief freshness` step to `ci-docs.yml` (regenerate + `git diff --exit-code tasks/SESSION-BRIEF.md`), mirroring the module-inventory step.
- [x] Slice B: update the reading order in `docs/agent/prompt/prompt.md` and the `intrinsicengine-core` session-start sequence to `AGENTS.md` → `tasks/SESSION-BRIEF.md` → chosen task file, with the two READMEs demoted to on-demand depth. Touch only the reading-order section; broader prompt deduplication and loop-mode defaults are owned by `PROC-007`.
- [x] Slice C (optional): explicitly skipped — `PROC-003` already reduced the anchors section to the two open-endpoint entries, so there is no satisfied-anchor prose left to retire; edges for open tasks live in front-matter.

## Tests
- [x] `python3 tools/agents/validate_tasks.py --root tasks --strict` passes with front-matter present.
- [x] Add a throwaway task with a `depends_on` pointing at a nonexistent ID, confirm strict mode fails, then remove it.
- [x] `python3 tools/agents/generate_session_brief.py` is idempotent (second run produces no diff) and `git diff --exit-code tasks/SESSION-BRIEF.md` passes after regeneration.
- [x] Brief correctness spot-check: a task whose dependency is open appears as blocked; retiring that dependency (simulated locally) flips it to unblocked.
- [x] `python3 tools/agents/check_task_policy.py --root . --strict` and `python3 tools/docs/check_doc_links.py --root .` pass.

## Docs
- [x] `docs/agent/task-format.md` documents the front-matter schema and that retiring/opening tasks requires regenerating the brief.
- [x] `docs/agent/prompt/prompt.md` reading order updated (Slice B).
- [x] `tools/agents/README.md` documents the generator.
- [x] Re-run the skill mirror sync so `intrinsicengine-core` and `intrinsicengine-task-workflow` references match.

## Acceptance criteria
- [x] Every open task file carries valid front-matter whose `depends_on` edges resolve.
- [x] `tasks/SESSION-BRIEF.md` exists, is ≤ ~60 lines, lists active/unblocked/blocked state per theme, and CI fails when it is stale.
- [x] Session-start mandatory reading drops from ~900 lines (contract + two READMEs) to ~350 (contract + brief).
- [x] Dependency edges have exactly one authoritative home (front-matter) for open tasks.

## Verification
```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/generate_session_brief.py
git diff --exit-code tasks/SESSION-BRIEF.md
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Adding front-matter to retired tasks under `tasks/done/`.
- Hand-editing `tasks/SESSION-BRIEF.md` (generator output only).
- Removing theme rationale prose outside the explicitly optional Slice C.

## Maturity
- Target: `Operational` (brief generated, CI-freshness-checked, and adopted by the onboarding prompt).
- Slice A closes `Scaffolded → CPUContracted` (schema + validation, no consumer yet); Slice B closes `Operational`; Slice C is optional cleanup and owes no follow-up if skipped.

## Completion

- Completed 2026-06-09 on branch `claude/agentic-workflow-analysis-kohifk`.
- Commit: slice A `2f314cb` (front-matter + validation); slice B is the
  PROC-004 follow-up commit on the same branch (generator, brief, CI
  freshness gate, prompt/skill reading-order adoption).
- Deviations: no generation timestamp (determinism beats it under the
  freshness check); the brief is 70 lines rather than ≤ 60 for 45 open
  tasks — one line per task was kept over truncation so the brief stays a
  complete substitute for scanning the backlog; Slice C was skipped because
  `PROC-003` had already reduced the anchors section to open-endpoint
  entries only. Theme B's stale member list (all `GRAPHICS-036..058`
  retired) was corrected in the backlog README as part of brief adoption.
