# ARCH-004 — Pin first legacy-deletion target and sequencing

## Status

- Status: done.
- Owner/agent: Claude on `claude/backlog-task-agent-prompt-xYDKf`.
- Branch: `claude/backlog-task-agent-prompt-xYDKf`.
- Completed: 2026-05-17.
- PR: pending (this retirement slice on `claude/backlog-task-agent-prompt-xYDKf`). Commit reference: backfilled on push.
- Landed slice: the planning artifacts called for by this task already
  exist on `main` from prior PRs that authored the first-deletion target
  (`src/legacy/Interface/`), the second/third targets (`src/legacy/Asset/`,
  `src/legacy/EditorUI/`), the `LEGACY-001` executing task, the Sequencing
  table in [`docs/migration/legacy-retirement.md`](../../docs/migration/legacy-retirement.md),
  and the program-driver back-reference from
  [`docs/migration/nonlegacy-parity-matrix.md`](../../docs/migration/nonlegacy-parity-matrix.md)'s
  `GRAPHICS-020` section. This retirement commit only audits the
  acceptance criteria (all satisfied), files the completion record, and
  refreshes the cross-links to point at the `tasks/done/` location so the
  retirement program continues to be reachable from the architecture
  backlog index and the migration docs.

## Goal
- [x] Pick the first `src/legacy/<area>/` subtree that will be deleted (not just stop being depended on, but actually removed from the repository) and record the precise prerequisite checklist that authorizes the deletion.
- [x] Record the second and third deletion targets in order, so that once the first lands, the next agent picking up legacy retirement has an unambiguous, dependency-ordered queue.
- [x] Make legacy retirement a tracked program rather than an indefinite "blocked" row on the parity matrix.

## Non-goals
- Do not delete any code in this task. This is planning only.
- Do not modify `src/legacy/*` source.
- Do not rewrite [`docs/migration/legacy-retirement.md`](../../docs/migration/legacy-retirement.md) policy; only extend its sequencing section.
- Do not promote the first deletion to `tasks/active/` from this task. Each deletion is a separate `LEGACY-NNN` task created at the appropriate time.
- Do not pick the largest legacy areas (`src/legacy/Graphics/` at 167 files, `src/legacy/RHI/` at 53, `src/legacy/Core/` at 39, `src/legacy/ECS/` at 28, `src/legacy/Runtime/` at 27) as the first deletion. The first deletion must be a confidence-building exercise, not a stress test.

## Context
- Owning subsystem/layer: cross-cutting (`tasks/`, `docs/migration/`, `tools/repo/`).
- Today's state (2026-05-16):
  - `src/legacy/` contains 330 source files / ~75 k LOC / 182 modules — roughly half of all engine source.
  - The `GRAPHICS-020` legacy graphics retirement gate matrix in [`docs/migration/nonlegacy-parity-matrix.md`](../../docs/migration/nonlegacy-parity-matrix.md) reports `blocked` or `partial/open` for every row.
  - [`docs/migration/legacy-retirement.md`](../../docs/migration/legacy-retirement.md) documents the policy but does not pin a *first* concrete deletion target or sequencing.
  - The layering allowlist (`tools/repo/layering_allowlist.yaml`) carries 1233 grandfathered violations whose elimination is blocked by legacy deletion.
- Legacy subdirectory sizes (file count) as of this writing:
  - `Apps`: 1 — too small to be representative.
  - `Interface`: 3 — small, candidate.
  - `Asset`: 5 — small, candidate.
  - `EditorUI`: 7 — small, candidate.
  - `ECS`: 28.
  - `Runtime`: 27.
  - `Core`: 39.
  - `RHI`: 53.
  - `Graphics`: 167 — the bulk of the unmigrated mass; gated on the GRAPHICS-033 + GRAPHICS-070..076 + GRAPHICS-081 chain.
- Candidate first-deletion subtrees (small enough to retire as a confidence-builder, but large enough that the deletion produces real signal):
  - `src/legacy/Interface/` (3 files): `Gui.{cpp,cppm}` + `Interface.cppm`. Owned today by app-level legacy entry points. Promoted owner: `src/platform/` (window/input ports) + `src/app/` (UI shell). Retirement requires:
    1. Confirm no consumers in `src/runtime/`, `src/graphics/*`, or `src/legacy/Apps/` import `Interface` or `Interface::GUI` modules.
    2. Either port the legacy `Apps/Sandbox` to the promoted `Extrinsic.Sandbox` entry point, or fence the legacy entry behind a build flag and stop building it by default.
    3. Layering allowlist drops the `Interface` rows.
  - `src/legacy/Asset/` (5 files): legacy asset stubs. Promoted owner: `src/assets/`. The non-legacy parity matrix already lists `Extrinsic.Asset.*` modules as superseding legacy `Asset.Manager`, `Asset.Pipeline`, `Asset.Errors`. Retirement requires:
    1. Migration of the legacy `Apps/Sandbox` and legacy `Runtime/AssetIngestService` consumers to the promoted asset service.
    2. `ASSETIO-001` not strictly required, because the legacy `Asset` subtree is the registry/service surface, not the file-format importers (which live under `Graphics`).
  - `src/legacy/EditorUI/` (7 files): editor panel controllers + widgets. Promoted owner: `src/app/` (UI shell, scaffold-only today). This is *larger surface but lower runtime risk* because the editor is not on any other layer's critical path.
