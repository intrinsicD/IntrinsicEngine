# GEOM-005 — Geometry API style and numeric policy

## Goal
- Define the canonical `src/geometry` API style, naming, error-reporting, numeric-tolerance, and diagnostics policy for future geometry algorithms and paper-method integrations.

## Non-goals
- No C++ behavior changes beyond optional documentation examples.
- No module/file renames.
- No broad migration of existing APIs in this task.
- No new geometry algorithms.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` (`geometry -> core` only).
- Seeded by [`docs/reviews/2026-05-12-src-geometry-gap-analysis.md`](../../../docs/reviews/2026-05-12-src-geometry-gap-analysis.md).
- Current inconsistencies include module/file/namespace drift, mixed `bool`/`std::optional`/status/`Core::Expected` failure reporting, inconsistent count/size naming, and implicit tolerance policy.
- Mesh, graph, and point-cloud APIs should be treated as peer domains with explicit borrowed-view, hard-copy, and move/consume semantics.
- This task should establish policy first; compatibility migrations should be separate mechanical or semantic follow-up tasks.

## Required changes
- [ ] Add a geometry API style guide under `docs/architecture/` or `docs/agent/` with rules for module names, namespaces, file names, public/private state, accessor naming, result diagnostics, and count/size terminology.
- [ ] Define the policy for new failure-reporting APIs, including when to use `Core::Expected<T>`, status enums, `std::optional`, and structured diagnostics.
- [ ] Define the numeric policy for `glm::vec*` public storage, `float` vs `double` internal computation, tolerance selection, scale normalization, and deterministic/reproducible behavior.
- [ ] Define symmetric `Mesh` / `Graph` / `PointCloud` domain-view policy: algorithms request the least structured domain they need; borrowed views are explicit; hard copies and moves are opt-in semantics.
- [ ] Decide and document whether `Geometry.LinearSolver` is public or internal; create a follow-up implementation task if re-exporting or hiding it requires code changes.
- [ ] Add a short compatibility/migration section that forbids mixing mechanical renames with semantic algorithm changes.
- [ ] Cross-link the new policy from `docs/architecture/geometry.md`.

## Tests
- [ ] Run documentation link validation.
- [ ] Run task policy validation.
- [ ] If generated inventories are affected by any module-surface wording or examples, refresh and verify `docs/api/generated/module_inventory.md` in a separate implementation task.

## Docs
- [ ] Update `docs/architecture/geometry.md` with the new policy link.
- [ ] Reference the policy from future `GEOM-*` algorithm tasks where applicable.

## Acceptance criteria
- [ ] New geometry tasks have a single documented policy to cite for style, diagnostics, and numeric behavior.
- [ ] The policy preserves `geometry -> core` layering and does not introduce higher-layer dependencies.
- [ ] Existing API inconsistencies are recorded as follow-up migration candidates rather than silently refactored.
- [ ] Documentation and task links validate.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not rename modules or namespaces in this policy task.
- Do not change geometry algorithm behavior.
- Do not add dependencies outside the `geometry -> core` contract.


