---
id: PROC-011
theme: H
depends_on: [DOCS-003]
maturity_target: Retired
completed_on: 2026-06-29
---
# PROC-011 — Route the contract to the architecture index + add backend/config authoring checks

## Goal
- Route `AGENTS.md` to the principle-bearing architecture docs (via the canonical
  index only) and capture the config-lane + backend-axis expectations at
  authoring time, so agents are actually directed to P1/P3/P4/P5 guidance and new
  features declare their control surfaces and backend axis (P6).

## Non-goals
- Editing engine code.
- Appending rows directly to legacy-background docs (route only to canonical
  docs / ADRs through `docs/architecture/index.md`).
- Re-stating the P1/P3/P5 invariants (owned by `PROC-010`).

## Context
- Status: done; owner/agent: Codex; branch: `main` local iteration.
- Slice plan: docs/process completion in one slice. Route AGENTS to the
  canonical architecture index, add the two architecture-review authoring rows,
  document optional control-surface/backend task sections, regenerate mirrors and
  session brief, then retire the task.
- The `AGENTS.md` "Related expanded docs" table has **no** pointer to
  `docs/architecture/` at all, so agents are never routed to `patterns.md`,
  `algorithm-variant-dispatch.md`, `feature-module-playbook.md`, or
  `frame-graph.md`. Three of those are classified legacy-background in
  `docs/architecture/index.md`, so adding four direct rows would create a
  docs-sync inconsistency - hence routing through the canonical index.
- Depends on `DOCS-003` so the dispatch doc is reconciled before the contract
  routes readers toward backend-axis guidance.
- Owning surfaces: `AGENTS.md` (related-docs table),
  `docs/agent/architecture-review-checklist.md`, and `tasks/templates/task.md` +
  `docs/agent/task-format.md`. Mirrored via `sync_skills.py`.

## Required changes
- [x] Add ONE row to the `AGENTS.md` related-docs table routing to
      `docs/architecture/index.md` with a read-when covering
      subsystem/pattern/recipe/backend-split design (the index routes onward only
      to canonical docs/ADRs).
- [x] Add an architecture-review-checklist row: a new parallelizable engine
      algorithm declares its backend axis (CPU/GPU hook present, or GPU explicitly
      deferred with a task ID).
- [x] Add an architecture-review-checklist row: config/UI mutation routes through
      the config/command lane and is expressible in a config file
      (round-trippable), not UI-only.
- [x] Add an optional `## Control surfaces` section (config/UI/agent reachability,
      or N/A) and an optional single-line `## Backends` pointer (backend axis
      present, or deferred to TASK-ID) to `tasks/templates/task.md`, documented in
      `docs/agent/task-format.md` beside the optional `## Maturity` precedent
      (non-enforcing).
- [x] Re-run `sync_skills.py --write` and `generate_session_brief.py`.

## Tests
- [x] `python3 tools/agents/check_task_policy.py --root . --strict` passes.
- [x] `python3 tools/docs/check_doc_links.py --root .` passes.
- [x] `python3 tools/agents/validate_tasks.py --root .` passes (template still valid).
- [x] Skill mirrors in sync after `sync_skills.py --write`.

## Docs
- [x] `AGENTS.md` routes to the canonical architecture index.
- [x] `docs/agent/architecture-review-checklist.md` carries the two new rows.
- [x] `tasks/templates/task.md` + `docs/agent/task-format.md` document the two
      optional sections.

## Acceptance criteria
- [x] The contract routes to the canonical architecture index (not directly to
      legacy-background docs).
- [x] Two new architecture-review rows (backend axis, config-lane reachability)
      exist; the task template carries the two optional sections.
- [x] Mirrors/links/validators green; no engine code touched.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root .
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/sync_skills.py --write && git diff --stat -- tools/agents/skills
python3 tools/agents/generate_session_brief.py
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
git diff --check
```

## Forbidden changes
- Appending rows directly to legacy-background architecture docs.
- Making the new optional task sections validator-enforced.
- Touching engine code.

## Completion notes
- PR/commit: this retirement commit.
- Completed on 2026-06-29 at `Retired` maturity. `AGENTS.md` now routes
  architecture and backend/config design questions to
  `docs/architecture/index.md`; the architecture review checklist has backend
  axis and config/command lane rows; and the task template/task-format docs now
  carry optional `## Control surfaces` and `## Backends` authoring prompts.
- The optional Theme I proposal from `PROC-010` was not applied here because it
  remains owner-decision gated.
