---
id: RUNTIME-165
theme: F
depends_on: [RUNTIME-164]
maturity_target: Operational
completed: 2026-07-09
---
# RUNTIME-165 — Extract async work service out of Engine

## Status
- Retired on 2026-07-09 at maturity `Operational`.
- `Extrinsic.Runtime.AsyncWorkService` owns the live `StreamingExecutor` and
  `DerivedJobRegistry`, constructs the registry over the executor, and
  centralizes completion/readback drains, count-limited main-thread apply,
  background pumping, shutdown draining, reset ordering, and derived-job facade
  delegation.
- `Runtime.Engine` keeps lifecycle/frame ordering and dependent subsystem
  wiring while no longer importing or storing the raw streaming executor /
  derived-job registry. The private frame-loop partition delegates bounded
  maintenance through the service.
- Verification passed: focused runtime async/job/import/scene/layering coverage
  passed 56/56, `IntrinsicTests` built, and the default CPU-supported CTest
  gate passed 3646/3646. Strict task/docs/layering/test-layout checks passed;
  clean-workshop automated rows passed, and warning-mode root/task-state
  findings were pre-existing and unchanged.
- PR/commit: pending.

## Goal
- Move `StreamingExecutor` and `DerivedJobRegistry` ownership, maintenance
  drains, public derived-job facade delegation, and shutdown/reset ordering out
  of `Runtime.Engine.cppm` / `Runtime.Engine.cpp` into a focused runtime async
  work service. Keep `Engine` as the lifecycle and dependency-wiring root.

## Non-goals
- Changing `StreamingExecutor`, `DerivedJobRegistry`, derived-job scheduling,
  readback parking/resume, main-thread apply budget, cancellation semantics, or
  diagnostics.
- Moving `AssetImportPipeline`, `SceneDocument`, `JobService`, or
  `Core::FrameLoop` ownership.
- Adding a second scheduler, worker pool, async API, or GPU execution path.

## Context
- Owner: `runtime`; this is composition-root cleanup around existing runtime
  background work modules.
- After `RUNTIME-164`, `Runtime.Engine.cppm` still directly imports and stores
  `StreamingExecutor` and `DerivedJobRegistry`.
- `Runtime.Engine:FrameLoop` still has a maintenance hook that knows both
  concrete async-work types and contains their drain/apply/pump branch policy.
- `Runtime.Engine.cpp` still constructs those concrete members, passes raw
  executor pointers into import/document subsystems, performs shutdown/drain
  ordering with raw unique pointers, and delegates the public derived-job
  facade directly to `DerivedJobRegistry`.

## Required changes
- [x] Add `Extrinsic.Runtime.AsyncWorkService` under `src/runtime/` owning
  `StreamingExecutor` and `DerivedJobRegistry`.
- [x] Move async maintenance methods (`DrainCompletions`,
  count-limited `ApplyMainThreadResults`, `PumpBackground`), shutdown/drain,
  reset, and derived-job facade delegation behind the service.
- [x] Update `Runtime.Engine.cppm` to store the service instead of raw
  streaming/derived-job members and to stop directly importing
  `Runtime.StreamingExecutor` / `Runtime.DerivedJobGraph`.
- [x] Update `Runtime.Engine.cpp` and `Runtime.Engine:FrameLoop` so
  initialization, import/document dependency wiring, maintenance hooks,
  shutdown, and public derived-job facade methods delegate through the service.
- [x] Add the new module to `src/runtime/CMakeLists.txt`.

## Tests
- [x] Add/update runtime source-contract coverage proving streaming executor
  and derived-job registry ownership/maintenance branch policy no longer live
  in `Runtime.Engine.cppm`, `Runtime.Engine.cpp`, or the frame-loop partition.
- [x] Preserve runtime derived-job, streaming executor, scene lifecycle, asset
  import, and Engine layering coverage.
- [x] Run the default CPU-supported correctness gate before retirement.

## Docs
- [x] Update `src/runtime/README.md` to document
  `Extrinsic.Runtime.AsyncWorkService` and revise Engine/maintenance/shutdown
  current-state wording.
- [x] Update `tasks/backlog/runtime/README.md` with the factual decomposition
  state.
- [x] Update `tasks/backlog/README.md` if the Theme F Engine-decomposition
  summary changes.
- [x] Regenerate `docs/api/generated/module_inventory.md`.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening and retirement.

## Acceptance criteria
- [x] `Runtime.Engine.cppm` no longer stores `m_StreamingExecutor` or
  `m_DerivedJobRegistry` and no longer directly imports
  `Extrinsic.Runtime.StreamingExecutor` or
  `Extrinsic.Runtime.DerivedJobGraph`.
- [x] `Runtime.Engine.cpp` no longer constructs or resets
  `StreamingExecutor`/`DerivedJobRegistry` directly and no longer reaches into
  `DerivedJobRegistry` for submit/cancel/snapshot facade methods.
- [x] `Runtime.Engine.FrameLoop.cppm` no longer imports
  `Runtime.StreamingExecutor` / `Runtime.DerivedJobGraph` or branches between
  executor-only and derived-job maintenance paths; it delegates the existing
  bounded maintenance contract to the async work service.
- [x] Existing behavior remains unchanged: derived-job registry still wraps the
  same streaming executor, readbacks still drain before main-thread apply, the
  per-frame apply budget remains `8`, import/document subsystems still borrow
  the live streaming executor, shutdown still drains before scene/assets reset,
  and derived-job facade calls remain fail-closed before initialization.
- [x] Strict task, docs, layering, and test-layout checks pass, aside from
  pre-existing warning-mode root/task-state findings if unchanged by this
  slice.

## Verification
```bash
python3 tools/agents/generate_session_brief.py
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeEngineLayering|RuntimeDerivedJobEngineWiring|RuntimeStreamingExecutor|DerivedJob|StreamingExecutor|RuntimeAssetImportFormatCoverage|RuntimeSceneLifecycle' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
tools/ci/run_clean_workshop_review.sh . --strict
python3 tools/agents/check_task_state_links.py --root .
python3 tools/repo/check_root_hygiene.py --root .
git diff --check
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Changing async scheduling behavior, task priorities, readback state
  transitions, apply validation, cancellation, diagnostics, or worker-thread
  lifecycle semantics.
- Changing import/reimport behavior, scene-document IO behavior, render
  contract order, or `Core::IStreamingFrameHooks`.
- Reverting unrelated dirty worktree changes.

## Maturity
- Target: `Operational` for this cleanup slice.
- This slice closes at `Operational` when live Engine initialization,
  maintenance, derived-job facade calls, and shutdown delegate async-work owner
  state to the new runtime service and focused async/layering coverage plus the
  default CPU gate pass.
