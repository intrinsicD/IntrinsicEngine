# Legacy Retirement Plan (`src/legacy/`)

## Purpose

`src/legacy/` is a temporary containment area for historical subsystems that have not yet been promoted into canonical final roots (`src/core`, `src/geometry`, `src/graphics/*`, etc.).

## Policy

- New feature work should target canonical layers, not `src/legacy/`, unless needed for compatibility.
- Any temporary cross-layer exception inside `src/legacy/` must be tracked in a current task under `tasks/active/` with a removal task ID.
- Promotion work from `src/legacy/` must keep mechanical path moves separate from semantic refactors.

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

Retirement runs as a tracked program. Targets are deleted in the order below, each as a single mechanical commit, gated by an explicit prerequisite checklist. New targets are added by [`ARCH-004 — Pin first legacy-deletion target and sequencing`](../../tasks/backlog/architecture/ARCH-004-legacy-retirement-first-deletion-target.md); the executing tasks live alongside it under `tasks/backlog/architecture/LEGACY-NNN-*.md`.

| # | Subtree | Size | Promoted owner | Prerequisite checklist | Executing task |
|---|---|---|---|---|---|
| 1 | `src/legacy/Interface/` | 3 files | `src/platform/` (window/input ports), `src/app/Sandbox/` (UI shell entry) | (a) `git grep` shows no `import Interface` / `Interface::GUI` / `#include "Interface` in `src/**` or `tests/**` outside `src/legacy/Interface/`; (b) legacy Sandbox builds without it, or is fenced behind a build flag; (c) layering allowlist row count drops by exactly the count of removed rows. | [`LEGACY-001`](../../tasks/backlog/architecture/LEGACY-001-delete-src-legacy-interface.md) |
| 2 | `src/legacy/Asset/` | 5 files | `src/assets/` | (a) legacy Sandbox + legacy `Runtime/AssetIngestService` migrated off the legacy asset surface; (b) parity matrix `assets` row no longer lists legacy `Asset.Manager` / `Asset.Pipeline` / `Asset.Errors` as live; (c) consumer grep clean. | `LEGACY-002` (to be authored when prerequisite (a) is satisfied). |
| 3 | `src/legacy/EditorUI/` | 7 files | `src/app/` (UI shell, scaffold-only today) | (a) Promoted editor entry exists under `src/app/`; (b) Sandbox no longer imports legacy editor controllers/widgets; (c) consumer grep clean. | `LEGACY-003` (to be authored once `src/app/` UI shell is non-scaffold). |

The largest subtrees (`src/legacy/Graphics/` 167 files, `src/legacy/RHI/` 53 files, `src/legacy/Core/` 39 files, `src/legacy/ECS/` 28 files, `src/legacy/Runtime/` 27 files) are deliberately not on this initial list. Their retirement is gated on the GRAPHICS-033 + GRAPHICS-070..076 + GRAPHICS-081 chain (promoted Vulkan operational + canonical recipe shipping) plus `ASSETIO-001` / `GRAPHICS-023` (asset/material/shader hot-reload). They will be added to this sequencing table when their upstream gates retire and a `LEGACY-NNN-delete-src-legacy-<area>` executing task is authored.

Process rule: a sequencing row is satisfied only when its prerequisite checklist is fully checked off by the agent promoting the executing task. Bypassing the checklist is forbidden by [`AGENTS.md`](../../AGENTS.md) §13.
