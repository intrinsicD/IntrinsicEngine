# Legacy Retirement Plan (`src/legacy/`)

## Purpose

`src/legacy/` is a temporary containment area for historical subsystems that have not yet been promoted into canonical final roots (`src/core`, `src/geometry`, `src/graphics/*`, etc.).

## Policy

- New feature work should target canonical layers, not `src/legacy/`, unless needed for compatibility.
- Any temporary cross-layer exception inside `src/legacy/` must be tracked in a current task under `tasks/active/` with a removal task ID.
- Layering allowlist rows must point at an open removal owner. As of `HARDEN-082`, `src/legacy/Interface/**` rows point at [`LEGACY-001`](../../tasks/backlog/architecture/LEGACY-001-delete-src-legacy-interface.md), and the remaining legacy subtree rows point at their specific per-subtree deletion tasks (`LEGACY-004`..`LEGACY-006`, `LEGACY-008`..`LEGACY-010`).
- Promotion work from `src/legacy/` must keep mechanical path moves separate from semantic refactors.
- Semantic reimplementation blockers were retired by
  [`LEGACY-011`](../../tasks/done/LEGACY-011-src-legacy-feature-reimplementation-map.md)
  and its child tasks: candidates were retained only when necessary or when they
  improved the current promoted architecture; otherwise they were deferred or
  retired. The per-subtree `LEGACY-*` deletion tasks remain mechanical and
  should not grow feature work.
- The current per-subtree gate state — which subtrees are blocked only by test
  consumers vs. by promoted engine code — is captured in the dated
  [`legacy-removal-audit.md`](legacy-removal-audit.md) snapshot.

## Exit criteria

`src/legacy/` can be considered retired only when all are true:

1. Canonical implementations exist and build under final roots.
2. CI no longer depends on legacy include paths.
3. Layering checks pass with no undocumented legacy exceptions.
4. Migration docs referencing active legacy shims are closed or archived.

## Cleanup expectations

When a legacy area is retired:

- Remove now-unused compatibility wrappers.
- Update architecture docs and inventory outputs.
- Mark related migration tasks as done with commit/PR references.

## Sequencing

Retirement runs as a tracked program. Targets are deleted in the order below, each as a single mechanical commit, gated by an explicit prerequisite checklist. The first/second/third deletion targets were pinned by [`ARCH-004 — Pin first legacy-deletion target and sequencing`](../../tasks/done/ARCH-004-legacy-retirement-first-deletion-target.md) (done 2026-05-17); new targets are added by a follow-up `ARCH-NNN` task once the upstream gates for the next subtree retire. The executing tasks live under `tasks/backlog/architecture/LEGACY-NNN-*.md`.

