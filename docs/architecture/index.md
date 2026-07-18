# Architecture Documentation Index

This index classifies architecture documents by status to keep migration work explicit.

Status labels:

- `canonical`: current source of truth for subsystem architecture.
- `legacy-background`: historical context that may still inform decisions.
- `migration`: temporary document used during repository/source migration.
- `archival`: preserved reference not used for active design guidance.
- `generated`: machine-generated inventory/report.
- `roadmap`: planning document for scoped future work; not a claim that the
  listed capability is implemented.

## Canonical architecture set

- [Overview](overview.md) (`canonical`)
- [Layering rules](layering.md) (`canonical`)
- [Kernel target state](kernel-target-state.md) (`canonical`) — north-star +
  convergence scorecard for ADR-0024 as right-sized by ADR-0027; tracked by
  [`ARCH-014`](../../tasks/active/ARCH-014-kernel-convergence-tracking.md).
- [Runtime](runtime.md) (`canonical`)
- [Runtime config control](runtime-config-control.md) (`canonical`)
- [Engine config file](engine-config.md) (`canonical`)
- [Graphics](graphics.md) (`canonical`) — reduced to the contract layer per [`DOCS-001`](../../tasks/archive/DOCS-001-reduce-graphics-architecture-prose.md); embedded decision records relocated to ADRs `0004..0018` (see Pointers section).
- [Geometry](geometry.md) (`canonical`)
- [Geometry API style and numeric policy](geometry-api-style.md) (`canonical`)
- [Assets](assets.md) (`canonical`)
- [ECS](ecs.md) (`canonical`)
- [Physics](physics.md) (`canonical`)
- [Task graphs](task-graphs.md) (`canonical`)
- [Frame graph](frame-graph.md) (`canonical`)
- [Compute parallel primitives](compute-parallel-primitives.md) (`canonical`)
- [Module rules](module-rules.md) (`canonical`)
- [Test strategy](test-strategy.md) (`canonical`)
- [Method API contract](method-api-contract.md) (`canonical`)
- [Algorithm variant dispatch pattern](algorithm-variant-dispatch.md) (`canonical`)

## ADRs

- [ADRs index](../adr/index.md) (`canonical`)
- [`0001-minimal-runtime-refactor.md`](../adr/0001-minimal-runtime-refactor.md) (`legacy-background`)
- [`0002-pragmatic-medium-runtime-refactor.md`](../adr/0002-pragmatic-medium-runtime-refactor.md) (`legacy-background`)
- [`0003-ideal-runtime-architecture.md`](../adr/0003-ideal-runtime-architecture.md) (`legacy-background`)
- [`0004-vulkan-backend-bringup-and-fallback.md`](../adr/0004-vulkan-backend-bringup-and-fallback.md) (`canonical`)
- [`0005-vulkan-operational-readiness-gate.md`](../adr/0005-vulkan-operational-readiness-gate.md) (`canonical`)
- [`0006-camera-picking-and-gizmo-runtime-handoff.md`](../adr/0006-camera-picking-and-gizmo-runtime-handoff.md) (`canonical`)
- [`0007-picking-selection-and-outline.md`](../adr/0007-picking-selection-and-outline.md) (`canonical`)
- [`0008-spatial-debug-visualizer-adapters.md`](../adr/0008-spatial-debug-visualizer-adapters.md) (`canonical`)
- [`0009-visualization-packets-and-overlay-upload.md`](../adr/0009-visualization-packets-and-overlay-upload.md) (`canonical`)
- [`0010-postprocess-chain-backend-policy.md`](../adr/0010-postprocess-chain-backend-policy.md) (`canonical`)
- [`0011-debug-view-inspection-table.md`](../adr/0011-debug-view-inspection-table.md) (`canonical`)
- [`0012-imgui-overlay-and-present-finalization.md`](../adr/0012-imgui-overlay-and-present-finalization.md) (`canonical`)
- [`0013-ecs-renderable-residency-bridge.md`](../adr/0013-ecs-renderable-residency-bridge.md) (`canonical`)
- [`0014-procedural-source-residency-bridge.md`](../adr/0014-procedural-source-residency-bridge.md) (`canonical`)
- [`0015-reference-scene-bootstrap.md`](../adr/0015-reference-scene-bootstrap.md) (`canonical`)
- [`0016-texture-residency-and-asset-cache-policy.md`](../adr/0016-texture-residency-and-asset-cache-policy.md) (`canonical`)
- [`0017-default-debug-surface-material.md`](../adr/0017-default-debug-surface-material.md) (`canonical`)
- [`0018-missing-material-fallback-substitution.md`](../adr/0018-missing-material-fallback-substitution.md) (`canonical`)
- [`0019-physics-layer-ownership-and-ecs-integration.md`](../adr/0019-physics-layer-ownership-and-ecs-integration.md) (`canonical`)
- [`0026-runtime-module-scope-by-consumer-contract.md`](../adr/0026-runtime-module-scope-by-consumer-contract.md) (`canonical`) —
  decides same-module cohesion and objective split triggers after ADR-0024
  assigns a responsibility to runtime composition.
- [`0027-right-sized-runtime-composition.md`](../adr/0027-right-sized-runtime-composition.md) (`canonical`) —
  amends ADR-0024's mechanism-prescriptive rows with evidence-backed
  keep/simplify/defer verdicts and an ownership-based convergence target.

## Existing architecture references by status

| Document | Status | Notes |
|---|---|---|
| `backend_integration_slicing_policy.md` | legacy-background | Useful guidance, not canonical policy. |
| `feature-module-playbook.md` | legacy-background | Candidate content for future module handbook consolidation. |
| `frame-loop-rollback-strategy.md` | legacy-background | Runtime policy background; may be merged into runtime docs later. |
| `geometry-pipeline-modularity.md` | roadmap | Registration-first swappable-stage pipeline design; slice roadmap owned by `GEOM-054` and follow-ups. |
| `gpu-driven-modular-rendering-pipeline-plan.md` | migration | Planning doc for rendering migration; not canonical architecture. |
| `ground-up-redesign-blueprint-2026.md` | archival | Vision/blueprint context. |
| `ground-up-redesign-vision.md` | archival | Vision narrative; non-normative. |
| `htex-halfedge-patch-system.md` | legacy-background | Geometry method background. |
| `parameterization-mapping-roadmap.md` | roadmap | GEOM-011 planning note for parameterization, atlas, distortion, and surface-map packs. |
| `post-merge-audit-checklist.md` | migration | Temporary migration review artifact. |
| `point-cloud-algorithm-roadmap.md` | roadmap | GEOM-010 planning note for point-cloud algorithm packs and method boundaries. |
| `rendering-three-pass.md` | legacy-background | Rendering strategy context. |
| `retained-geometry-lifecycle-consolidation-plan.md` | migration | Migration planning note. |
| `runtime-subsystem-boundaries.md` | legacy-background | Detailed runtime background. |
| `legacy-rendering-architecture-migration.md` | migration | Specific to the source-tree migration phase. |
| `legacy-task-graphs-migration.md` | migration | Specific to the source-tree migration phase. |
| `vectorfield-overlay-lifecycle-invariants.md` | legacy-background | Subsystem-specific historical notes. |

## Notes

- New architecture decisions should be recorded in canonical docs or ADRs.
- Migration-only updates must be marked `migration` until the target source layout is complete.
