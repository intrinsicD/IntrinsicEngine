# Clean-workshop architecture review gate

A lightweight scorecard for catching slow architectural drift — god-object
growth, boundary leaks, stringly-typed routing, and scaffold accumulation —
before it becomes an expensive foundation to revisit. Installed by
[`WORKSHOP-009`](../../../../../tasks/archive/WORKSHOP-009-clean-workshop-review-gate.md).

This gate is **not** a per-PR blocker and **not** a replacement for tests or the
per-PR [`review-checklist.md`](../../../../../docs/agent/review-checklist.md). It is a focused,
objective-where-possible pass run at the moments below, producing follow-up task
IDs rather than vague TODOs. For broad architecture changes it complements (does
not replace) [`architecture-review-checklist.md`](../../../../../docs/agent/architecture-review-checklist.md).

## When this review is required

Run the scorecard when a change does any of:

- changes a dependency boundary (a new cross-layer import or CMake link edge);
- adds a renderer subsystem, member, or frame-graph pass;
- changes RHI / platform / runtime wiring or composition order;
- closes a `Scaffolded` or parity task (see the maturity taxonomy in
  [`task-maturity.md`](../../../../../docs/agent/task-maturity.md));
- adds or edits a `tools/repo/layering_allowlist.yaml` entry.

If none of these apply, the per-PR `review-checklist.md` is sufficient.

## Scorecard

Score each row `pass | finding | n/a`. A `finding` does not block the PR by
itself; it must produce a follow-up backlog task ID (see "Recording findings").

| # | Check | How to evaluate | Tooling today |
| --- | --- | --- | --- |
| 1 | Promoted layer imports match `/AGENTS.md` §2 | `python3 tools/repo/check_layering.py --root src --strict` is clean, and any new edge is allowed by the §2 table (not just allowlisted). | automated |
| 2 | CMake target links match layer policy | New `target_link_libraries(...)` edges respect the §2 table; the strict layer check is module- and CMake-aware (`WORKSHOP-001`). | automated |
| 3 | No new public API exposes a higher-layer type to a lower layer | Inspect added `.cppm` export surfaces: a lower layer's interface must not name a higher layer's type (e.g. no `ECS`/`Runtime` types in a `graphics` public API). | manual |
| 4 | Renderer member/subsystem growth is justified by an owning seam | A new renderer member/subsystem has a named owning seam (registry/system), not another field bolted onto the renderer god-object. Track decomposition under `WORKSHOP-005`/`WORKSHOP-006`. | manual |
| 5 | New passes use typed IDs, not string routing | A new frame-graph pass is routed by a typed identity, not a stringly-typed name lookup. Typed pass/resource identity landed in `WORKSHOP-003`, and renderer command routing is keyed by `FramePassId` via `WORKSHOP-004`; flag new string-routed passes as regressions. | manual (typed infra: `WORKSHOP-003`; router: `WORKSHOP-004`) |
| 6 | New frame-recipe dependencies are resource-driven or explicitly justified | A new recipe edge is derived from resource read/write dependencies, or the task records why an explicit ordering edge is needed. Dependency-driven recipes are owned by `WORKSHOP-007`. | manual (`WORKSHOP-007`) |
| 7 | Scaffold/parity tasks have a follow-up maturity gate | A task retiring at `Scaffolded`/`CPUContracted` names its `Operational`/`ParityProven` follow-up or records that no follow-up is owed (the `Scaffolded` closure rule in [`task-maturity.md`](../../../../../docs/agent/task-maturity.md)). `tools/agents/check_task_policy.py --strict` enforces the backend-facing subset. | partial (`check_task_policy.py`) |
| 8 | Legacy/temporary exceptions have a task ID and expiry | Every `layering_allowlist.yaml` row and every "temporary"/shim marker names an open removal owner (`AGENTS.md` §13); `python3 tools/repo/check_layering_allowlist_quality.py --root . --strict` covers the allowlist. | partial (allowlist automated; source markers via the [`drift-audit-checklist.md`](../../../../../docs/agent/drift-audit-checklist.md) Row 7) |

## Command bundle

Run the automated subset and print this checklist's location:

```bash
tools/ci/run_clean_workshop_review.sh . --strict
```

The bundle runs the strict layer check, the allowlist-quality check, the
task-policy check, and the doc-link check, then prints where to record findings.
It is a convenience wrapper over existing validators — it adds no new gate.

## Recording findings

Record the review in `docs/reviews/<YYYY-MM-DD>-clean-workshop-review.md` (or
fold the scorecard into the change's review notes) using the example at
[`docs/reviews/2026-06-06-clean-workshop-review-example.md`](../../../../../docs/reviews/2026-06-06-clean-workshop-review-example.md).
Each `finding` row must link a follow-up task ID; never leave a bare TODO.

## Related

- [`review-checklist.md`](../../../../../docs/agent/review-checklist.md) — per-PR pre-commit gate.
- [`architecture-review-checklist.md`](../../../../../docs/agent/architecture-review-checklist.md) — the
  deeper architecture-impacting-change checklist.
- [`task-maturity.md`](../../../../../docs/agent/task-maturity.md) — the `Scaffolded → Retired` taxonomy.
- [`drift-audit-checklist.md`](../../../../../docs/agent/drift-audit-checklist.md) — whole-tree state
  drift (untracked markers, dead seams).
- [`/AGENTS.md`](../../../../../AGENTS.md) §2/§4/§13 — layering invariants and temporary
  exception rules.
- [`tasks/backlog/workshop/README.md`](../../../../../tasks/backlog/workshop/README.md) —
  the clean-workshop task pack.
