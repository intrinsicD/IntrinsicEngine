---
id: RUNTIME-142
theme: F
depends_on: []
---
# RUNTIME-142 — Async model-scene/texture import and scene-file IO

## Goal
- Extend the deferred streaming-import path (already used by dropped
  geometry) to model-scene and texture drops and to scene save/load, so no
  file read or decode runs synchronously on the main thread inside
  `Engine::RunFrame()`.

## Non-goals
- No change to supported formats, decode semantics, or scene JSON schema.
- No import-pipeline extension seam (that is `RUNTIME-144`).
- The global-barrier removal in the apply path is `RUNTIME-140`.

## Context
- Owner/layer: `runtime` (`Runtime.Engine` import routing,
  `Runtime.StreamingExecutor`, `Runtime.AssetModelSceneHandoff`,
  `Runtime.SceneSerialization`).
- Geometry drops already stream correctly: decode on the worker lane,
  main-thread apply (`src/runtime/Runtime.Engine.cpp:3766-3924`, BUG-044).
  But model-scene/texture drops and all editor-menu imports run synchronous
  file IO + full decode inline
  (`Runtime.Engine.cpp:3645-3661` synchronous route;
  `4226-4293, 4418`: `Core::IO::FileIOBackend backend;
  bridge.ImportModelScene(request.Path, backend)`), and scene save/load is
  synchronous too (`4526-4561`). Drop events fire during Phase-1
  `PollEvents`; editor commands during the ImGui callback — both inside the
  frame.
