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

- [RUNTIME-269 — Shared Vulkan sparse solve kernels for existing methods](RUNTIME-269-shared-vulkan-sparse-solve-kernels.md)

## Scene lighting and point presentation

- [`RUNTIME-218` — Default scene lighting and light authoring](RUNTIME-218-default-scene-lighting-and-light-authoring.md)
  owns default scene lighting and editor light authoring.
- [`RUNTIME-222` — Model-space point radius rendering](RUNTIME-222-model-space-point-radius-rendering.md)
  owns published radius-property binding and camera projection in model-space units.

Each task file states its own goal, non-goals, prerequisites, and verification
commands. Read those notes before scheduling: dependencies between local tasks
and their paired UI or method work are recorded there, not here.

## Compilation locality

- [RUNTIME-272 — Compile shared property-domain test fixtures once](RUNTIME-272-compiled-property-domain-test-fixtures.md)

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
