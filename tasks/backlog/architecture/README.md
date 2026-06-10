# Architecture Backlog

Architecture and layering decisions, ADR-shaped tasks, and architecture
governance work. The authoritative engine contract is
[`/AGENTS.md`](../../../AGENTS.md); this directory tracks proposed or pending
decisions that may extend or refine that contract.

See [`tasks/backlog/README.md`](../README.md) for the cross-domain convergence
map.

## Tasks

- [LEGACY-011 — Value-gated legacy feature reimplementation map](LEGACY-011-src-legacy-feature-reimplementation-map.md):
  cross-domain child-task map that compares current promoted behavior,
  improvement, and retain/defer/retire decisions before the remaining
  `src/legacy/` subtree deletion tasks become purely mechanical.
- [LEGACY-012 — Migrate legacy consumer tests to promoted coverage](LEGACY-012-migrate-legacy-consumer-tests.md):
  migrates or retires tests and non-legacy consumers that still import bare
  legacy module names after their promoted feature owners exist.
- [RORG-031A — Architecture foundation backlog seed](RORG-031A-architecture-foundation.md):
  tracks architecture-doc normalization, layering-checker, docs-sync-checker,
  and module-inventory governance work.
- [LEGACY-001 — Delete `src/legacy/Interface/`](LEGACY-001-delete-src-legacy-interface.md):
  first concrete deletion under `ARCH-004`. Backlog until the consumer-grep
  prerequisite passes; promotion to `tasks/active/` is gated by `ARCH-004`.
- [LEGACY-002 — Seed retirement tasks for remaining `src/legacy/` subtrees](LEGACY-002-seed-src-legacy-retirement-backlog.md):
  opens one structured `LEGACY-*` retirement task per remaining legacy subtree
  so allowlist entries and migration docs can point at concrete removal owners.
- [HARDEN-078 — Track or resolve untracked TODO / temporary markers in promoted src](HARDEN-078-track-untracked-todo-temporary-markers.md):
  drift-audit Row 7 follow-up — gives the `Core.Filesystem` TODO and the
  `Runtime.Engine::GetStreamingGraph()` temporary bridge a tracked owner per
  `AGENTS.md` §13.

## Legacy retirement

`LEGACY-002` seeds one structured deletion task per remaining
`src/legacy/<Subsystem>/` subtree so the layering allowlist and migration docs
can name a concrete owning task per subtree. Each task follows the
[`LEGACY-001`](LEGACY-001-delete-src-legacy-interface.md) shape (scope,
consumer-grep prerequisite gate, mechanical-deletion verification) and stays in
backlog until its gate exits 0.

- [LEGACY-004 — Delete `src/legacy/Asset/`](LEGACY-004-delete-src-legacy-asset.md) (6 files → `src/assets/`).
- [LEGACY-005 — Delete `src/legacy/Core/`](LEGACY-005-delete-src-legacy-core.md) (40 files → `src/core/`).
- [LEGACY-006 — Delete `src/legacy/ECS/`](LEGACY-006-delete-src-legacy-ecs.md) (29 files → `src/ecs/`).
- [LEGACY-008 — Delete `src/legacy/Graphics/`](LEGACY-008-delete-src-legacy-graphics.md) (168 files → `src/graphics/*`).
- [LEGACY-009 — Delete `src/legacy/RHI/`](LEGACY-009-delete-src-legacy-rhi.md) (54 files → `src/graphics/rhi/`).
- [LEGACY-010 — Delete `src/legacy/Runtime/`](LEGACY-010-delete-src-legacy-runtime.md) (29 files → `src/runtime/`).
- [LEGACY-011 — Value-gated legacy feature reimplementation map](LEGACY-011-src-legacy-feature-reimplementation-map.md):
  child-task inventory for retained/deferred feature candidates that block the
  remaining deletion tasks from becoming pure consumer-grep/mechanical removals.
