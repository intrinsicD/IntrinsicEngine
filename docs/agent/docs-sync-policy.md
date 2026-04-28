# Docs Sync Policy

Documentation updates are required in the same PR when code/structure/policy changes.

## Required docs updates by change type

- **Architecture/layering changes**
  - Update `AGENTS.md` (if contract-level) and relevant `docs/architecture/*`.
- **Migration/path moves**
  - Update `docs/migration/*`, links, and inventories.
- **Task/process changes**
  - Update `tasks/*` records and `docs/agent/*` where process rules are affected.
- **Method or benchmark infrastructure changes**
  - Update `docs/methods/*` or `docs/benchmarking/*` plus validators and manifests.
- **CI/workflow changes**
  - Update workflow docs/process checklists.

## Quality gates

- Docs should describe current behavior/state, not aspirational plans, unless clearly labeled as roadmap/migration.
- Cross-links must be valid.
- Generated inventories should be refreshed when impacted by structure changes.
