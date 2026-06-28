---
id: GEOM-034
theme: none
depends_on: [GEOM-027, GEOM-028, GEOM-029, GEOM-030, GEOM-031, GEOM-032, GEOM-033]
---
# GEOM-034 — Geometry property API documentation audit

## Goal
- Audit and normalize the architecture documentation for the final
  `Geometry.Properties` contract after the property-system refactor tasks land.

## Non-goals
- No C++ API or behavior changes in this task.
- No renderer/runtime/ECS/assets/platform/app implementation work.
- No aspirational documentation for behavior that has not landed.
- No historical rewrite of retired task narratives unless a link is broken.

## Context
- Status: done.
- Owner/agent: Codex.
- Completed: 2026-06-28.
- Commit: this commit (`Audit geometry property API docs`).
- Maturity: `Scaffolded` documentation synchronization; this audit is the
  intended endpoint and no `Operational` follow-up is owed.
- Owning subsystem/layer: documentation for `geometry` public APIs.
- The code refactor tasks `GEOM-027` through `GEOM-033` each own local docs for
  their behavior change; this task owns the final cross-document audit once
  those changes are current state.
- The canonical API-style policy is
  [`docs/architecture/geometry-api-style.md`](../../docs/architecture/geometry-api-style.md).
- Broader architecture docs may contain stale property/view ownership wording,
  especially where rendering or runtime plans describe geometry property access.

## Required changes
- [x] Update
      [`docs/architecture/geometry-api-style.md`](../../docs/architecture/geometry-api-style.md)
      with one coherent property API section covering names, validity, const
      access, naming style, bool/proxy storage, and descriptors.
- [x] Update
      [`docs/architecture/geometry.md`](../../docs/architecture/geometry.md)
      so domain views and property sharing refer to the final property contract.
- [x] Audit
      [`docs/architecture/rendering-target-architecture.md`](../../docs/architecture/rendering-target-architecture.md)
      and any nearby runtime/graphics docs for stale `PropertySet`, `MeshView`,
      or mutable borrowed-view claims; replace stale details with links to the
      geometry docs where appropriate.
- [x] Confirm
      [`docs/api/generated/module_inventory.md`](../../docs/api/generated/module_inventory.md)
      is current after the code tasks; regenerate and commit only if stale.
- [x] Leave historical review documents intact unless the audit finds broken
      links or text that is explicitly maintained as current-state guidance.

## Tests
- [x] Run documentation link checks.
- [x] Run task-policy validation to prove the dependency chain remains valid.
- [x] Run module inventory generation in check mode by comparing the generated
      output to the committed file after prior code tasks.

## Docs
- [x] This task is docs-only; all required documentation work is listed in
      Required changes.

## Acceptance criteria
- [x] Property API docs describe current behavior only, not planned behavior.
- [x] Geometry API style and geometry architecture docs agree on names,
      mutability, invalid/default behavior, bool storage, and descriptors.
- [x] Higher-layer docs do not duplicate or contradict the geometry-owned
      property contract.
- [x] Module inventory and task/session docs are fresh.

## Verification
```bash
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
git diff --exit-code -- docs/api/generated/module_inventory.md
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root . --strict
python3 tools/agents/generate_session_brief.py
git diff --exit-code -- tasks/SESSION-BRIEF.md
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Changing C++ source or tests under this docs-only task.
- Documenting future behavior as if it already exists.

## Maturity
- Target: `Scaffolded` for documentation synchronization; this docs audit is
  the intended endpoint, and no `Operational` follow-up is owed.
