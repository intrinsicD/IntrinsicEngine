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
- [ ] Define a promoted `EditorCommandHistory` or equivalent runtime-owned service with capacity, execute/record/undo/redo/clear, labels, and deterministic failure statuses.
- [ ] Add command adapters for local transform edits, selection changes, primitive-view toggles, visualization config/property edits, spatial-debug bindings, scene create/delete/duplicate, and asset import materialization where feasible.
- [ ] Define recursive delete/orphan policy for hierarchy mutations and sidecar cleanup.
- [ ] Add dirty-state tracking for scene documents and UI affordances without making UI authoritative.
- [ ] Keep command payloads data-only and stable-ID based where possible.

## Tests
- [ ] Add `contract;runtime` tests for history capacity, execute/record/undo/redo/clear, stale entity rejection, and compound command rollback order.
- [ ] Add tests for transform, selection, hierarchy delete/orphan, visualization, and import-related command cases selected by the implementation slice.
- [ ] Add UI model tests proving dirty-state and undo/redo availability are reported without UI owning state.

## Docs
- [ ] Update `src/runtime/README.md` and `docs/migration/nonlegacy-parity-matrix.md`.
- [ ] Update `tasks/backlog/ui/README.md` if SandboxEditorUi command surfaces change.
- [ ] Regenerate module inventory if public module surfaces change.

## Acceptance criteria
- [ ] Runtime owns a deterministic command history service covering current editor commands.
- [ ] Undo/redo uses ECS/runtime ownership rules and rejects stale entities without undefined behavior.
- [ ] Dirty-state UX has a tested runtime source of truth.

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
