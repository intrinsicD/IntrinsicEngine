---
id: RUNTIME-148
theme: F
depends_on: []
maturity_target: Operational
---
# RUNTIME-148 — Extract the scene-document facade out of Engine

## Goal
- Move the scene persistence facade (`RUNTIME-098`/`RUNTIME-142` surface) —
  save/load/queue entry points, new/close document, the scene-file event
  log, and the serializable-scene snapshot helpers — from
  `Extrinsic.Runtime.Engine` into a new engine-owned subsystem
  `Extrinsic.Runtime.SceneDocument`, exposed via
  `Engine::GetSceneDocument()`.

## Non-goals
- No change to serialization format or `Runtime.SceneSerialization`.
- No change to the async queue semantics landed by `RUNTIME-142`.
- No change to `EditorCommandHistory` dirty-state vocabulary — the document
  subsystem keeps marking the same save/load/import transitions.
- No asset-import moves (owned by `RUNTIME-147`).

## Context
- Owning subsystem/layer: `runtime`. `Engine` remains owner; the subsystem
  is constructed in `Initialize()` with references to
  `ECS::Scene::Registry`, `StreamingExecutor`, `EditorCommandHistory`,
  `SelectionController`, `StableEntityLookup`, and `RenderExtractionCache`.
- Current locations, all in `src/runtime/Runtime.Engine.{cppm,cpp}`:
  - Public methods: `SaveSceneToPath`, `QueueSceneSaveToPath`,
    `LoadSceneFromPath`, `QueueSceneLoadFromPath`, `GetLastSceneFileEvent`,
    `NewSceneDocument`, `CloseSceneDocument`.
  - Private methods: `RecordSceneFileEvent`, `ClearSceneRuntimeState`.
  - State: `m_LastSceneFileEvent`, `m_SceneFileEventSequence`.
  - Exported types: `RuntimeSceneFileOperation`,
    `RuntimeQueuedSceneFileOperation`, `RuntimeSceneFileEvent`.
  - Anonymous-namespace helpers: `SnapshotSerializableScene`,
    `CopySerializableComponent`, `CopySerializableTag`,
    `CopySerializableHierarchy`, `QueuedSceneLoadState`,
    `QueuedSceneSaveState`.
- Coupling to preserve: scene replacement must still (1) rebuild
  `StableEntityLookup` at the replacement boundary
  (`RebuildStableEntityLookupAfterSceneReplacement`), (2) clear selection
  and runtime sidecars (`ClearSceneRuntimeState`), and (3) mark
  `EditorCommandHistory` document state. Whether these run via direct
  references or an `Engine`-installed post-replacement callback is the
  implementer's choice; the ordering contract is what the tests pin.
- Known consumers to update: `Runtime.SandboxEditorUi` (editor file menu),
  Sandbox app code, `Test.RuntimeSceneLifecycle.cpp`, and any acceptance
  tests driving save/load through `Engine`.
- Part of the `Runtime.Engine` decomposition series (`RUNTIME-146..151`).

## Required changes
- [ ] Add `Extrinsic.Runtime.SceneDocument` interface + implementation
      units under `src/runtime/`, registered in `src/runtime/CMakeLists.txt`.
- [ ] Move the methods, state, exported types, and snapshot helpers listed
      in Context verbatim.
- [ ] Construct/destroy the subsystem in `Engine::Initialize()` /
      `Shutdown()`; keep the scene-replacement side-effect ordering
      (lookup rebuild → sidecar clear → history marking) identical.
- [ ] Add `Engine::GetSceneDocument()` (and `const` overload); migrate
      call sites; do not keep delegating methods on `Engine`.
- [ ] Keep drop-event and import paths (owned elsewhere) compiling against
      the moved types.

## Tests
- [ ] `Test.RuntimeSceneLifecycle.cpp` passes with only import/name
      updates (behavior-preservation evidence for save/load/new/close and
      the event log).
- [ ] The `RUNTIME-142` slow-IO regression (frame loop advances while a
      queued scene save/load is blocked) still passes.
- [ ] Default CPU gate stays green:
      `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine'`.

## Docs
- [ ] Update `src/runtime/README.md` module list for the new module.
- [ ] Regenerate module inventories per `intrinsicengine-docs-sync`.
- [ ] Update `tasks/backlog/runtime/README.md` status line on retirement.

## Acceptance criteria
- [ ] `Runtime.Engine.cppm` no longer declares any scene-file method,
      event type, or member listed in Context.
- [ ] Scene replacement side-effect ordering is unchanged and covered by a
      test (existing or extended `Test.RuntimeSceneLifecycle` case).
- [ ] CPU gate and layering check pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Mixing this mechanical move with serialization-format or dirty-state
  semantic changes.
- Touching asset import, config control, or frame-loop code owned by
  `RUNTIME-147`/`RUNTIME-149`/`RUNTIME-150`.

## Maturity
- Target: `Operational` — save/load is already exercised by the sandbox
  editor and lifecycle tests; the move must preserve that. No new
  capability follow-up is owed.
