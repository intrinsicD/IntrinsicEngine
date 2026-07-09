---
id: RUNTIME-147
theme: F
depends_on: []
maturity_target: Operational
completed: 2026-07-08
---
# RUNTIME-147 — Extract the runtime asset-import pipeline out of Engine

## Status

- Retired on 2026-07-08 at `Operational`.
- PR: pending. Commit: pending local change.
- Runtime now owns `Extrinsic.Runtime.AssetImportPipeline`, an engine-owned
  import subsystem that carries the import/reimport/queue/cancel facade,
  ingest-state records, import event log, `RUNTIME-144` registries, decode
  helpers, materialization helpers, and import dirty-state marking.
- `Runtime.Engine.cppm` now exposes only
  `Engine::GetAssetImportPipeline()` / `const` accessors for this surface;
  `Runtime.Engine.cpp` delegates platform drop routing to the pipeline and no
  longer defines the moved import helpers.
- Sandbox default policies, Sandbox editor UI, and runtime import tests now use
  the pipeline accessor directly, with no compatibility import facade left on
  `Engine`.
- Verified locally with the `ci` preset, which enables
  `INTRINSIC_ENABLE_SANITIZERS=ON`: focused runtime import/editor tests passed
  31/31, `IntrinsicRuntimeContractTests` and `IntrinsicTests` built, the full
  default CPU-supported CTest gate passed 3638/3638, strict layering,
  test-layout, task-policy, docs-link, docs-sync, and diff-whitespace checks
  passed, and the module inventory was regenerated. Root hygiene remains in
  warning mode for pre-existing `ara/` and `imgui.ini` root entries.

## Goal
- Move the entire asset-import facade — request/queue/reimport/cancel
  entry points, the ingest state machine wiring, the import event log, the
  post-import processor / import-authoring-policy / import-completed
  registries, and the ~1,500 lines of decode/materialize helpers — from
  `Extrinsic.Runtime.Engine` into a new engine-owned subsystem
  `Extrinsic.Runtime.AssetImportPipeline`, exposed via a single
  `Engine::GetAssetImportPipeline()` accessor.

## Non-goals
- No behavior change to import routing, ingest diagnostics, event
  sequencing, or streaming-lane usage (`RUNTIME-142` semantics preserved).
- No change to the registration seams' contracts established by
  `RUNTIME-144` (descs, handles, dispatch order stay identical).
- No changes to `AssetService`, `Asset.ImportRouter`, or the IO bridges.
- No input-action registry move (input dispatch is frame-loop concern,
  stays with `Engine` / `RUNTIME-150`).
- No scene save/load moves (owned by `RUNTIME-148`).

## Context
- Owning subsystem/layer: `runtime`. `Engine` remains the composition root
  and owner; the pipeline becomes a value/`unique_ptr` member constructed in
  `Engine::Initialize()` with references to the collaborators it already
  uses today (`StreamingExecutor`, `AssetService`, `GpuAssetCache`,
  `RenderExtractionCache`, `ECS::Scene::Registry`,
  `CameraControllerRegistry`, `SelectionController`,
  `EditorCommandHistory`, `EngineConfig`).
- Document dirty-state coupling: successful scene-changing imports mark
  the document dirty via `m_EditorCommandHistory.MarkDirty("Import
  Asset")` / `("Reimport Asset")` in `ImportAssetFromPath`,
  `ReimportAsset`, the dropped-file apply, and both queued applies
  (`Runtime.Engine.cpp:4217,4271,4557,4816,5190`). The pipeline must keep
  marking through the engine-owned `EditorCommandHistory` — after Slice B
  removes the `Engine` delegations there is no other place this happens,
  so omitting the reference would silently regress File/Scene dirty
  state for imports.
- Current locations, all in `src/runtime/Runtime.Engine.{cppm,cpp}`:
  - Public methods: `ImportAssetFromPath`, `QueueModelTextureImport`,
    `ReimportAsset`, `ImportDroppedFilePaths`, `CancelAssetImport`,
    `ClearCompletedAssetImports`, `GetAssetImportQueueSnapshot`,
    `GetLastAssetImportEvent`, `GetAssetIngestRecordsForTest`,
    `SetModelTextureImportIOBackendFactoryForTest`, and the six
    `Register/Unregister*` methods for post-import processors,
    import-entity authoring policies, and import-completed handlers.
  - Private methods: `QueueDroppedGeometryImport`,
    `QueueDroppedModelTextureImport`, `QueueModelTextureImportWithIngest`,
    `ImportAssetFromPathWithIngest`, `ImportAssetFromPathImpl`,
    `RecordAssetImportEvent`.
  - State: `m_AssetIngestStateMachine`, `m_PostImportProcessors`,
    `m_ImportEntityAuthoringPolicies`, `m_ImportCompletedHandlers`,
    `m_InputActions` stays behind, `m_AssetImportStreamingTasks`,
    `m_LastAssetImportEvent`, `m_AssetImportEventSequence`.
  - Anonymous-namespace helpers: the decode/materialize structs
    (`Decoded*Import`, `Dropped*ImportState`, `MaterializedGeometryImport`,
    `GeometryImportBounds`…), ingest translation helpers, registry
    dispatch helpers (`RunPostImportProcessors`,
    `RunImportEntityAuthoringPolicies`, `RunImportCompletedHandlers`), and
    the camera-focus bounds helpers.
