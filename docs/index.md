# IntrinsicEngine Documentation Index

This page is the canonical entry point for repository documentation.

## Architecture

- [Architecture documents](architecture/) — subsystem design notes, runtime boundaries, rendering plans, and migration-era architecture records.
- [Rendering three-pass architecture](architecture/rendering-three-pass.md)
- [Runtime subsystem boundaries](architecture/runtime-subsystem-boundaries.md)
- [Task graph architecture](architecture/src_new-task-graphs.md)

## ADRs

Current ADR-style documents are maintained under `docs/architecture/` until the dedicated ADR migration is completed:

- [ADR O1 — Minimal runtime refactor](architecture/adr-o1-minimal-runtime-refactor.md)
- [ADR O2 — Pragmatic medium runtime refactor](architecture/adr-o2-pragmatic-medium-runtime-refactor.md)
- [ADR O3 — Ideal runtime architecture](architecture/adr-o3-ideal-runtime-architecture.md)

## Methods

Method-specific docs are not yet split into `docs/methods/`. During migration, use:

- [Agent method workflow](agent/method-workflow.md)
- [Source-tree target layout (methods directory contract)](migration/target-repo-layout.md)

## Benchmarking

Benchmark docs are still being consolidated. Current references:

- [Agent benchmark workflow](agent/benchmark-workflow.md)
- [Build & troubleshooting notes](build-troubleshooting.md)

## Agent workflow

- [Agent contract](agent/contract.md)
- [Task format](agent/task-format.md)
- [Review checklist](agent/review-checklist.md)
- [Docs sync policy](agent/docs-sync-policy.md)
- [Roles](agent/roles.md)

## Migration

- [Migration docs index (in progress)](migration/)
- [Current repository inventory snapshot](migration/current-repo-inventory.md)
- [Target repository layout](migration/target-repo-layout.md)

## API / generated docs

Generated API and inventory docs are in progress. Current generated inventory location:

- [src_new module inventory](architecture/src_new_module_inventory.md)

## Build / troubleshooting

- [Build troubleshooting](build-troubleshooting.md)

## Reviews and reports

- [Reviews](reviews/)
- [Reports](reports/)
