---
id: RUNTIME-140
theme: F
depends_on: []
maturity_target: CPUContracted
completed: 2026-07-05
---
# RUNTIME-140 — Remove the global scheduler barrier from the import apply path

## Status
- Retired on 2026-07-05 at `CPUContracted` on local `main`; PR not opened.
- PR/commit: this retirement commit.
- `AssetService::CompleteCpuLoadAndFlushEvent(...)` now gives runtime import
  apply code a per-asset completion and event-flush primitive. The runtime
  materialization paths use that targeted drain instead of
  `Core::Tasks::Scheduler::WaitForAll()`, so an import apply no longer waits
  for unrelated scheduler work.
- `AssetLoadPipeline::OnCpuDecoded(...)` now rejects duplicate in-flight CPU
  decode completion without archiving the load entry, so a direct per-asset
  completion cannot corrupt an already-claimed scheduled completion.
- Regression coverage proves both the direct asset-service completion path and
  `Engine::ImportAssetFromPath` return while an unrelated scheduler sentinel is
  still in flight, while still publishing the imported payload/event.

## Goal
- Eliminate `Core::Tasks::Scheduler::WaitForAll()` from the asset-import
  apply path: an import completion must wait only on its own load, never on
  every in-flight task in the process, so one import cannot stall the frame
  on unrelated background work.

## Non-goals
- No scheduler API redesign (`CORE-005`/`CORE-007` own scheduler changes);
  a per-load completion primitive that already exists (counter/event/state
  machine poll) is preferred over new core surface.
- No change to which imports are synchronous vs streamed — `RUNTIME-142`
  owns moving the remaining synchronous import routes off the main thread.

## Context
- Owner/layer: `runtime` (`Runtime.Engine` import materialization) with
  `assets` (`AssetService`) as the completion-source authority.
- `DrainAssetImportEvents` calls the global `WaitForAll()` then
  `service.Tick()` (`src/runtime/Runtime.Engine.cpp:857-864`). `WaitForAll`
  loops until the scheduler's `inFlightTasks` hits zero
  (`src/core/Core.Tasks.Lifecycle.cpp:56-79`). Call sites reach it from the
  frame path: dropped-geometry streaming applies inside the Phase-10
  maintenance contract (`Runtime.Engine.cpp:3857` →
  `MaterializeDecodedGeometryImport` → `1683/1756/1813`) and editor-triggered
  `ImportAssetFromPath` inside the ImGui callback (`4321, 4370, 4445, 4477`).
  Because the same scheduler also runs sim passes and other streaming
  decodes, one import apply stalls the frame on all of them.
- The intent of the barrier is "make sure this asset's async load finished
  before Tick observes it" — a per-asset wait, not a global fence.
- Origin: `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  finding R9.

## Required changes
- [x] Identify what each `DrainAssetImportEvents` call site actually needs
      to observe (which pending load / which event) and replace the global
      barrier with a targeted wait: per-load completion state on
      `AssetService` (poll the specific `AssetId`/load ticket) or a bounded
      tick-until-event with the specific load's completion as the condition.
- [x] Where the caller does not need synchronous completion at all (event
      consumers that could observe the result next frame), drop the wait and
      let the normal maintenance `AssetService::Tick` deliver the event.
- [x] Delete `DrainAssetImportEvents`'s `WaitForAll` usage; add a
      lint/grep-style guard note in `src/runtime/README.md` that
      `Scheduler::WaitForAll` is shutdown-only in the frame path.

## Tests
- [x] Contract: an import apply completes correctly while an unrelated
      long-running scheduler task is still in flight (frame does not wait
      for it) — provable with a blocked sentinel task.
- [x] Contract: import events still arrive exactly once and in order for
      multi-file drops.
- [x] Existing import/materialization suites (BUG-044 regression among
      them) stay green.

## Docs
- [x] Update `src/runtime/README.md` import-apply notes (per-load waits;
      no global fences in the frame path).

## Acceptance criteria
- [x] No `Scheduler::WaitForAll()` call reachable from `Engine::RunFrame()`
      (verified by test with a sentinel in-flight task).
- [x] Import behavior (events, materialized results) unchanged.
- [x] Default CPU gate green.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeIntegrationTests
cmake --build --preset ci --target IntrinsicRuntimeContractTests
build/ci/bin/IntrinsicRuntimeIntegrationTests --gtest_filter='AssetService.CompleteCpuLoadAndFlushEventIgnoresUnrelatedSchedulerWork'
build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='RuntimeAssetImportFormatCoverage.ImportAssetFromPathDoesNotWaitForUnrelatedSchedulerWork'
ctest --test-dir build/ci --output-on-failure -R 'AssetService|AssetLoadPipeline|RuntimeAssetImportFormatCoverage|SandboxEditorUi.*(Import|Dropped|Reimport)|RuntimeAssetModel(Scene|Texture)Handoff' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
build/ci/bin/IntrinsicRuntimeIntegrationTests --gtest_filter='AssetService.*:AssetLoadPipeline.*'
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes
- Replacing the global barrier with sleeps/polling loops on unrelated state.
- Blocking asset import correctness on timing (waits must key on the
  specific load's completion, fail-closed on load error).
- Scheduler core changes.

## Maturity
- Target: `CPUContracted`. The endpoint is the runtime/assets frame-path
  contract for per-asset import completion under the default CPU/null backend;
  no `Operational` follow-up is owed because the defect was not
  backend-specific.
