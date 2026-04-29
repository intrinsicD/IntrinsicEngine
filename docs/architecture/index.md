# Architecture Documentation Index

This index classifies architecture documents by status to keep migration work explicit.

Status labels:

- `canonical`: current source of truth for subsystem architecture.
- `legacy-background`: historical context that may still inform decisions.
- `migration`: temporary document used during repository/source migration.
- `archival`: preserved reference not used for active design guidance.
- `generated`: machine-generated inventory/report.

## Canonical architecture set

- [Overview](overview.md) (`canonical`)
- [Layering rules](layering.md) (`canonical`)
- [Runtime](runtime.md) (`canonical`)
- [Graphics](graphics.md) (`canonical`)
- [Geometry](geometry.md) (`canonical`)
- [Assets](assets.md) (`canonical`)
- [ECS](ecs.md) (`canonical`)
- [Task graphs](task-graphs.md) (`canonical`)
- [Frame graph](frame-graph.md) (`canonical`)
- [Module rules](module-rules.md) (`canonical`)
- [Test strategy](test-strategy.md) (`canonical`)
- [Method API contract](method-api-contract.md) (`canonical`)

## ADRs

- [ADRs index](../adr/index.md) (`canonical`)
- [`0001-minimal-runtime-refactor.md`](../adr/0001-minimal-runtime-refactor.md) (`legacy-background`)
- [`0002-pragmatic-medium-runtime-refactor.md`](../adr/0002-pragmatic-medium-runtime-refactor.md) (`legacy-background`)
- [`0003-ideal-runtime-architecture.md`](../adr/0003-ideal-runtime-architecture.md) (`legacy-background`)

## Existing architecture references by status

| Document | Status | Notes |
|---|---|---|
| `algorithm-variant-dispatch.md` | legacy-background | Historical implementation options. |
| `backend_integration_slicing_policy.md` | legacy-background | Useful guidance, not canonical policy. |
| `feature-module-playbook.md` | legacy-background | Candidate content for future module handbook consolidation. |
| `frame-loop-rollback-strategy.md` | legacy-background | Runtime policy background; may be merged into runtime docs later. |
| `gpu-driven-modular-rendering-pipeline-plan.md` | migration | Planning doc for rendering migration; not canonical architecture. |
| `ground-up-redesign-blueprint-2026.md` | archival | Vision/blueprint context. |
| `ground-up-redesign-vision.md` | archival | Vision narrative; non-normative. |
| `htex-halfedge-patch-system.md` | legacy-background | Geometry method background. |
| `post-merge-audit-checklist.md` | migration | Temporary migration review artifact. |
| `rendering-three-pass.md` | legacy-background | Rendering strategy context. |
| `retained-geometry-lifecycle-consolidation-plan.md` | migration | Migration planning note. |
| `runtime-subsystem-boundaries.md` | legacy-background | Detailed runtime background. |
| `legacy-rendering-architecture-migration.md` | migration | Specific to the source-tree migration phase. |
| `legacy-task-graphs-migration.md` | migration | Specific to the source-tree migration phase. |
| `../migration/src_new_module_inventory.md` | migration | Archived migration inventory for the retired migration source tree; canonical inventory lives at `docs/api/generated/module_inventory.md`. |
| `vectorfield-overlay-lifecycle-invariants.md` | legacy-background | Subsystem-specific historical notes. |

## Notes

- New architecture decisions should be recorded in canonical docs or ADRs.
- Migration-only updates must be marked `migration` until the target source layout is complete.
