---
id: UI-064
theme: F
depends_on: [RUNTIME-284]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive UI slice; evidence is the diff, the ImGui window test, review and CI.
contract_schema: 1
contracts: []
contract_review: Reviewed the catalog; a window over RUNTIME-284 history and checkpoint commands changes no binding, publication, module interface or format contract.
---
# UI-064 — History window

## Goal
Show the undo and redo stacks, allow undo/redo to any entry, and manage named
checkpoints; agent-made entries are distinguishable by their `Agent: ` prefix.

## Context
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- Data from `EditorDocumentModel::UndoLabels/RedoLabels` and checkpoint commands (RUNTIME-284); undo/redo through `DocumentCommands.Undo/Redo` loops.

## Control surfaces
- Config: N/A.
- UI: `view.history` ("History"): undo stack newest-first with current-position marker, redo stack, click-to-undo/redo-to-entry, Checkpoints section (name field, Create for selected entity, Restore/Delete rows, size).
- Agent/CLI: `history_list`, `undo`, `redo`, `checkpoint_*` (RUNTIME-284).

## Acceptance criteria
- [ ] Window registered; clicking an older entry undoes to it; checkpoint create/restore/delete work.
- [ ] `Agent: `-prefixed entries are visually marked.
- [ ] ImGui test: perform three commands, click the first entry, observe two undos; create and restore a checkpoint.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxProcessingPanels|HistoryWindow|EditorCheckpoint' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Direct access to `EditorCommandHistory` from app code.