| # | Subtree | Size | Promoted owner | Prerequisite checklist | Executing task |
|---|---|---|---|---|---|
| 1 | `src/legacy/Interface/` | 3 files | `src/platform/` (window/input ports), `src/app/Sandbox/` (UI shell entry) | (a) consumer-grep gate in `LEGACY-001` Verification exits 0 with `OK: no external consumers ...` (the gate inverts `git grep`'s match-exits-0 default and excludes `src/legacy/Interface/**` from the search); the gate still fails across remaining legacy graphics/editor/runtime consumers and `tests/contract/ui/`; (b) remaining legacy modules build without it, or are fenced behind a build flag; (c) layering allowlist row count drops by exactly the count of removed rows. | [`LEGACY-001`](../../tasks/backlog/architecture/LEGACY-001-delete-src-legacy-interface.md) |
| 2 | `src/legacy/Asset/` | 5 files | `src/assets/` | (a) legacy `Runtime/AssetIngestService` and remaining legacy graphics/runtime consumers migrate off the legacy asset surface; (b) parity matrix `assets` row no longer lists legacy `Asset.Manager` / `Asset.Pipeline` / `Asset.Errors` as live; (c) consumer grep clean. | [`LEGACY-004`](../../tasks/backlog/architecture/LEGACY-004-delete-src-legacy-asset.md) |
| 3 | `src/legacy/EditorUI/` | 8 files | `src/runtime/Editor/` + `src/app/Sandbox/` attachment | Done 2026-06-07: promoted `SandboxEditorUi` shell/domain windows, geometry-processing discovery, file/scene command surfaces, and the `LEGACY-003` Sandbox-retirement prerequisite were in place; consumer grep exited clean before deletion. Follow-up UI-006 later restored Frame Graph diagnostics against renderer-owned stats. | [`LEGACY-007`](../../tasks/done/LEGACY-007-delete-src-legacy-editorui.md) |

As of `LEGACY-002` (2026-06-06), every remaining `src/legacy/<Subsystem>/` subtree now has a seeded per-subtree executing task in `tasks/backlog/architecture/`, each carrying its own consumer-grep prerequisite gate; they stay in backlog until those gates exit 0. In addition to rows 2–3 above:

- [`LEGACY-003`](../../tasks/done/LEGACY-003-delete-src-legacy-apps.md) (done 2026-06-07) — `src/legacy/Apps/` legacy Sandbox binary retired as a pure leaf consumer after `ExtrinsicSandbox` became canonical.
- [`LEGACY-007`](../../tasks/done/LEGACY-007-delete-src-legacy-editorui.md) (done 2026-06-07) — `src/legacy/EditorUI/` retired after promoted `SandboxEditorUi` coverage replaced the legacy editor surface; UI-006 has since restored the Frame Graph diagnostics panel against renderer-owned stats.
- [`LEGACY-005`](../../tasks/backlog/architecture/LEGACY-005-delete-src-legacy-core.md) — `src/legacy/Core/` (foundation; retires last).
  `LEGACY-013` cleared the promoted-src bare `Core.*` import subset; remaining
  blockers are `LEGACY-012` test consumers and legacy-internal consumers that
  retire through subtree ordering.
- [`LEGACY-006`](../../tasks/backlog/architecture/LEGACY-006-delete-src-legacy-ecs.md) — `src/legacy/ECS/`.
- [`LEGACY-008`](../../tasks/backlog/architecture/LEGACY-008-delete-src-legacy-graphics.md) — `src/legacy/Graphics/` (largest subtree; gated on the GRAPHICS-033 + GRAPHICS-070..076 + GRAPHICS-081 chain — now retired — plus migration of the remaining legacy consumers).
- [`LEGACY-009`](../../tasks/backlog/architecture/LEGACY-009-delete-src-legacy-rhi.md) — `src/legacy/RHI/`.
  `GRAPHICS-086` retired the command-helper, persistent-descriptor,
  swapchain/image, scene-instance, and CUDA decision blockers; remaining
  prerequisites are consumer-grep/subtree ordering and any independently named
  Vulkan operational evidence.
- [`LEGACY-010`](../../tasks/backlog/architecture/LEGACY-010-delete-src-legacy-runtime.md) — `src/legacy/Runtime/`.
- [`LEGACY-011`](../../tasks/done/LEGACY-011-src-legacy-feature-reimplementation-map.md)
  (done 2026-06-18) — value-gated feature-reimplementation map retired after
  every remaining candidate gained a retained/deferred/retired outcome. Open
  deletion tasks are now blocked by consumer-grep cleanup, promoted-src Core
  import cleanup, and mechanical subtree ordering rather than unnamed feature
  gaps.
- [`CORE-002`](../../tasks/done/CORE-002-command-feature-catalog-contract.md)
  is retired: legacy command/feature catalogs are not promoted as a global
  service. Retained dependency-free core utility/telemetry seams use
  `Extrinsic.Core.*`; runtime/editor command history is owned by `RUNTIME-102`.
  Remaining `src/legacy/Core/` deletion blockers are consumer-grep cleanup and
  legacy-only compatibility tests, not unnamed command/catalog feature gaps.
- [`LEGACY-012`](../../tasks/backlog/architecture/LEGACY-012-migrate-legacy-consumer-tests.md) —
  migrates or retires tests and other non-legacy consumers that still import
  bare legacy module names after promoted feature owners exist.

Deletion order is consumer-leaves first, foundation last (see the "Legacy retirement" section of [`tasks/backlog/architecture/README.md`](../../tasks/backlog/architecture/README.md) for the full ordering rationale). Current `find -type f` subtree sizes (including each `CMakeLists.txt`): `Graphics` 168, `RHI` 54, `Core` 40, `ECS` 29, `Runtime` 29, `Asset` 6.

Process rule: a sequencing row is satisfied only when its prerequisite checklist is fully checked off by the agent promoting the executing task. Bypassing the checklist is forbidden by [`AGENTS.md`](../../AGENTS.md) §13.