- [LEGACY-012 — Migrate legacy consumer tests to promoted coverage](LEGACY-012-migrate-legacy-consumer-tests.md):
  cleanup task for tests and non-legacy consumers that keep the deletion
  consumer-grep gates red after semantic replacements land.

Ordering hint — deletion is **consumer-leaves first, foundation last**: a
subtree can only be mechanically deleted once no other subtree (and no promoted
module) still imports it. `Apps` (`LEGACY-003`) and `EditorUI`
(`LEGACY-007`) are retired; suggested remaining order after `LEGACY-001`:
`Runtime` (`LEGACY-010`) → `Graphics` (`LEGACY-008`) → `Asset` (`LEGACY-004`) → `RHI`
(`LEGACY-009`) → `ECS` (`LEGACY-006`) → `Core` (`LEGACY-005`, the foundation,
still imported by promoted `geometry`/`runtime`, so it retires last). The
`sequencing` table in
[`docs/migration/legacy-retirement.md`](../../../docs/migration/legacy-retirement.md)
tracks the per-subtree prerequisite checklists.

## Convergence

These tasks contribute to **Theme F — Architecture/runtime/UI foundation
seeds** in the convergence map. Layering invariants remain owned by
`AGENTS.md`; any change here that introduces a new dependency edge or source
root must update the relevant `docs/architecture/*` doc set in the same PR per
[`docs/agent/docs-sync-policy.md`](../../../docs/agent/docs-sync-policy.md).

## Related

- [`docs/architecture/index.md`](../../../docs/architecture/index.md).
- [`docs/agent/architecture-review-checklist.md`](../../../docs/agent/architecture-review-checklist.md).

## Retired

Retired entries moved here verbatim by the PROC-008 state/history
split; narratives live in the retirement log.

- [CORE-002 — Command and feature catalog contract](../../done/CORE-002-command-feature-catalog-contract.md)
  (done, 2026-06-09, `CPUContracted` / explicit retirement decision):
  resolves remaining legacy `Core.Commands`, `Core.FeatureRegistry`, and
  `Core.SystemFeatureCatalog` behavior as promoted narrow contracts, runtime/UI
  follow-ups, or explicit retirements.
- [ARCH-005 — Resolve graphics/RHI platform layering violations](../../done/ARCH-005-resolve-graphics-platform-layering-violations.md)
  (done 2026-05-17): removed the four strict-layering failures where
  promoted graphics/RHI targets imported or linked `platform` for
  window/surface inputs. Landed jointly with [`WORKSHOP-002`](../../done/WORKSHOP-002-remove-platform-window-from-rhi.md);
  `RHI::IDevice::Initialize` now takes a platform-neutral
  `RHI::DeviceCreateDesc`, `ExtrinsicRHI` no longer links
  `ExtrinsicPlatform`, and the strict layer check runs unguarded in
  `pr-fast` / `ci-linux-clang`.
- [RORG-036 — Layer ownership audit for misplaced concepts](../../done/RORG-036-layer-ownership-audit.md)
  (done 2026-06-06): whole-tree audit of promoted module placement against the
  `/AGENTS.md` §2 table. Outcome was a **clean baseline** — `RORG-034`/`RORG-035`
  already moved the previously-misplaced frame-loop and extent/rect value types
  to `core`, and no remaining promoted module imports a higher/sibling layer for
  a value-type-only reason. Examined the named candidates
  (`Runtime.StreamingExecutor`, the geometry/procedural packers, the
  dependency-free `Runtime.RenderWorldPool`) and decided each stays; zero moves
  accepted, so no follow-up move/split tasks were filed. `RenderWorldPool` is
  recorded in the task as a latent `core`-split candidate to revisit on a second
  consumer.
