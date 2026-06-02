# Architecture Backlog

Architecture and layering decisions, ADR-shaped tasks, and architecture
governance work. The authoritative engine contract is
[`/AGENTS.md`](../../../AGENTS.md); this directory tracks proposed or pending
decisions that may extend or refine that contract.

See [`tasks/backlog/README.md`](../README.md) for the cross-domain convergence
map.

## Tasks

- [ARCH-005 — Resolve graphics/RHI platform layering violations](../../done/ARCH-005-resolve-graphics-platform-layering-violations.md)
  (done 2026-05-17): removed the four strict-layering failures where
  promoted graphics/RHI targets imported or linked `platform` for
  window/surface inputs. Landed jointly with [`WORKSHOP-002`](../../done/WORKSHOP-002-remove-platform-window-from-rhi.md);
  `RHI::IDevice::Initialize` now takes a platform-neutral
  `RHI::DeviceCreateDesc`, `ExtrinsicRHI` no longer links
  `ExtrinsicPlatform`, and the strict layer check runs unguarded in
  `pr-fast` / `ci-linux-clang`.
- [RORG-031A — Architecture foundation backlog seed](RORG-031A-architecture-foundation.md):
  tracks architecture-doc normalization, layering-checker, docs-sync-checker,
  and module-inventory governance work.
- [RORG-036 — Layer ownership audit for misplaced concepts](RORG-036-layer-ownership-audit.md):
  inventories promoted modules whose value types, pure data contracts, or
  dependency-free APIs appear to live in higher layers than their true owner and
  creates one scoped follow-up task per accepted move/split.
- [HARDEN-069 — Rebind legacy layering allowlist entries to active retirement tasks](../../done/HARDEN-069-rebind-legacy-layering-allowlist-to-active-retirement-tasks.md)
  (done 2026-06-02): metadata-only rebinding of legacy allowlist rows from the retired `HARDEN-010`
  ID to current legacy-retirement task IDs, preserving the allowlisted edge set.
- [HARDEN-070 — Drop dead null guards on reference-initialised helpers](HARDEN-070-drop-dead-null-guards-on-reference-initialised-helpers.md):
  hygiene cleanup of ~7 internal-boundary `m_X == nullptr` guards in
  `SpatialDebugAdapters`, `TransientDebugUploadHelper`, and
  `VisualizationOverlayUploadHelper` whose constructor-reference precondition
  already makes the null branch unreachable. Filed from
  [`docs/reports/2026-05-26-agent-output-audit.md`](../../../docs/reports/2026-05-26-agent-output-audit.md)
  Row 5.
- [HARDEN-074 — Make markdown link checking see inline-code labels](../../done/HARDEN-074-doc-link-checker-inline-code-labels.md)
  (done 2026-06-02): fixed the doc-link checker blind spot where links such as
  ``[`TASK-ID`](...)`` can be skipped before validation, then cleans up the
  stale links exposed by the stricter parser.
- [HARDEN-075 — Validate task-state links and stale status claims](../../done/HARDEN-075-task-state-link-consistency-checker.md)
  (done 2026-06-02): added deterministic task lifecycle cross-reference checking so docs cannot
  keep claiming a task is active/backlog/done when the file lives elsewhere or
  no longer exists.
- [HARDEN-076 — Enforce open task owners for layering allowlist rows](HARDEN-076-enforce-open-task-layering-allowlist-owners.md):
  follows [`HARDEN-069`](../../done/HARDEN-069-rebind-legacy-layering-allowlist-to-active-retirement-tasks.md)
  by making `check_layering_allowlist_quality.py` fail strict mode when a
  temporary allowlist exception points at a missing or retired task owner.
- [HARDEN-077 — Enforce operational follow-ups for ambiguous maturity closures](HARDEN-077-enforce-operational-followups-for-ambiguous-maturity.md):
  turns the review-only "CPUContracted is not Operational" rule into a focused
  task-policy check for backend-facing graphics/Vulkan/runtime slices.
- [DOCS-001 — Reduce `docs/architecture/graphics.md` to contract + status](../../done/DOCS-001-reduce-graphics-architecture-prose.md)
  (done 2026-05-17): shrank the 793-line `graphics.md` to 118 lines by
  extracting 15 embedded decision records into ADRs `0004..0018` and adding a
  Pointers section. Slice 1 (classification), slice 2 (15 per-ADR commits
  ADR-0004 through ADR-0018), slice 3 (no-op; the only migration-inventory
  row cross-linked to the existing parity matrix via ADR-0006), and slice 4
  (final tightening + Pointers) all landed.
- [ARCH-004 — Pin first legacy-deletion target and sequencing](../../done/ARCH-004-legacy-retirement-first-deletion-target.md)
  (done 2026-05-17): pinned `src/legacy/Interface/` as the first deletion
  target, recorded `src/legacy/Asset/` and `src/legacy/EditorUI/` as the
  second and third, and added the Sequencing table to
  [`docs/migration/legacy-retirement.md`](../../../docs/migration/legacy-retirement.md)
  so retirement is a tracked program rather than an indefinite "blocked"
  row. The executing task is `LEGACY-001` below.
- [LEGACY-001 — Delete `src/legacy/Interface/`](LEGACY-001-delete-src-legacy-interface.md):
  first concrete deletion under `ARCH-004`. Backlog until the consumer-grep
  prerequisite passes; promotion to `tasks/active/` is gated by `ARCH-004`.
- [LEGACY-002 — Seed retirement tasks for remaining `src/legacy/` subtrees](LEGACY-002-seed-src-legacy-retirement-backlog.md):
  opens one structured `LEGACY-*` retirement task per remaining legacy subtree
  so allowlist entries and migration docs can point at concrete removal owners.
- [REVIEW-001 — Establish weekly human-led review of agent-authored slices](../../done/REVIEW-001-human-led-agent-week-review-cadence.md)
  (done 2026-05-17): landed `docs/agent/agent-output-review-checklist.md` with
  the nine-row failure-mode checklist, added the cadence pointer to
  `docs/agent/contract.md` and the reviewer rotation note to
  `docs/agent/roles.md`, and ran the first calibration audit on the
  GRAPHICS-033E/F + HARDEN-066 + RUNTIME-091 window
  (`docs/reports/2026-05-17-agent-output-audit.md`, ≈ 15 minutes, eight rows
  pass, one self-corrected historical finding, no new follow-up filed).
- [REVIEW-002 — Recurring repo-state drift and inconsistency audit](REVIEW-002-recurring-drift-and-inconsistency-audit.md):
  installs a whole-tree drift audit that composes existing validators and
  semantic spot-checks for inventory drift, stale task links, allowlist owner
  drift, planned-marker drift, dead seams, and naming inconsistency.

## Convergence

These tasks contribute to **Theme F — Architecture/runtime/UI foundation
seeds** in the convergence map. Layering invariants remain owned by
`AGENTS.md`; any change here that introduces a new dependency edge or source
root must update the relevant `docs/architecture/*` doc set in the same PR per
[`docs/agent/docs-sync-policy.md`](../../../docs/agent/docs-sync-policy.md).

## Related

- [`docs/architecture/index.md`](../../../docs/architecture/index.md).
- [`docs/agent/architecture-review-checklist.md`](../../../docs/agent/architecture-review-checklist.md).
