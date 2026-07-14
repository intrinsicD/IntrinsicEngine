# BUG-021 — Sandbox drop import blocks platform polling

## Status
- Status: done.
- Maturity: `CPUContracted`.
- Completion date: 2026-06-08.
- PR/commit: pending local commit.

## Goal
- Make promoted sandbox file drops enqueue import work without synchronously decoding large mesh files inside platform event polling, while preserving main-thread `AssetService` and ECS materialization.

## Non-goals
- No Vulkan render-graph barrier or promoted-device operational fix in this task.
- No new file format decoders or importer parity changes.
- No renderer thread ownership changes; runtime remains the composition owner.
- No edits to `src/graphics/framegraph/Graphics.RenderGraph.Compiler.cpp` unless a focused render-graph regression proves it is required.

## Context
- Owner layer: `runtime`, with a small `Runtime.StreamingExecutor` contract extension.
- `Platform::WindowDropEvent` is emitted during `Window::PollEvents()`; before BUG-021, the runtime listener called `ImportDroppedFilePaths(...)`, which decoded and materialized inline.
- Large OBJ files such as Suzanne can stall the app because decode and mesh conversion run before the frame proceeds.
- Asset-service mutation and ECS entity creation must remain on the main thread.

## Required changes
- [x] Extend `StreamingExecutor` so successful CPU payload results with `ApplyOnMainThread` use the existing `WaitingForMainThreadApply` state and are applied on the main thread.
- [x] Change dropped-file import routing to submit background decode/conversion work instead of running `ImportAssetFromPath(...)` inline from the platform drop callback.
- [x] Keep manual editor import commands synchronous for deterministic command-result behavior.
- [x] Preserve ambiguous geometry extension fallback for dropped files.

## Tests
- [x] Add/update streaming executor coverage for CPU-payload main-thread apply.
- [x] Add/update sandbox editor/runtime contract coverage proving dropped file import is deferred and later materialized.
- [x] Run focused runtime/editor/streaming tests.
- [x] Run default CPU-supported CTest gate.
- [x] Run structural checks relevant to task/docs/layering/test layout.

## Docs
- [x] Update runtime docs to distinguish manual synchronous imports from deferred platform drop imports.
- [x] Refresh generated module inventory if public module surfaces change. No refresh was required for BUG-021 because it only adds private runtime wiring and executor behavior coverage.

## Acceptance criteria
- [x] Dropping a supported mesh, graph, or point-cloud file records/queues the request without creating ECS geometry inside `Window::PollEvents()`.
- [x] The queued import decodes off the main thread and applies `AssetService`/ECS changes from the main-thread apply phase.
- [x] Last import status is still observable by the sandbox editor UI after the deferred apply completes.
- [x] Invalid paths and unsupported files still fail closed with a recorded import event.
- [x] Existing manual `Engine::ImportAssetFromPath(...)` behavior is preserved.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeStreamingExecutor|SandboxEditorUi|AssetGeometryIO' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
git diff --check
```

## Forbidden changes
- Do not move file decoding into graphics, platform, or renderer layers.
- Do not let background tasks mutate `AssetService`, ECS registry, renderer, or RHI objects.
- Do not weaken the import router ambiguity diagnostics for manual imports.
- Do not edit `Graphics.RenderGraph.Compiler.cpp` for the import-stall fix.

## Maturity
- Target: `CPUContracted` for the runtime/drop threading contract.
- The Vulkan validation messages reported with promoted Vulkan are tracked as a separate render-graph/promoted-device diagnosis; no `Operational` follow-up is owed by this import-stall task.
