---
id: ASSETIO-005
theme: F
depends_on: [RUNTIME-101]
---
# ASSETIO-005 — Asset import queue and progress UI

## Status
- Completed 2026-06-16 at maturity `Operational`.
- PR/commit: this retirement commit.
- Scope note: this was the only unblocked ASSETIO backlog task in the generated
  session brief when work began. `ASSETIO-008` remains blocked by `GEOM-025`
  and was not promoted in this slice.
- Summary: Runtime now exposes a stable AssetIO queue snapshot over
  `RuntimeAssetIngestStateMachine`, including coarse stages, timestamps,
  determinate/indeterminate progress, terminal diagnostics, cancellation, and
  clear-completed behavior. `Engine` owns the polling and command facade, while
  `SandboxEditorUi` consumes data-only rows in the File / Import window.

## Goal
- Add a promoted AssetIO import queue/status surface that lists queued, running, applying, uploading, completed, failed, and cancelled asset imports, then expose that surface in the sandbox editor with per-asset progress bars and diagnostics.

## Non-goals
- No new file format support or decoder ownership changes.
- No GPU/Vulkan requirement for the queue or editor display.
- No fake byte-level progress for decoders that do not expose byte or item progress.
- No UI-owned asset state; editor panels read runtime-owned snapshots and emit commands only.
- No replacement for the `RUNTIME-101` ingest state machine.

## Context
- Drag/drop now routes OBJ mesh files through promoted runtime/asset import and logs completion, but long imports still look stalled to users because there is no public queue snapshot or progress UI.
- The measured `fertility.obj` case has 13,971 vertices, 27,954 triangles, and 83,862 directed face edges. The immediate slow-path bug was quadratic mesh-soup validation, but asset imports can still take visible time in debug, sanitizer, remote-storage, model-scene, texture, and future GPU-upload paths.
- `Runtime.StreamingExecutor` already owns internal task states such as queued, running, main-thread apply, GPU upload, complete, failed, and cancelled. Those states are not yet exposed as a stable import-queue contract for UI, tests, or diagnostics.
- `RUNTIME-101` is the upstream owner for consolidating manual import, drag/drop, reimport, duplicate request, cancellation, and lifecycle-drain semantics. This task consumes that state machine and adds the user-visible queue/status layer.

## Required changes
- [x] Define stable AssetIO queue snapshot DTOs with operation ID, source path, payload kind, asset ID when known, enqueue/start/finish timestamps, current stage, terminal status, normalized progress when determinate, and diagnostic text when failed.
- [x] Route manual import, drag/drop, reimport, texture upload, model-scene handoff, and main-thread materialization through the shared queue snapshot source.
- [x] Expose a runtime-owned read-only queue snapshot API suitable for polling from `SandboxEditorUi` without importing UI into assets, geometry, graphics, or platform layers.
- [x] Add cancellation and clear-completed commands only where the underlying runtime state machine can honor them deterministically; otherwise display disabled commands with diagnostics.
- [x] Add a sandbox editor AssetIO panel/table that shows all queued imports, current stage, progress bar, payload kind, path basename, elapsed time, and terminal diagnostics.
- [x] Treat unknown-progress stages as indeterminate or stage-labelled progress, never as precise byte progress.

## Tests
- [x] Add `contract;runtime` tests for queue snapshots covering queued, decoding/running, main-thread apply, GPU-upload wait, complete, failed, and cancelled states.
- [x] Add `integration;runtime` coverage for multiple dropped files preserving queue order and per-item terminal diagnostics.
- [x] Add UI model or panel tests proving multiple queue rows, progress values, failed diagnostics, disabled cancellation, and clear-completed behavior are rendered from runtime snapshots.
- [x] Keep GPU/Vulkan upload progress tests opt-in or fake-backed; the default CPU gate must not require an operational Vulkan device.

## Docs
- [x] Update runtime and asset backlog/readme docs with the queue ownership boundary.
- [x] Update sandbox editor/UI docs or task notes with the user-facing AssetIO panel behavior.
- [x] Regenerate module inventory if public module surfaces change.

## Acceptance criteria
- [x] Dropping multiple supported assets shows every import in a visible sandbox queue with stable identity, status, and stage.
- [x] Long-running imports visibly progress or show an indeterminate active state until completion/failure.
- [x] Failed imports remain visible with deterministic diagnostics instead of disappearing into logs.
- [x] Completed imports can be cleared without deleting assets or ECS scene content.
- [x] The queue API preserves layer ownership: assets remain CPU-only, runtime owns orchestration, and UI only consumes snapshots/commands.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeAssetIngestStateMachine|SandboxEditorUi.*(AssetImportQueue|DroppedFileQueue|DroppedGeometryQueueCancellation|DuplicateDropped|DroppedFilePathsRoute)|AssetImportFormatCoverage.*Ingest' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Asset|Import|Streaming|SandboxEditorUi' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
python3 tools/agents/generate_session_brief.py --check
git diff --check
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing UI/editor code into assets, geometry, graphics, platform, or core.
- Mutating asset, ECS, or graphics state from worker threads.
- Reporting determinate percentages for stages that only expose coarse state.

## Maturity
- Completed: `Operational` for the promoted sandbox import UX on CPU/null.
- Closure includes a live runtime queue snapshot and visible editor progress
  rows backed by default-gate tests.
- GPU/Vulkan-specific upload progress is optional and remains opt-in; no
  `Operational` follow-up is owed for GPU hosts because the runtime-owned queue
  and UI behavior are complete without requiring an operational Vulkan device.

## Slice plan
- **Slice A — runtime queue contract.** Extend the runtime-owned ingest state
  machine with queue snapshot DTOs, coarse stage/progress mapping, timestamps,
  terminal diagnostics, clear-completed support, and Engine facades for
  snapshot polling plus deterministic cancellation when an active
  `StreamingExecutor` task is still cancellable. Tests: state-machine queue
  snapshot coverage and engine dropped-import queue coverage.
- **Slice B — sandbox editor surface.** Add data-only AssetIO queue rows to
  `SandboxEditorUi` models and draw them in the File / Import window with
  stage text, progress/indeterminate state, basename, payload kind, elapsed
  time, terminal diagnostics, disabled cancellation reasons, and
  clear-completed command routing. Tests: pure UI model coverage and attached
  UI import queue observation.
- **Slice C — docs, generated state, and retirement.** Update runtime/UI docs,
  regenerate module inventory and session brief, run focused/default
  verification, retire the task to `tasks/done/`, and append the retirement
  narrative.