- Origin: `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  finding R11.

## Required changes
- [x] Route model-scene and texture dropped-file imports through
      `StreamingExecutor`: file read + decode on the worker lane, existing
      handoff/materialization on the main-thread apply, preserving current
      event/diagnostic semantics (BUG-038 logging contract).
- [x] Route model-scene and texture editor-menu/manual imports through
      `StreamingExecutor` while preserving the current command-result surface
      or replacing it with an explicit pending operation contract.
- [x] Route scene load through the same shape: parse into the temporary
      registry off-thread; keep the swap-in (registry replacement, sidecar
      drains, `StableEntityLookup` rebuild) on the main thread per the
      documented scene-replacement lifecycle.
- [x] Route scene save off-thread: snapshot serializable state on the main
      thread, serialize + write on the worker lane, report completion via
      the existing import/editor event surface.
- [x] Keep failure behavior fail-closed and identical (a bad parse never
      touches the live scene).

## Tests
- [x] Contract: dropped model-scene and texture imports queue immediately,
      complete through the streaming apply lane, and materialize fixture
      assets with the same result counters as the synchronous path.
- [x] Contract: editor-menu/manual model-scene and texture imports return a
      pending operation, enter the runtime import queue, and complete through
      the async route with unchanged observable results.
- [x] Contract: scene load with an invalid document leaves the live scene
      untouched (existing guarantee preserved through the async route).
- [x] Contract: queued scene save writes the queued scene snapshot, reports
      completion through the scene-file event, and marks the editor document
      saved only after the main-thread completion callback.
- [ ] Contract: frame does not block during a large import (timing probe
      with a slow-IO fake backend).
- [x] Existing drop/import/scene-serialization suites stay green.

## Docs
- [x] Update `src/runtime/README.md` and
      `docs/architecture/runtime.md` dropped asset-import notes.
- [x] Update runtime import notes for dropped and editor-menu/manual
      model-scene/texture queued imports.
- [x] Update scene-replacement/import notes for async scene-load routing.
- [x] Update scene-replacement/import notes again when scene save moves off
      the frame path.

## Acceptance criteria
- [ ] No synchronous file IO or asset decode reachable from
      `Engine::RunFrame()` for model-scene/texture/scene-document routes
      (audited call-site list recorded in this file).
- [ ] Import/save/load observable behavior unchanged apart from latency now
      spanning frames.
- [x] Default CPU gate green.

## Slice log
- 2026-07-05: Dropped model-scene and texture imports now use
  `Engine::QueueDroppedModelTextureImport(...)`. The worker callback builds the
  promoted ASSETIO bridge and runs `ImportModelScene(...)` / `ImportTexture2D(...)`
  with `Core::IO::FileIOBackend`; the main-thread apply callback shares the
  same decoded model-scene/texture materialization helpers used by
  `ImportAssetFromPathImpl(...)`, completes the ingest state machine, and marks
  document history dirty only after successful scene-changing apply.
- Remaining synchronous audited call sites after the dropped-file slice:
  `Engine::ImportAssetFromPath(...)` / `Engine::ReimportAsset(...)` still call
  `ImportAssetFromPathWithIngest(...)` -> `ImportAssetFromPathImpl(...)`, where
  model-scene/texture direct imports still construct `Core::IO::FileIOBackend`
  and decode inline. `Engine::SaveSceneToPath(...)` and
  `Engine::LoadSceneFromPath(...)` still serialize/parse scene documents inline.
- 2026-07-05: Sandbox editor model-scene/texture import commands now route
  through `Engine::QueueModelTextureImport(...)`. The command surface returns
  `SandboxEditorCommandStatus::Pending` plus the ingest handle immediately; the
  existing runtime import event and queue snapshot publish completion. The
  queued worker/apply path is shared with dropped model-scene/texture imports
  and accepts `Unknown` hints by resolving the effective `.gltf`/texture payload
  before submission.
- Remaining synchronous audited call sites after this slice:
  direct programmatic `Engine::ImportAssetFromPath(...)` /
  `Engine::ReimportAsset(...)` compatibility calls can still decode
  model-scene/texture payloads inline. `Engine::SaveSceneToPath(...)` and
  `Engine::LoadSceneFromPath(...)` still serialize/parse scene documents inline.
- 2026-07-05: Sandbox editor scene-open commands now route through
  `Engine::QueueSceneLoadFromPath(...)`. The worker lane reads and parses the
  scene document into a temporary registry, then the main-thread apply callback
  runs the existing scene-replacement lifecycle (`ClearSceneRuntimeState()`,
  registry clear/swap, `StableEntityLookup` rebuild, document-history reset).
  Completion is published through `Engine::GetLastSceneFileEvent()` so the
  editor's pending load result is replaced by a success/failure result on a
  later frame. Invalid documents now fail closed through the async route and do
  not touch the live scene.
- Remaining synchronous audited call sites after the scene-load slice:
  direct programmatic `Engine::ImportAssetFromPath(...)` /
  `Engine::ReimportAsset(...)` compatibility calls can still decode
  model-scene/texture payloads inline. Direct `Engine::LoadSceneFromPath(...)`
  remains a synchronous compatibility facade. Sandbox editor save commands and
  direct `Engine::SaveSceneToPath(...)` still serialize/write scene documents
  inline.
- 2026-07-05: Sandbox editor scene-save commands now route through
  `Engine::QueueSceneSaveToPath(...)`. The frame thread snapshots the persisted
  scene surface into a temporary registry, the worker lane serializes and writes
  that snapshot with `Core::IO::FileIOBackend`, and the main-thread completion
  callback marks `EditorCommandHistory` saved and publishes
  `Engine::GetLastSceneFileEvent()`.
- Remaining synchronous audited call sites after the scene-save slice:
  direct programmatic `Engine::ImportAssetFromPath(...)` /
  `Engine::ReimportAsset(...)` compatibility calls can still decode
  model-scene/texture payloads inline. Direct `Engine::SaveSceneToPath(...)`
  and `Engine::LoadSceneFromPath(...)` remain synchronous compatibility
  facades.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Worker-thread mutation of the live ECS registry, renderer, or asset
  service (decode produces data; apply mutates, on the main thread).
- Relaxing the fail-closed scene-load guarantee.
- Format/schema changes.