- Constraints from `AGENTS.md`:
  - §13 Temporary migration exceptions: deletion must not re-create undocumented exceptions.
  - §10 CI: structural checks must remain green for touched scope.
  - §12 Review checklist: mechanical moves/deletions must not be mixed with semantic edits.

## Required changes
- [x] Decide the first deletion target. Recommendation in this task: **`src/legacy/Interface/`** as the first target, because:
  - Smallest meaningful surface (3 files).
  - Has obvious promoted-stack equivalent (`src/platform/` for window/input, `src/app/Sandbox/` for UI shell entry).
  - Deletion does not require the promoted Vulkan path to be operational; it only requires the legacy Sandbox to stop importing it.
  - Worst case if the deletion is reverted is small and isolated.
- [x] Record the chosen first target, the prerequisite checklist (specific consumers that must migrate, specific layering allowlist rows that must be removed), and the precise CI/test commands that authorize the deletion, in [`docs/migration/legacy-retirement.md`](../../docs/migration/legacy-retirement.md) under a new "Sequencing" section that this task adds.
- [x] Record second and third candidates in priority order (recommendation: `src/legacy/Asset/`, then `src/legacy/EditorUI/`) with their own short prerequisite checklists.
- [x] Open a follow-up `LEGACY-001-delete-src-legacy-interface.md` task in `tasks/backlog/architecture/` that references this sequencing decision and contains the deletion contract (mechanical removal + allowlist drop + CI verification commands). Do not promote it to `tasks/active/` from this task; that promotion happens only when the prerequisites in `docs/migration/legacy-retirement.md` are checked off.
- [x] Cross-link this task from [`tasks/backlog/architecture/README.md`](../backlog/architecture/README.md).
- [x] Cross-link this task from the `GRAPHICS-020` row of [`docs/migration/nonlegacy-parity-matrix.md`](../../docs/migration/nonlegacy-parity-matrix.md) as the program-level retirement driver.

## Tests
- [x] No code is produced by this task. No automated tests.
- [x] Manual verification: after the planning edits land, `python3 tools/agents/check_task_policy.py --root . --strict` and `python3 tools/docs/check_doc_links.py --root .` both pass.

## Docs
- [x] [`docs/migration/legacy-retirement.md`](../../docs/migration/legacy-retirement.md) gains a Sequencing section listing first/second/third targets with prerequisite checklists and CI commands.
- [x] [`tasks/backlog/architecture/README.md`](../backlog/architecture/README.md) lists this task.
- [x] [`docs/migration/nonlegacy-parity-matrix.md`](../../docs/migration/nonlegacy-parity-matrix.md) `GRAPHICS-020` row carries a back-reference to this task as the retirement program driver.
- [x] The new `LEGACY-001-delete-src-legacy-interface.md` task file exists in backlog with its deletion contract and reads explicitly that it is gated on this task's Sequencing checklist being satisfied.

## Acceptance criteria
- [x] `tasks/backlog/architecture/ARCH-004-legacy-retirement-first-deletion-target.md` exists in backlog. (Retired to `tasks/done/ARCH-004-legacy-retirement-first-deletion-target.md` on 2026-05-17; the planning content is unchanged.)
- [x] `tasks/backlog/architecture/LEGACY-001-delete-src-legacy-interface.md` exists in backlog and references this task.
- [x] `docs/migration/legacy-retirement.md` carries a Sequencing section with at least one target (the first deletion).
- [x] Strict task-policy and docs-links validators pass.
- [x] No actual `src/legacy/` deletion has occurred in this task's commit.
- [x] Promotion rule recorded here: when the prerequisites for `src/legacy/Interface/` deletion are satisfied (no consumers remain, allowlist rows are unused, sandbox builds without the subtree), the next agent moves `LEGACY-001-delete-src-legacy-interface.md` from backlog to `tasks/active/` and executes the deletion as a single mechanical commit.

## Verification
```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Deleting any `src/legacy/` code under cover of this task.
- Promoting `LEGACY-001` to `tasks/active/` from this task.
- Picking a deletion target that requires the GRAPHICS-033 promoted-Vulkan path to be operational. The first-deletion goal is to prove the retirement program can ship, not to depend on the longest-pole gate.
- Bundling multiple legacy subtree deletions into one promotion of `LEGACY-001`.
