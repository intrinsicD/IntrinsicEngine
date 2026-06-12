---
id: ASSETIO-005
theme: F
depends_on: [RUNTIME-101]
---
# ASSETIO-005 — Asset import queue and progress UI

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
- [ ] Define stable AssetIO queue snapshot DTOs with operation ID, source path, payload kind, asset ID when known, enqueue/start/finish timestamps, current stage, terminal status, normalized progress when determinate, and diagnostic text when failed.
- [ ] Route manual import, drag/drop, reimport, texture upload, model-scene handoff, and main-thread materialization through the shared queue snapshot source.
- [ ] Expose a runtime-owned read-only queue snapshot API suitable for polling from `SandboxEditorUi` without importing UI into assets, geometry, graphics, or platform layers.
- [ ] Add cancellation and clear-completed commands only where the underlying runtime state machine can honor them deterministically; otherwise display disabled commands with diagnostics.
- [ ] Add a sandbox editor AssetIO panel/table that shows all queued imports, current stage, progress bar, payload kind, path basename, elapsed time, and terminal diagnostics.
- [ ] Treat unknown-progress stages as indeterminate or stage-labelled progress, never as precise byte progress.

## Tests
- [ ] Add `contract;runtime` tests for queue snapshots covering queued, decoding/running, main-thread apply, GPU-upload wait, complete, failed, and cancelled states.
- [ ] Add `integration;runtime` coverage for multiple dropped files preserving queue order and per-item terminal diagnostics.
- [ ] Add UI model or panel tests proving multiple queue rows, progress values, failed diagnostics, disabled cancellation, and clear-completed behavior are rendered from runtime snapshots.
- [ ] Keep GPU/Vulkan upload progress tests opt-in or fake-backed; the default CPU gate must not require an operational Vulkan device.

## Docs
- [ ] Update runtime and asset backlog/readme docs with the queue ownership boundary.
- [ ] Update sandbox editor/UI docs or task notes with the user-facing AssetIO panel behavior.
- [ ] Regenerate module inventory if public module surfaces change.

## Acceptance criteria
- [ ] Dropping multiple supported assets shows every import in a visible sandbox queue with stable identity, status, and stage.
- [ ] Long-running imports visibly progress or show an indeterminate active state until completion/failure.
- [ ] Failed imports remain visible with deterministic diagnostics instead of disappearing into logs.
- [ ] Completed imports can be cleared without deleting assets or ECS scene content.
- [ ] The queue API preserves layer ownership: assets remain CPU-only, runtime owns orchestration, and UI only consumes snapshots/commands.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Asset|Import|Streaming|SandboxEditorUi' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing UI/editor code into assets, geometry, graphics, platform, or core.
- Mutating asset, ECS, or graphics state from worker threads.
- Reporting determinate percentages for stages that only expose coarse state.

## Maturity
- Target: `Operational` for the promoted sandbox import UX on CPU/null and GPU-capable hosts.
- This task must not retire as a scaffold: closure requires a live runtime queue snapshot and visible editor progress rows backed by tests.
- GPU/Vulkan-specific upload progress is optional and must stay opt-in; no Operational follow-up is owed for GPU hosts if the CPU/null queue and UI behavior are complete.
