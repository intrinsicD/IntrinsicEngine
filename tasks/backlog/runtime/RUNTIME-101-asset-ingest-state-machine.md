# RUNTIME-101 — Asset ingest state-machine migration

## Goal
- Replace legacy `Runtime.AssetIngestService` with a promoted runtime ingest state machine over `AssetService`, `Asset.ImportRouter`, `Runtime.StreamingExecutor`, and runtime-owned ECS/materialization handoffs.

## Non-goals
- No decoder ownership in runtime beyond registering lower-level callbacks already assigned to runtime bridges.
- No graphics/RHI imports in assets.
- No UI file-dialog implementation; UI command surfaces are separate.
- No legacy `Graphics.IORegistry` or `Graphics.ModelLoader` imports.

## Context
- Owner/layer: `runtime` owns cross-layer ingest orchestration and main-thread scene materialization.
- `UI-007` added drag/drop import UI and `Runtime.StreamingExecutor` apply behavior for promoted paths, but the parity matrix still names asset ingest service migration as an unproven legacy retirement gate.
- Reuse `AssetService`, `AssetGeometryIO`, `AssetModelTextureIO`, `AssetModelSceneHandoff`, `AssetModelTextureHandoff`, `StreamingExecutor`, and `Engine::ImportDroppedFilePaths(...)`.

## Value gate
- Current state: promoted manual import and drag/drop already route through asset bridges and `StreamingExecutor`.
- Improvement: a shared request/result state machine prevents duplicate materialization, stale completion clobbering, and divergent manual vs drag/drop diagnostics.
- Scope decision: retain one narrow runtime ingest coordinator over existing bridges; do not recreate legacy ingest services, decoder registries, or ECS `AssetSourceRef` coupling.

## Required changes
- [ ] Define a single promoted ingest request/result state machine for manual import, drag/drop, reimport, cancellation, duplicate requests, and main-thread apply.
- [ ] Route mesh, graph, point-cloud, model-scene, and texture ingest through existing asset bridges and materialization handoffs.
- [ ] Preserve deterministic diagnostics for missing files, unsupported formats, ambiguous payload hints, decode failure, callback failure, and materialization failure.
- [ ] Add reimport policy for existing asset IDs and scene entities without reviving legacy `AssetSourceRef` behavior in ECS beyond documented CPU metadata.
- [ ] Ensure streaming completion is drained in runtime lifecycle order from `RUNTIME-099`.

## Tests
- [ ] Add `contract;runtime` tests for ingest state transitions and diagnostics.
- [ ] Add `integration;runtime` tests for drag/drop and reimport through `StreamingExecutor` with main-thread apply.
- [ ] Add regression tests proving stale/duplicate completions do not materialize duplicate entities or clobber newer asset generations.

## Docs
- [ ] Update `src/runtime/README.md` and `docs/migration/nonlegacy-parity-matrix.md`.
- [ ] Update `tasks/backlog/assets/README.md` if ingest ownership changes.
- [ ] Regenerate module inventory if public module surfaces change.

## Acceptance criteria
- [ ] All runtime import entry points share the same ingest state machine and result taxonomy.
- [ ] Reimport and drag/drop no longer depend on legacy `Runtime.AssetIngestService` semantics.
- [ ] Ingest completion is deterministic under concurrent queued requests.

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
- Importing legacy `Runtime.AssetIngestService`, `Graphics.IORegistry`, or `Graphics.ModelLoader`.
- Mutating ECS or asset state from worker threads.

## Maturity
- Target: `CPUContracted` for the ingest state machine; representative CPU/null import coverage is retired by `ASSETIO-004`.
- no `Operational` follow-up is owed for RUNTIME-101; broader file-backed GPU/readback proof requires a future value-gated task outside this ingest-state-machine scope.
