---
id: RUNTIME-101
theme: F
depends_on: [RUNTIME-099]
---
# RUNTIME-101 — Asset ingest state-machine migration

## Status
- Completed 2026-06-15 at maturity `CPUContracted`.
- PR/commit: this retirement commit.
- Decision: `Extrinsic.Runtime.AssetIngestStateMachine` is the promoted
  backend-neutral ingest request/result contract for manual import, dropped
  files, and reimport. `Engine::ReimportAsset(...)` reloads the same
  `AssetId` through `AssetService` path metadata and payload type detection;
  it does not revive legacy scene-file `AssetSourceRef` coupling.

## Goal
- Replace legacy `Runtime.AssetIngestService` with a promoted runtime ingest state machine over `AssetService`, `Asset.ImportRouter`, `Runtime.StreamingExecutor`, and runtime-owned ECS/materialization handoffs.

## Non-goals
- No decoder ownership in runtime beyond registering lower-level callbacks already assigned to runtime bridges.
- No graphics/RHI imports in assets.
- No UI file-dialog implementation; UI command surfaces are separate.
- No legacy `Graphics.IORegistry` or `Graphics.ModelLoader` imports.

## Context
- Owner/layer: `runtime` owns cross-layer ingest orchestration and main-thread scene materialization.
- `UI-007` added drag/drop import UI and `Runtime.StreamingExecutor` apply behavior for promoted paths; this task closes the parity-matrix asset ingest service migration gate at `CPUContracted`.
- Reuse `AssetService`, `AssetGeometryIO`, `AssetModelTextureIO`, `AssetModelSceneHandoff`, `AssetModelTextureHandoff`, `StreamingExecutor`, and `Engine::ImportDroppedFilePaths(...)`.

## Value gate
- Current state: promoted manual import and drag/drop already route through asset bridges and `StreamingExecutor`.
- Improvement: a shared request/result state machine prevents duplicate materialization, stale completion clobbering, and divergent manual vs drag/drop diagnostics.
- Scope decision: retain one narrow runtime ingest coordinator over existing bridges; do not recreate legacy ingest services, decoder registries, or ECS `AssetSourceRef` coupling.

## Required changes
- [x] Define a single promoted ingest request/result state machine for manual import, drag/drop, reimport, cancellation, duplicate requests, and main-thread apply.
- [x] Route mesh, graph, point-cloud, model-scene, and texture ingest through existing asset bridges and materialization handoffs.
- [x] Preserve deterministic diagnostics for missing files, unsupported formats, ambiguous payload hints, decode failure, callback failure, and materialization failure.
- [x] Add reimport policy for existing asset IDs and scene entities without reviving legacy `AssetSourceRef` behavior in ECS beyond documented CPU metadata.
- [x] Ensure streaming completion is drained in runtime lifecycle order from `RUNTIME-099`.

## Tests
- [x] Add `contract;runtime` tests for ingest state transitions and diagnostics.
- [x] Add `integration;runtime` tests for drag/drop and reimport through `StreamingExecutor` with main-thread apply.
- [x] Add regression tests proving stale/duplicate completions do not materialize duplicate entities or clobber newer asset generations.

## Docs
- [x] Update `src/runtime/README.md` and `docs/migration/nonlegacy-parity-matrix.md`.
- [x] No `tasks/backlog/assets/README.md` update needed; ingest ownership remains runtime.
- [x] Regenerate module inventory if public module surfaces change.

## Acceptance criteria
- [x] All runtime import entry points share the same ingest state machine and result taxonomy.
- [x] Reimport and drag/drop no longer depend on legacy `Runtime.AssetIngestService` semantics.
- [x] Ingest completion is deterministic under concurrent queued requests.

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
- No `Operational` follow-up is owed for RUNTIME-101; broader file-backed GPU/readback proof requires a future value-gated task outside this ingest-state-machine scope.

## Slice plan
- **Slice A — state machine and taxonomy.** Done locally. Defines the
  ingest request/result state machine (manual import, drag/drop, reimport,
  cancellation, duplicate requests, main-thread apply) and the diagnostics
  taxonomy, with `contract;runtime` transition tests. Defers entry-point
  wiring to Slice B.
- **Slice B — entry-point wiring.** Done locally. Routes
  `Engine::ImportAssetFromPath(...)`, synchronous dropped non-geometry imports,
  and deferred dropped geometry imports through the state machine over existing
  bridges and materialization handoffs. Deferred dropped geometry records
  complete/fail from the `StreamingExecutor` main-thread apply lane; duplicate
  active drops are suppressed before they can materialize a second entity.
- **Slice C — reimport policy, lifecycle drain, and final diagnostic staging.**
  Done locally. Adds same-`AssetId` reimport through `AssetService::Reload`,
  invalid-reimport diagnostics, reload-generation clobber prevention, and final
  materialization-vs-decode diagnostic classification for synchronous
  reimport/manual paths.

## Verification results
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeAssetIngestStateMachine' --timeout 60
```

Result: Slice A runtime contract target built; `RuntimeAssetIngestStateMachine`
CTest passed 7/7.

```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeAssetIngestStateMachine|RuntimeAssetImportFormatCoverage.*Ingest|SandboxEditorUi.*DuplicateDropped|SandboxEditorUi.*DroppedFile|SandboxEditorUi.*PlatformDropEventImports|SandboxEditorUi.*DroppedFileImportFailure' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

Result: Slice B runtime contract target built; focused ingest/import/drop tests
passed 13/13.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Asset|Import|Streaming|SandboxEditorUi' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
bash tools/ci/run_clean_workshop_review.sh . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_root_hygiene.py --root .
python3 tools/repo/check_pr_contract.py
git diff --check
```

Result: `ci` configured with Clang 23, `IntrinsicTests` built, the task
runtime/import filter passed 238/238, clean-workshop automated rows passed, and
the default CPU-supported CTest gate passed 3034/3034. Structural/task/doc,
root-hygiene, PR-contract, and whitespace checks passed.

## Clean-workshop scorecard
- Rows 1-2: pass via `bash tools/ci/run_clean_workshop_review.sh . --strict`.
- Row 3: pass. The new runtime public API exports runtime-owned ingest request,
  result, record, transition, and diagnostic types plus lower-layer `Asset` and
  `Core` handles/status values; no lower layer exposes a higher-layer type.
- Rows 4-6: n/a. This task adds no renderer subsystem/member, frame-graph pass,
  string-routed pass, or frame-recipe dependency.
- Row 7: pass. The task retires at `CPUContracted`, and the `## Maturity`
  section records that no `Operational` follow-up is owed for this
  ingest-state-machine scope.
- Row 8: pass. No layering allowlist entries, temporary shims, or migration
  exceptions were added or changed.