- Exported request/result/desc/handle types (`RuntimeAssetImport*`,
  `RuntimePostImportProcessor*`, `RuntimeImportEntityAuthoringPolicy*`,
  `RuntimeImportCompletedHandler*`, `RuntimeQueuedAssetImport`,
  `RuntimeIOBackendFactory`) move to the new module's interface.
- Known consumers to update: `src/runtime/Runtime.SandboxDefaultPolicies.*`,
  `src/runtime/Editor/Runtime.SandboxEditorUi.*`, `src/app/Sandbox/*`, and
  the contract/integration tests that drive imports through `Engine`.
- `Engine::HandleWindowDropEvent` stays in `Engine` (platform event routing)
  and delegates to the pipeline.
- ARCH-013 re-review (2026-07-08): Decision re-scoped. The mechanical
  extraction remains valid, but `Engine::GetAssetImportPipeline()` is a
  transitional composition-root accessor, not a new cross-module dependency
  pattern. The moved pipeline should be shaped so a later Resolve-phase service
  or import module can expose narrow capabilities through `ServiceRegistry`;
  app/default policy registration should continue to happen from composition
  code, and ADR-0024's parked asset-boundary question remains owned here.
- Part of the `Runtime.Engine` decomposition series (`RUNTIME-146..151`).
  This is the largest slice of the series; land it via the slice plan below.

## Slice plan
- **Slice A (mechanical extraction).** Create
  `src/runtime/Runtime.AssetImportPipeline.{cppm,cpp}`; move types, state,
  helpers, and method bodies verbatim. `Engine` keeps its existing public
  methods as one-line delegations to the pipeline so no call site outside
  `Engine` changes. CPU gate must stay green. Defers call-site migration
  and delegation removal to Slice B.
- **Slice B (call-site migration).** Update `SandboxDefaultPolicies`,
  `SandboxEditorUi`, Sandbox app code, and tests to use
  `GetAssetImportPipeline()` directly; delete the delegating methods from
  `Engine`. Defers nothing.

## Required changes
- [x] Add `Extrinsic.Runtime.AssetImportPipeline` interface + implementation
      units under `src/runtime/`, registered in `src/runtime/CMakeLists.txt`.
- [x] Move the public methods, private methods, member state, exported
      types, and anonymous-namespace helpers listed in Context.
- [x] Construct/destroy the pipeline in `Engine::Initialize()` /
      `Engine::Shutdown()` preserving current ordering relative to
      `StreamingExecutor`, `AssetService`, and `GpuAssetCache` teardown.
- [x] Wire the engine-owned `EditorCommandHistory` into the pipeline and
      preserve the `MarkDirty` calls on every successful scene-changing
      import path (sync, reimport, dropped-file apply, queued applies).
- [x] Add `Engine::GetAssetImportPipeline()` (and `const` overload).
- [x] Slice A: keep delegating `Engine` methods; Slice B: migrate all call
      sites and delete the delegations.
- [x] Keep registration-before-`Initialize()` behavior working for the
      three registries (stash-and-apply, as `SetImGuiEditorCallback` does)
      or document in the task file why registration now requires
      `Initialize()` first and update `SandboxDefaultPolicies` accordingly.

## Tests
- [x] Existing import/ingest contract tests pass with only import/name
      updates (`Test.RuntimeSandboxAcceptance`, ingest queue/cancel tests,
      `Test.SandboxEditorUi`, policy/processor registration tests).
- [x] Default CPU gate stays green:
      `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine'`.
- [x] One new contract test proving `GetAssetImportPipeline()` exposes the
      same event-log/queue-snapshot state the `Engine` facade returned
      before the move (guards the delegation removal in Slice B).
- [x] A contract test (new or extended) asserting the
      `EditorCommandHistory` document state reads dirty after a
      successful scene-changing import driven through the pipeline, and
      stays unmarked after a failed import (guards the dirty-state
      coupling across Slice B).

## Docs
- [x] Update `src/runtime/README.md` module list for the new module.
- [x] Regenerate module inventories per `intrinsicengine-docs-sync`.
- [x] Update `tasks/backlog/runtime/README.md` status line on retirement.

## Acceptance criteria
- [x] `Runtime.Engine.cppm` no longer declares any import/ingest/registry
      method or member listed in Context; `Runtime.Engine.cpp` shrinks by
      at least the moved helper block (~2,500+ lines total).
- [x] All import behavior verified by the existing test suite is unchanged.
- [x] Document dirty-state marking for imports behaves identically before
      and after Slice B (covered by the named contract test).
- [x] Teardown ordering is preserved (no new shutdown races; existing
      shutdown-path tests pass).
- [x] CPU gate and layering check pass after each slice.

## Verification
Completed 2026-07-08:

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='RuntimeAssetImportFormatCoverage.*:SandboxEditorUi.*Import*:SandboxEditorUi.*Reimport*:SandboxEditorUi.*AssetImport*'
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root .
python3 tools/repo/check_root_hygiene.py --root .
git diff --check
```

## Forbidden changes
- Mixing the mechanical move (Slice A) with any semantic refactor of
  import behavior, diagnostics, or registry dispatch order.
- Changing `RuntimeAssetImport*` / registry desc/handle contracts.
- Touching scene save/load, config control, or frame-loop code owned by
  `RUNTIME-148..150`.

## Maturity
- Target: `Operational` — the import pipeline is already exercised through
  `Engine::Run()` and the sandbox; both slices must preserve that. No new
  capability follow-up is owed by this task.
