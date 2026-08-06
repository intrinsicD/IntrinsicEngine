---
id: RORG-031E
theme: F
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: "Reviewed the full catalog; this planning-seed retirement reconciles task inventories and dependency records without changing a reusable subsystem, geometry data-domain, method integration, publication, or control-surface contract."
---
# RORG-031E — Geometry and method-readiness backlog seed

## Status

- Re-gated as a `REVIEW-003` blocker on 2026-08-06. Its open-child inventory
  and verification still name retired `GEOM-058`, `GEOM-062`, and `GEOM-064`
  while omitting open `GEOM-073` and `GEOM-074`. The independently maintained
  geometry backlog and task front matter now carry the live dependency graph,
  so the deletion test is to reconcile the current maps and retire this seed
  instead of refreshing a second long-lived umbrella inventory.

## Goal
- Reconcile this stale duplicate inventory with the authoritative geometry/task
  maps, prove every surviving child and geometry-to-method dependency is
  independently tracked, and retire the umbrella.

## Non-goals
- Implementing new geometry algorithms in this task.
- Changing geometry ownership boundaries in this task.
- Duplicating the per-task detail that lives in the child task files and
  [`tasks/backlog/geometry/README.md`](README.md).

## Context
- Geometry remains a core subsystem target for migration and future
  methods/paper integration; `geometry -> core` only.
- Current open children under `tasks/backlog/geometry/` are `GEOM-013`,
  `GEOM-024`, `GEOM-059..061`, and `GEOM-065..074`. The category README and
  each task's front matter already carry their scope and dependency order.
- Retired children/foundations: `GEOM-005..012`, `GEOM-014..016`,
  `GEOM-017`, `GEOM-019..023`, `GEOM-025..034`, `GEOM-037..052`,
  `GEOM-054`, `GEOM-055`, `GEOM-058`, `GEOM-062..064`, `GEOIO-002`, and
  `GEOIO-003` (narratives in the retirement log; index in the category README).
- Concrete method-readiness edges are already encoded on consuming tasks,
  including `GEOM-024 → METHOD-006/METHOD-024`, retired `GEOM-058 →
  METHOD-015/METHOD-017`, retired `GEOM-062 → METHOD-016/017/018`, and
  retired `GEOM-064 → METHOD-021/022`. Future acceleration ideas without a
  consuming task are not coordination work for a permanent seed.

## Required changes
- [ ] Replace the stale child snapshot with the exact current open set and
      verify each child is independently indexed in the geometry README.
- [ ] Verify every concrete geometry→method gate named above exists in the
      consuming task's `depends_on`; drop speculative future edges from the
      umbrella premise.
- [ ] Reconcile the root Theme F open-set prose with the generated task state;
      remove retired runtime leaves and this seed when it retires.
- [ ] Retire this seed and record that category indexes, task front matter, and
      `tasks/SESSION-BRIEF.md` supersede its duplicate inventory.

## Tests
- [ ] No code changes; exact current/retired path probes and strict task/link
      validators prove the reconciled map.

## Docs
- [ ] Remove this seed from the geometry/root open-member lists, correct the
      stale root runtime-leaf claim, append its retirement record, and
      regenerate `tasks/SESSION-BRIEF.md`.

## Acceptance criteria
- [ ] The final recorded open-child snapshot matches the files under
      `tasks/backlog/geometry/` before this seed moves out of that directory.
- [ ] Every geometry→method gate this seed names is encoded in the consuming
      task's `depends_on` front-matter.
- [ ] Root/category task maps agree with `tasks/SESSION-BRIEF.md` about the
      current Theme F and geometry open sets.
- [ ] No second long-lived geometry child inventory replaces this seed.
- [ ] This task resides in `tasks/done/` and no open index presents it as
      selectable work.

## Verification
```bash
for t in GEOM-013 GEOM-024 GEOM-059 GEOM-060 GEOM-061 GEOM-065 GEOM-066 GEOM-067 GEOM-068 GEOM-069 GEOM-070 GEOM-071 GEOM-072 GEOM-073 GEOM-074; do
  ls tasks/backlog/geometry/${t}-*.md >/dev/null || { echo "missing ${t}"; exit 1; }
done
for t in GEOM-058 GEOM-062 GEOM-064; do
  ls tasks/done/${t}-*.md >/dev/null || { echo "not retired ${t}"; exit 1; }
done
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Implementing child-task scope under cover of this seed.

## Maturity
- Target: `Retired`; authoritative category indexes and task front matter now
  carry the surviving work without this coordination umbrella.