- [HARDEN-069 — Rebind legacy layering allowlist entries to active retirement tasks](../../done/HARDEN-069-rebind-legacy-layering-allowlist-to-active-retirement-tasks.md)
  (done 2026-06-02): metadata-only rebinding of legacy allowlist rows from the retired `HARDEN-010`
  ID to current legacy-retirement task IDs, preserving the allowlisted edge set.
- [HARDEN-070 — Drop dead null guards on reference-initialised helpers](../../done/HARDEN-070-drop-dead-null-guards-on-reference-initialised-helpers.md)
  (done 2026-06-06): hygiene cleanup of the nine internal-boundary
  `m_X == nullptr` guards in `SpatialDebugAdapters` (4),
  `TransientDebugUploadHelper` (3), and `VisualizationOverlayUploadHelper` (2)
  whose constructor-reference precondition already makes the null branch
  unreachable; replaced with one-line lifetime-contract notes in each `.cppm`.
  Filed from
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
- [HARDEN-076 — Enforce open task owners for layering allowlist rows](../../done/HARDEN-076-enforce-open-task-layering-allowlist-owners.md)
  (done 2026-06-02):
  follows [`HARDEN-069`](../../done/HARDEN-069-rebind-legacy-layering-allowlist-to-active-retirement-tasks.md)
  by making `check_layering_allowlist_quality.py` fail strict mode when a
  temporary allowlist exception points at a missing or retired task owner.
- [HARDEN-077 — Enforce operational follow-ups for ambiguous maturity closures](../../done/HARDEN-077-enforce-operational-followups-for-ambiguous-maturity.md)
  (done 2026-06-02):
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
- [REVIEW-001 — Establish weekly human-led review of agent-authored slices](../../done/REVIEW-001-human-led-agent-week-review-cadence.md)
  (done 2026-05-17): landed `docs/agent/agent-output-review-checklist.md` with
  the nine-row failure-mode checklist, added the cadence pointer to
  `docs/agent/contract.md` and the reviewer rotation note to
  `docs/agent/roles.md`, and ran the first calibration audit on the
  GRAPHICS-033E/F + HARDEN-066 + RUNTIME-091 window
  (`docs/reports/2026-05-17-agent-output-audit.md`, ≈ 15 minutes, eight rows
  pass, one self-corrected historical finding, no new follow-up filed).
- [REVIEW-002 — Recurring repo-state drift and inconsistency audit](../../done/REVIEW-002-recurring-drift-and-inconsistency-audit.md)
  (done 2026-06-06): installed the whole-tree drift audit
  ([`docs/agent/drift-audit-checklist.md`](../../../docs/agent/drift-audit-checklist.md))
  composing existing validators and semantic spot-checks for inventory drift,
  stale task links, allowlist owner drift, planned-marker drift, dead seams, and
  naming inconsistency, and ran the first calibration
  ([`docs/reports/2026-06-06-drift-audit.md`](../../../docs/reports/2026-06-06-drift-audit.md),
  ≈ 20 min). Eight rows clean; Row 7 (untracked TODO/temporary markers) filed
  the follow-up `HARDEN-078`.
- [HARDEN-079 — Core module implementation splits](../../done/HARDEN-079-core-module-implementation-splits.md):
  module-interface hygiene follow-up for promoted `src/core/*.cppm` targets
  found by the 2026-06-06 implementation-body audit, including
  `Core.FrameGraph.cppm` and other core interfaces with non-trivial bodies that
  should live in matching `.cpp` implementation units.
- [LEGACY-003 — Delete `src/legacy/Apps/`](../../done/LEGACY-003-delete-src-legacy-apps.md)
  (done 2026-06-07): removed the legacy Sandbox leaf binary and its CMake
  wiring after `ExtrinsicSandbox` became canonical.
- [LEGACY-007 — Delete `src/legacy/EditorUI/`](../../done/LEGACY-007-delete-src-legacy-editorui.md)
  (done 2026-06-07): removed the legacy editor UI subtree after promoted
  `SandboxEditorUi` coverage replaced its remaining consumers.
