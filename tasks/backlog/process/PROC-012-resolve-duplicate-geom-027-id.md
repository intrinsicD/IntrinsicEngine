---
id: PROC-012
theme: none
depends_on: []
---
# PROC-012 — Resolve duplicate GEOM-027 task ID

## Goal
- Resolve the duplicate `GEOM-027` task ID by renumbering the unrelated *control-surface / KMeans backend-seam* task to the next free geometry ID (`GEOM-052`) and updating every reference, so `tools/agents/check_task_policy.py --root . --strict` reports zero findings.

## Non-goals
- No change to the scope, contents, slice plan, or acceptance criteria of either affected task; this is an ID/reference fix only.
- Do NOT renumber `tasks/backlog/geometry/GEOM-027-property-name-lifetime-contract.md`. That file is the canonical `GEOM-027`: the property-system sequence depends on it (`GEOM-033` declares `depends_on` `GEOM-027`, and `GEOM-034` depends on the `GEOM-027`–`GEOM-033` sequence). Renumbering it would ripple through that dependency graph.
- No renumbering or editing of any other task ID.
- No new task numbers consumed beyond the single `GEOM-052` allocation.

## Context
- Two files both declare `id: GEOM-027`:
  - `tasks/backlog/geometry/GEOM-027-property-name-lifetime-contract.md` (`theme: none`, part of the `GEOM-027`–`GEOM-034` property-system refactor sequence) — the canonical holder of the ID.
  - `tasks/backlog/geometry/GEOM-027-shared-cpu-gpu-backend-seam-kmeans-exemplar.md` (`theme: F`, `depends_on: [DOCS-003]`, `maturity_target: CPUContracted`) — an unrelated research-engine control-surface / shared CPU-GPU backend-seam task. This is the file to renumber.
- The collision was introduced by commit `9ed14b4` ("Seed research-engine control-surface and contract-encoding backlog"); it predates and is unrelated to the bcg geometry-port backlog seeding (`GEOM-037`–`GEOM-051`).
- `check_task_policy.py --root . --strict` currently fails with exactly one finding: the duplicate `GEOM-027`.
- Next free geometry ID: `GEOM-052` (`GEOM-037`–`GEOM-051` are now allocated).
- Owning subsystem/layer: process / task-system hygiene only; no `src/` code is touched.

## Required changes
- [ ] Rename `tasks/backlog/geometry/GEOM-027-shared-cpu-gpu-backend-seam-kmeans-exemplar.md` to `tasks/backlog/geometry/GEOM-052-shared-cpu-gpu-backend-seam-kmeans-exemplar.md` (use `git mv` to preserve history).
- [ ] Update that file's front-matter `id:` from `GEOM-027` to `GEOM-052` and its `# GEOM-027 — ...` title line to `# GEOM-052 — ...`, leaving all other content unchanged (keep `theme: F`, `depends_on: [DOCS-003]`, `maturity_target: CPUContracted`).
- [ ] Update the cross-references that point at the renumbered task: the entry in `tasks/backlog/README.md` and any narrative/link in other backlog READMEs or `docs/` that refer to the control-surface/KMeans-exemplar task by `GEOM-027` or by its old filename.
- [ ] Grep the repository for `GEOM-027` and confirm every remaining mention refers to the property-name-lifetime contract (the canonical `GEOM-027`), not the renumbered task; fix any that refer to the renumbered task.
- [ ] Regenerate `tasks/SESSION-BRIEF.md` with `python3 tools/agents/generate_session_brief.py`.

## Tests
- [ ] `python3 tools/agents/check_task_policy.py --root . --strict` reports zero findings (no duplicate-ID error).
- [ ] `python3 tools/agents/validate_tasks.py --root .` passes (nine-section format intact for both files).
- [ ] `python3 tools/docs/check_doc_links.py --root .` reports no broken relative links (the renamed file's inbound links resolve).

## Docs
- [ ] Update the `tasks/backlog/README.md` listing (and any `docs/` reference) so the renumbered task appears as `GEOM-052`.
- [ ] Confirm `tasks/SESSION-BRIEF.md` reflects `GEOM-052` after regeneration.

## Acceptance criteria
- [ ] Exactly one task file declares `GEOM-027` (the property-name-lifetime contract); the control-surface/KMeans-exemplar task is `GEOM-052`.
- [ ] `check_task_policy.py --root . --strict` passes with zero findings.
- [ ] No inbound reference to the renumbered task still says `GEOM-027`, and no reference to the property sequence was disturbed (`GEOM-033`/`GEOM-034` dependencies still resolve to `GEOM-027`).
- [ ] Doc-link and task-format validators pass; `SESSION-BRIEF.md` is regenerated.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root .
python3 tools/agents/generate_session_brief.py
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work or editing either task's scope/contents.
- Renumbering `GEOM-027-property-name-lifetime-contract.md` or any task ID other than the single control-surface/KMeans-exemplar task.
- Consuming more than one new task number.
