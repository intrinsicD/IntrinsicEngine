# UI-008 — Editor file dialog, dirty-state, and debug workflows

## Goal
- Promote the editor workflow affordances that improve current `SandboxEditorUi`: file/path boundary, scene dirty-state display, undo/redo controls, save/open status, and headless/CLI-safe fallbacks.

## Non-goals
- No file IO ownership in UI; runtime/core own file operations.
- No platform-native dialog implementation unless `PLATFORM-006` selects that boundary.
- No renderer/RHI state ownership in UI.
- No legacy `Interface` or `Runtime.EditorUI` imports.
- No richer sample/debug scene clone unless a runtime-owned command proves it improves a current workflow or test fixture.

## Context
- Owner/layer: `ui` task over `Runtime.SandboxEditorUi`; UI emits command DTOs to runtime and observes data-only models.
- `UI-001..007` promoted the editor shell, domain windows, processing discovery, CPU K-Means, visualization property presets, render-graph panel, and drag/drop import status. The app/parity matrix still lists file dialogs, dirty-tracker UX, command-line/headless modes, richer sample scenes, and app-level debug workflows as unproven.
- `RUNTIME-102` retired the command history/dirty-state source of truth:
  `Extrinsic.Runtime.EditorCommandHistory` owns history/dirty state, and
  `SandboxEditorUi` exposes a document model plus undo/redo buttons from that
  runtime snapshot.
  `PLATFORM-006` retired the file-dialog boundary decision: current promoted
  workflows use runtime/UI path entry plus platform dropped-path events, while
  native dialogs remain deferred behind a future platform/runtime task.

## Value gate
- Current state: the editor shell and domain workflows exist, including drag/drop import status, K-Means, visualization presets, and rendergraph diagnostics.
- Improvement: users get visible document state, undo/redo affordances, and deterministic save/open/path-entry behavior without UI owning file IO or platform state.
- Scope decision: retain dirty/undo/path-entry/status workflows. Defer native dialogs, sample-scene expansion, and app-level debug clones unless runtime/platform tasks accept them first.

## Required changes
- [x] Extend the existing data-only document/file/import models for save-as/open/new/close/debug workflows and headless-safe diagnostics.
- [x] Add UI command routing for undo/redo, new scene, close scene, save-as/open path entry, and sample/debug scene creation using runtime-owned commands.
- [x] If `PLATFORM-006` selects platform-native dialogs, add a UI request surface without owning platform state; otherwise document path-entry as the intended endpoint.
- [x] Add app-level debug workflow commands only if runtime already owns the command and the workflow improves a current fixture or diagnostic path.
- [x] Preserve `app -> runtime only` for `ExtrinsicSandbox`.

## Tests
- [x] Add `contract;runtime` or `contract;ui` tests for editor model fields and command routing.
- [x] Add app-layer dependency tests proving Sandbox still imports runtime only.
- [x] Add headless-safe tests for command-line/path-entry workflows.

## Docs
- [x] Update `tasks/backlog/ui/README.md`, `src/runtime/README.md`, and `docs/migration/nonlegacy-parity-matrix.md`.
- [x] Update app/runtime docs if command-line or sample-scene behavior changes.
- [x] Regenerate module inventory if public module surfaces change.

## Acceptance criteria
- [x] Editor dirty-state and undo/redo controls reflect runtime state and fail closed when services are unavailable.
- [x] File-dialog/path-entry behavior has an explicit boundary and tests.
- [x] Sandbox app remains policy-light and imports runtime only.

## Status
- Completed 2026-06-09 at maturity `CPUContracted`.
- PR/commit: this retirement commit.
- Extended `SandboxEditorSceneFileModel` with lifecycle, path-entry, native-dialog-boundary, and per-command availability fields.
- Added runtime-owned `Engine::NewSceneDocument()` and `Engine::CloseSceneDocument()` facades, plus `SandboxEditorUi` command routing for New, Close, Save / Save As, and Open Path.
- Kept native file dialogs deferred by data model and docs: current promoted endpoint is path entry plus platform dropped-path events.
- Added a source-scan contract proving `ExtrinsicSandbox` imports only runtime modules plus its own app module and links only `ExtrinsicRuntime`.
- Deferred sample/debug scene creation and app-level debug workflow clones because no runtime-owned command currently improves an accepted fixture without expanding scope. Future tasks must first accept the runtime command.

## Verification results
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|Headless|Panel|Command' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

Result: `IntrinsicRuntimeContractTests` built; filtered CTest passed 67/67.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|Headless|Panel|Command' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Adding platform, graphics, ECS, or asset ownership to UI.
- Importing legacy interface/editor modules.

## Maturity
- Target: `CPUContracted` for UI command/model behavior; any native dialog `Operational` proof is platform-specific and follows `PLATFORM-006`.
