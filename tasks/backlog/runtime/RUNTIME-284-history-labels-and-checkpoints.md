---
id: RUNTIME-284
theme: F
depends_on: [RUNTIME-287]
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [repo.source-documentation, geometry.property-coherence, runtime.editor-prepared-frame-locality]
---
# RUNTIME-284 — History label listing and entity-property checkpoints

## Goal
- Expose the full undo/redo label stacks and add named, in-memory entity-property
  checkpoints that can be restored as one undoable command.

## Non-goals
- No scene-file checkpoints or disk writes (would need `SceneSerialization` processed-property persistence and consent).
- No new undo/redo surface beyond labels: multi-step undo loops over the existing `Undo()`/`Redo()`.

## Context
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- `Runtime.EditorCommandHistory.cppm`: deques of `EditorCommandRecord{Label, Redo, Undo, Dirtying}`, `Snapshot()` exposes only top labels; `EditorDocumentModel{HistoryAvailable, CanUndo, CanRedo, UndoLabel, RedoLabel, Revision…}` in `Runtime.EditorCommon.cppm`, built in `Runtime.EditorWorkspaceSnapshots.Models.cpp`; `EditorDocumentCommandSurface{Undo, Redo}` bound in `Runtime.SceneEditingOperations.Public.cpp`.
- Checkpoints capture exact `GeometryScalarPropertySnapshot`s for all domains of an entity plus a topology stamp (`BuildSelectionTopologyStamp`) in a bounded store (count and byte caps) owned by the attachment and cleared on scene replacement (`SceneReplacementParticipantDesc`).

## Control surfaces
- Config: N/A.
- UI: History window (UI-064).
- Agent/CLI: `history_list` (read-only), `undo {steps}` / `redo {steps}` (mutating), `checkpoint_list` (read-only), `checkpoint_create/restore/delete` (mutating; in-memory only) in `Runtime.AgentOperations`.

## Required changes
- [ ] `EditorCommandHistory::UndoLabels()/RedoLabels()` (oldest → newest copies) surfaced as `EditorDocumentModel::UndoLabels/RedoLabels`.
- [ ] `Runtime.EditorCheckpointOperations.cppm` + implementation: `CreateEditorCheckpoint {name, entity}`, `RestoreEditorCheckpoint {name}` as one history command "Restore checkpoint <name>" with stale-topology guard, `ListEditorCheckpoints` (name, entity, bytes), `DeleteEditorCheckpoint`.
- [ ] Agent operations registered (undo/redo steps bounded).

## Tests
- [ ] `Test.EditorCommandHistory.cpp`: label listing order, prefix scope interplay.
- [ ] `tests/contract/runtime/Test.EditorCheckpointOperations.cpp`: create → mutate → restore → undo restore; stale topology refused; caps enforced; cleared on scene replacement.

## Docs
- [ ] `docs/architecture/sandbox-editor-feature-boundaries.md`; module inventory regenerated.

## Acceptance criteria
- [ ] Full label stacks visible through the document model; checkpoints restore exact property values and are undoable.
- [ ] Memory bounded; nothing written to disk.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'EditorCommandHistory|EditorCheckpoint|AgentOperations' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Disk-backed checkpoints; unbounded stores; new undo surfaces duplicating `Undo()`/`Redo()`.
