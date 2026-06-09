# RUNTIME-102 — Editor command history and undo/redo seam

## Goal
- Promote editor command history, undo/redo, recursive delete/orphan policy, and dirty-state tracking as runtime/editor services instead of legacy `Core.Commands` or ad hoc UI mutation.

## Non-goals
- No generic command buffer inside `src/ecs`.
- No ImGui-specific types in public runtime command DTOs.
- No serialization of renderer/RHI state.
- No legacy `Core.Commands` compatibility wrapper.

## Context
- Owner/layer: `runtime` editor services, consumed by `Runtime.SandboxEditorUi` and app-level UI hooks.
- Legacy `Core.Commands` used type-erased undo/redo callbacks and component change helpers. Under the new architecture, command interpretation and undo/redo belong above ECS because runtime/editor own selection policy, recursive delete/orphan policy, asset/materialization commands, and UI command routing.
- Reuse `SelectionController`, `StableEntityLookup`, `SceneSerialization`, `ECS` typed mutation primitives, and current SandboxEditor command DTOs.

## Value gate
- Current state: current editor commands exist but lack a runtime-owned history, dirty-state source of truth, and rollback semantics.
- Improvement: undo/redo and dirty state become deterministic runtime behavior without pushing command buffers into `core`, ECS, graphics, or UI.
- Scope decision: retain command history only for current promoted editor/runtime commands. Do not add a generic plugin command bus or legacy type-erased command catalog.

## Required changes
- [x] Define a promoted `EditorCommandHistory` or equivalent runtime-owned service with capacity, execute/record/undo/redo/clear, labels, and deterministic failure statuses.
- [x] Add command adapters for local transform edits, selection changes, primitive-view toggles, visualization config/property edits, spatial-debug bindings, scene create/delete/duplicate, and asset import materialization where feasible.
- [x] Define recursive delete/orphan policy for hierarchy mutations and sidecar cleanup.
- [x] Add dirty-state tracking for scene documents and UI affordances without making UI authoritative.
- [x] Keep command payloads data-only and stable-ID based where possible.

## Tests
- [x] Add `contract;runtime` tests for history capacity, execute/record/undo/redo/clear, stale entity rejection, and compound command rollback order.
- [x] Add tests for transform, selection, hierarchy delete/orphan, visualization, and import-related command cases selected by the implementation slice.
- [x] Add UI model tests proving dirty-state and undo/redo availability are reported without UI owning state.

## Docs
- [x] Update `src/runtime/README.md` and `docs/migration/nonlegacy-parity-matrix.md`.
- [x] Update `tasks/backlog/ui/README.md` if SandboxEditorUi command surfaces change.
- [x] Regenerate module inventory if public module surfaces change.

## Acceptance criteria
- [x] Runtime owns a deterministic command history service covering current editor commands.
- [x] Undo/redo uses ECS/runtime ownership rules and rejects stale entities without undefined behavior.
- [x] Dirty-state UX has a tested runtime source of truth.

## Status
- Completed 2026-06-09 at maturity `CPUContracted`.
- PR/commit: this retirement commit.
- Added `Extrinsic.Runtime.EditorCommandHistory` with deterministic status/result/snapshot DTOs, bounded undo/redo capacity, active-path and dirty revision tracking, typed adapters for transform edits, single-selection replacement, mesh primitive-view settings, visualization configs, spatial-debug bindings, compound rollback, and hierarchy delete/orphan planning.
- Wired `Engine` to expose the history service, mark scene-changing imports dirty, mark successful saves clean with an active path, and reset history on successful scene loads.
- Wired `SandboxEditorUi` to expose a document model and undo/redo buttons from the runtime history snapshot. UI does not own file IO, ECS state, or document dirty authority.
- Deferred reversible scene create/delete/duplicate and asset-import materialization undo because current runtime scene lifecycle/snapshot support cannot yet restore arbitrary materialized ECS/asset side effects without broad scene-manager work. `RUNTIME-100` / future UI workflow tasks own that expansion.
- Follow-up consumer: `UI-008` now depends on the promoted history/dirty-state source rather than inventing one.

## Verification results
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'Command|SandboxEditorUi|Selection|Scene' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

Result: configure succeeded; `IntrinsicRuntimeContractTests` built; filtered CTest passed 229/229.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Command|SandboxEditorUi|Selection|Scene' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Adding command ownership to ECS or graphics.
- Capturing raw ImGui or platform pointers in command history entries.

## Maturity
- Target: `CPUContracted` for current editor/runtime command surfaces.
