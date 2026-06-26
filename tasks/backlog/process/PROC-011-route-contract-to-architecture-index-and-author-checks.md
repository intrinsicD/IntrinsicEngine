---
id: PROC-011
theme: H
depends_on: [DOCS-003]
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
- The `AGENTS.md` "Related expanded docs" table has **no** pointer to
  `docs/architecture/` at all, so agents are never routed to `patterns.md`,
  `algorithm-variant-dispatch.md`, `feature-module-playbook.md`, or
  `frame-graph.md`. Three of those are classified legacy-background in
  `docs/architecture/index.md` (line ~79), so adding four direct rows would
  create a docs-sync inconsistency — hence routing through the canonical index.
- Depends on `DOCS-003` so the dispatch doc is reconciled before the contract
  routes readers toward backend-axis guidance.
- Owning surfaces: `AGENTS.md` (related-docs table),
  `docs/agent/architecture-review-checklist.md`, and `tasks/templates/task.md` +
  `docs/agent/task-format.md`. Mirrored via `sync_skills.py`.

## Required changes
- [ ] Add ONE row to the `AGENTS.md` related-docs table routing to
      `docs/architecture/index.md` with a read-when covering
      subsystem/pattern/recipe/backend-split design (the index routes onward only
      to canonical docs/ADRs).
- [ ] Add an architecture-review-checklist row: a new parallelizable engine
      algorithm declares its backend axis (CPU/GPU hook present, or GPU explicitly
      deferred with a task ID).
- [ ] Add an architecture-review-checklist row: config/UI mutation routes through
      the config/command lane and is expressible in a config file
      (round-trippable), not UI-only.
- [ ] Add an optional `## Control surfaces` section (config/UI/agent reachability,
      or N/A) and an optional single-line `## Backends` pointer (backend axis
      present, or deferred to TASK-ID) to `tasks/templates/task.md`, documented in
      `docs/agent/task-format.md` beside the optional `## Maturity` precedent
      (non-enforcing).
- [ ] Re-run `sync_skills.py --write` and `generate_session_brief.py`.

## Tests
- [ ] `python3 tools/agents/check_task_policy.py --root . --strict` passes.
- [ ] `python3 tools/docs/check_doc_links.py --root .` passes.
- [ ] `python3 tools/agents/validate_tasks.py --root .` passes (template still valid).
- [ ] Skill mirrors in sync after `sync_skills.py --write`.

## Docs
- [ ] `AGENTS.md` routes to the canonical architecture index.
- [ ] `docs/agent/architecture-review-checklist.md` carries the two new rows.
- [ ] `tasks/templates/task.md` + `docs/agent/task-format.md` document the two
      optional sections.

## Acceptance criteria
- [ ] The contract routes to the canonical architecture index (not directly to
      legacy-background docs).
- [ ] Two new architecture-review rows (backend axis, config-lane reachability)
      exist; the task template carries the two optional sections.
- [ ] Mirrors/links/validators green; no engine code touched.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root .
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/sync_skills.py --write && git diff --stat -- tools/agents/skills
```

## Forbidden changes
- Appending rows directly to legacy-background architecture docs.
- Making the new optional task sections validator-enforced.
- Touching engine code.
