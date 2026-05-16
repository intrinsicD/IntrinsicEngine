# Architecture Backlog

Architecture and layering decisions, ADR-shaped tasks, and architecture
governance work. The authoritative engine contract is
[`/AGENTS.md`](../../../AGENTS.md); this directory tracks proposed or pending
decisions that may extend or refine that contract.

See [`tasks/backlog/README.md`](../README.md) for the cross-domain convergence
map.

## Tasks

- [RORG-031A — Architecture foundation backlog seed](RORG-031A-architecture-foundation.md):
  tracks architecture-doc normalization, layering-checker, docs-sync-checker,
  and module-inventory governance work.
- [RORG-036 — Layer ownership audit for misplaced concepts](RORG-036-layer-ownership-audit.md):
  inventories promoted modules whose value types, pure data contracts, or
  dependency-free APIs appear to live in higher layers than their true owner and
  creates one scoped follow-up task per accepted move/split.
- [DOCS-001 — Reduce `docs/architecture/graphics.md` to contract + status](DOCS-001-reduce-graphics-architecture-prose.md):
  shrink the 793-line `graphics.md` to ≤ 250 lines by extracting embedded
  decision records into ADRs and migration inventories, leaving only the
  canonical contract behind. Sliced for incremental landing.
- [ARCH-004 — Pin first legacy-deletion target and sequencing](ARCH-004-legacy-retirement-first-deletion-target.md):
  picks the first `src/legacy/<area>/` subtree to actually delete (recommendation:
  `src/legacy/Interface/`), records second and third targets in dependency order,
  and adds a Sequencing section to `docs/migration/legacy-retirement.md` so
  retirement becomes a tracked program rather than an indefinite "blocked" row.
- [LEGACY-001 — Delete `src/legacy/Interface/`](LEGACY-001-delete-src-legacy-interface.md):
  first concrete deletion under `ARCH-004`. Backlog until the consumer-grep
  prerequisite passes; promotion to `tasks/active/` is gated by `ARCH-004`.
- [REVIEW-001 — Establish weekly human-led review of agent-authored slices](REVIEW-001-human-led-agent-week-review-cadence.md):
  adds a low-overhead weekly audit checklist (`docs/agent/agent-output-review-checklist.md`)
  covering nine agent-specific failure modes (scope creep, decorative comments,
  premature abstraction, documented-but-not-tested, etc.) plus a first
  calibration audit of the GRAPHICS-033E/F + HARDEN-066 + RUNTIME-091 window.

## Convergence

These tasks contribute to **Theme F — Architecture/runtime/UI foundation
seeds** in the convergence map. Layering invariants remain owned by
`AGENTS.md`; any change here that introduces a new dependency edge or source
root must update the relevant `docs/architecture/*` doc set in the same PR per
[`docs/agent/docs-sync-policy.md`](../../../docs/agent/docs-sync-policy.md).

## Related

- [`docs/architecture/index.md`](../../../docs/architecture/index.md).
- [`docs/agent/architecture-review-checklist.md`](../../../docs/agent/architecture-review-checklist.md).
