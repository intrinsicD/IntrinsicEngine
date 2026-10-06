# Runtime Backlog

Agent-facing entry point into the `tasks/backlog/runtime/` queue. It lists the
open runtime-owned backlog tasks filed here and points at the neighbouring
queues and contracts a runtime pick usually needs.

The repository agent contract is [`AGENTS.md`](../../../AGENTS.md). Work that is
in progress lives in [`tasks/active/`](../../active/), and the current
working focus is recorded in [`SESSION-BRIEF.md`](../../SESSION-BRIEF.md).

## Ownership

This queue owns engine composition, runtime modules and services, config and
agent lanes, geometry property publication, and the runtime side of render
extraction and residency.

## Method property-domain integration

The canonical property-domain contract for method integration is
[`docs/architecture/geometry-api-style.md`](../../../docs/architecture/geometry-api-style.md).

- [`RUNTIME-210` — Signed Heat runtime and config integration](RUNTIME-210-signed-heat-runtime-config-integration.md)
  owns mesh-only runtime/config integration and per-vertex publication for
  the Signed Heat CPU reference.
- [`RUNTIME-211` — K-Means property-domain integration](RUNTIME-211-kmeans-property-domain-integration.md)
  owns selected finite `vec3` property inputs and same-domain label, color
  and scalar publication for the CPU/Vulkan K-Means operation.
- [`RUNTIME-212` — Progressive Poisson property-domain publication](RUNTIME-212-progressive-poisson-property-domain-publication.md)
  owns selected finite `vec3` property inputs and source-cardinality
  hierarchy publication on the originating element domain.

## Shared Vulkan numerical kernels


## Scene lighting and point presentation

- [`RUNTIME-218` — Default scene lighting and light authoring](RUNTIME-218-default-scene-lighting-and-light-authoring.md)
  owns default scene lighting and editor light authoring.
- [`RUNTIME-222` — Model-space point radius rendering](RUNTIME-222-model-space-point-radius-rendering.md)
- [RUNTIME-275 — Gaussian noise editor operation](RUNTIME-275-gaussian-noise-editor-operation.md)
  owns published radius-property binding and camera projection in model-space units.

Each task file states its own goal, non-goals, prerequisites, and verification
commands. Read those notes before scheduling: dependencies between local tasks
and their paired UI or method work are recorded there, not here.

## Agent control lane and inspection operations

- [RUNTIME-276 — Declarative `ConfigFieldSpec` tables, schema generation and conformance test](RUNTIME-276-declarative-config-field-specs.md)
- [RUNTIME-280 — Selection-by-query operations and mask publication](RUNTIME-280-selection-query-operations.md)
- [RUNTIME-281 — Deterministic view capture command](RUNTIME-281-deterministic-view-capture-command.md)
- [RUNTIME-282 — Headless batch CLI](RUNTIME-282-headless-batch-cli.md)
- [RUNTIME-283 — Property import/export operations](RUNTIME-283-property-import-export-operations.md)
- [RUNTIME-284 — History label listing and entity-property checkpoints](RUNTIME-284-history-labels-and-checkpoints.md)
- [RUNTIME-286 — Mesh health report](RUNTIME-286-mesh-health-report.md)

## Integration of test-only geometry kernels

From REVIEW-007 E7 (2026-10-06): the operator chose end-to-end integration
(config, Sandbox UI, agent operation, publication, tests) over deletion.

- [RUNTIME-320 — Convex hull of a selection, point cloud or mesh](RUNTIME-320-convex-hull-editor-agent-integration.md)
- [RUNTIME-321 — Implicit plane field remeshing and Octree node properties](RUNTIME-321-implicit-plane-field-editor-agent-integration.md)
- [RUNTIME-322 — Average instance orientations with RotationAveraging](RUNTIME-322-rotation-averaging-editor-agent-integration.md)
- [RUNTIME-323 — Grid occupancy via SparseGrid](RUNTIME-323-sparse-grid-occupancy-editor-agent-integration.md)
- [RUNTIME-324 — Octree split point Center/Mean/Median selectable in point spacing](RUNTIME-324-octree-median-split-selectable-variant.md)

## Consolidation of duplicated runtime mechanisms

From the 2026-10-01 duplication/consistency audit; each task owns its own scope.

- [RUNTIME-311 — Unify the two-phase GPU Run/Accept transaction lifecycle](RUNTIME-311-unify-gpu-scalar-outlier-transaction-lifecycle.md)
- [RUNTIME-314 — Reuse existing runtime helpers instead of local copies](RUNTIME-314-reuse-existing-processing-helpers.md)

## Appearance attribute binding


## Compilation locality


BUILD-009 and RUNTIME-266/267/268 are complete. Existing config and prepared-frame
ownership contracts remain authoritative; further changes require a measured
consumer need. Current cross-cutting work is listed in the root backlog.

## Related queues and documentation

Runtime picks frequently touch adjacent queues:
[`architecture`](../architecture/README.md) for kernel and layering seams,
[`geometry`](../geometry/README.md) for the algorithms and containers runtime
integrates, and [`rendering`](../rendering/README.md) for the graphics side of
extraction and presentation. The canonical architecture index is
[`docs/architecture/index.md`](../../../docs/architecture/index.md).

For completed work, the [retirement log](../../done/RETIREMENT-LOG.md) links
retired task records. The directory indexes are
[`tasks/done/README.md`](../../done/README.md) and
[`tasks/archive/README.md`](../../archive/README.md).

## Completed agent-control foundations

Operator direction 2026-09-27; architecture decision in
[ARCH-019](../../done/ARCH-019-agent-control-lane-mcp.md). The registry
[RUNTIME-287](../../done/RUNTIME-287-agent-operations-registry.md) is done.
