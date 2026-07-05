---
id: RUNTIME-141
theme: F
depends_on: []
---
# RUNTIME-141 — Async editor method-command lane (no heavy compute in the ImGui callback)

## Status
- Active on 2026-07-05; Slices A, B, and C are implemented and verified. The
  parent task remains active for the remaining synchronous geometry-processing
  commands identified by the Slice D inventory.
- This task is intentionally sliced because it spans the shared runtime job
  lane plus several method-specific snapshot/apply conversions.
- Remaining open slices: mesh curvature, mesh subdivide, mesh/graph/point-cloud
  vertex normals, point-cloud outlier removal, and UV regeneration.

## Slice plan
- **Slice A (this slice).** Wire an engine-owned `DerivedJobRegistry` beside
  `StreamingExecutor`, expose a sandbox editor derived-job submission surface,
  and convert the CPU K-Means button path to queue a worker job from copied
  point data with main-thread publication. Tests prove the ImGui command
  returns `Pending` before the worker body runs, the result applies later, and
  stale targets are discarded before mutation. Defers Progressive Poisson,
  denoise/remesh/simplify, and registration conversions to later slices.
- **Slice B (complete).** Convert Progressive Poisson CPU sampling, including
  mesh-surface sampling, to the shared editor method job lane.
- **Slice C (complete).** Convert mesh denoise/remesh/simplify command
  handlers to the shared lane with copied mesh snapshots and generation/stale
  guards.
- **Slice D (complete).** Convert registration alignment and update the
  heavyweight editor button inventory.
- **Slice E.** Convert or split the remaining synchronous geometry-processing
  commands identified by the inventory: mesh curvature, mesh subdivide,
  mesh/graph/point-cloud vertex normals, point-cloud outlier removal, and UV
  regeneration.

## Goal
- Editor-triggered heavy action buttons and method runs (CPU K-Means,
  Progressive Poisson, denoise, remesh, simplify, registration, and similar
  geometry/method commands) enqueue runtime-owned async jobs/tasks through the
  existing `StreamingExecutor`/`DerivedJobRegistry` machinery with
  generation-keyed main-thread applies, then return to the frame loop without
  stalling rendering.

## Non-goals
- No changes to method algorithms, outputs, or parameters.
- The async GPU readback helper is owned by `RUNTIME-137`; Progressive
  Poisson GPU parity and its `ReadBuffer`/`vkDeviceWaitIdle` drain are owned
  by `METHOD-014` (+ `RUNTIME-137` adoption). This task owns the *CPU
  command lane* only.
- Selected-entity model/inspector derivations are owned by `RUNTIME-138`;
  this task owns explicit method-run commands.
- No new scheduler (reuse `StreamingExecutor`/`DerivedJobRegistry`).
- Lightweight UI state changes, parameter edits, selection/gizmo commands, and
  other frame-critical cheap commands may remain immediate when they are proven
  not to run heavy compute or blocking IO.

## Context
- Owner/layer: `runtime` (`Runtime.SandboxEditorUi` command handlers,
  `Runtime.StreamingExecutor`, `Runtime.DerivedJobGraph`).
- User-reported bug (2026-07-05): UI buttons that trigger expensive work should
  create async tasks/jobs and must not stall or block rendering while the work is
  running.
- Today editor commands run method compute inline inside the per-frame ImGui
  editor callback: `RunKMeansForSandbox`
  (`src/runtime/Editor/Runtime.SandboxEditorUi.cpp:4286-4308`),
  `RunProgressivePoissonAndPublish` → synchronous `PPR::Compute`
  (`:4789-4804`), and the denoise/remesh/simplify handlers (`:16260, 16367`).
  A long solve freezes the frame for its full duration. The K-Means *GPU*
  queue and `AsyncBufferReadback` already demonstrate the intended
  poll-based pattern.
- The ECS registry is single-threaded: workers must consume
  generation-stamped immutable snapshots captured on the main thread; stale
  results are discarded on apply (same model as `RUNTIME-138`).
- Origin: `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  finding R10.

## Control surfaces
- UI: method panels show pending/running/ready/stale/failed job state and
  keep a cancel/re-run affordance; results apply when ready.
- Agent/CLI: existing method command surfaces keep working; completion is
  observable through the same job state.
- Config: none new.

## Required changes
- [ ] Inventory Sandbox editor action buttons and classify each heavyweight
      command as already queued, lightweight/immediate, or still synchronously
      expensive.
- [ ] Introduce a shared "editor method/action job" submission helper over
      `StreamingExecutor`/`DerivedJobRegistry`: snapshot inputs on the main
      thread, run compute on the worker lane, apply results on the main
      thread with generation checks and bounded per-frame apply budget.
- [x] Make converted button handlers return a queued/pending status and job id
      or diagnostics immediately, without executing the heavy body inline.
- [x] Convert the CPU K-Means editor command to the helper (parity with the
      existing GPU-queue UX: same published label/color properties).
- [x] Convert Progressive Poisson CPU runs to the helper.
- [x] Convert denoise/remesh/simplify commands to the helper.
- [x] Convert registration alignment commands to the helper.
- [ ] Panels reflect job state instead of blocking; a second submit while
      one runs either queues or replaces per current UX expectations
      (document choice per panel).

## Tests
- [x] Contract: a representative converted UI button creates a pending job and
      returns before the heavy compute body runs.
- [x] Contract: a submitted method job runs off the main thread, applies its
      result on a later frame, and the applied output equals the previous
      synchronous output for a fixed seed/scene (per converted command).
- [x] Contract: a stale job (scene/geometry generation changed mid-run) is
      discarded without mutating state.
- [ ] Contract: the ImGui editor callback duration stays bounded while a
      heavy job runs (timing probe with a deliberately slow job).
- [ ] Contract: render extraction/prepare can advance while an editor method
      job is pending.
- [x] Existing method/editor command suites stay green.

## Docs
- [x] Update `src/runtime/README.md` editor-command execution model.

## Acceptance criteria
- [ ] Heavy editor UI buttons create runtime jobs/tasks instead of executing
      solves or blocking IO inside the ImGui callback.
- [ ] No editor method command executes its solve inside the ImGui callback
      (verified per converted command by the queued-status and timing
      contracts).
- [ ] Rendering remains advanceable while converted jobs are pending; only the
      bounded main-thread apply phase may mutate committed state.
- [ ] Method outputs unchanged for deterministic fixtures.
- [ ] Default CPU gate green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|EditorMethodJob|DerivedJob|StreamingExecutor' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

Slice A verification completed on 2026-07-05:

```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='RuntimeDerivedJobEngineWiring.RunFrameAppliesSubmittedDerivedJob:RuntimeEngineLayering.StreamingHookAppliesMainThreadResultsWithFrameBudget:SandboxEditorUi.KMeansCpuRequestQueuesDerivedJobAndPublishesOnApply:SandboxEditorUi.KMeansCpuDerivedJobDiscardsStaleTargetBeforeApply:SandboxEditorUi.KMeansVulkanRequestQueuesGpuJobWhenSurfaceAccepts:SandboxEditorUi.KMeansVulkanRequestFallsBackToCpuReference:SandboxEditorUi.KMeansCommandPublishesMeshGraphAndPointCloudProperties'
ctest --test-dir build/ci --output-on-failure -R 'RuntimeEngineLayering' --timeout 120
ctest --test-dir build/ci --output-on-failure -R 'RuntimeEngineLayering|RuntimeDerivedJobEngineWiring|SandboxEditorUi|DerivedJob|StreamingExecutor|RuntimeSceneLifecycle' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/check_pr_contract.py
git diff --check
python3 tools/repo/check_root_hygiene.py --root .
```

Slice B verification completed on 2026-07-05:

```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='SandboxEditorUi.ProgressivePoissonCpuRequestQueuesDerivedJobAndPublishesOnApply:SandboxEditorUi.ProgressivePoissonCpuDerivedJobDiscardsStalePointCloudBeforeApply:SandboxEditorUi.ProgressivePoissonMeshCpuRequestQueuesDerivedJobAndPublishesOnApply:SandboxEditorUi.ProgressivePoissonCommandPublishesPointPropertiesAndVisualization:SandboxEditorUi.ProgressivePoissonCommandMatchesDirectMethodConfig:SandboxEditorUi.ProgressivePoissonCommandSamplesMeshSurfaceToPointCloud:SandboxEditorUi.KMeansCpuRequestQueuesDerivedJobAndPublishesOnApply:SandboxEditorUi.KMeansCpuDerivedJobDiscardsStaleTargetBeforeApply'
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|DerivedJob|StreamingExecutor|RuntimeSceneLifecycle' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

Slice C verification completed on 2026-07-05:

```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='SandboxEditorUi.MeshDenoiseRequestQueuesDerivedJobAndPublishesOnApply:SandboxEditorUi.MeshDenoiseDerivedJobDiscardsStaleMeshBeforeApply:SandboxEditorUi.MeshRemeshRequestQueuesDerivedJobAndPublishesOnApply:SandboxEditorUi.MeshSimplifyRequestQueuesDerivedJobAndPublishesOnApply:SandboxEditorUi.MeshDenoiseCommandPublishesPositionsAndSupportsUndoRedo:SandboxEditorUi.MeshRemeshCommandReplacesTopologyAndSupportsUndoRedo:SandboxEditorUi.MeshSimplifyCommandReducesFaceCountAndSupportsUndoRedo:SandboxEditorUi.MeshSimplifyPreservesUvSeamsWhenTexcoordsPresent'
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|DerivedJob|StreamingExecutor|RuntimeSceneLifecycle' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

Slice D verification completed on 2026-07-05:

```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='SandboxEditorUi.RegistrationRequestQueuesDerivedJobAndPublishesOnApply:SandboxEditorUi.RegistrationDerivedJobDiscardsStaleSourceBeforeApply:SandboxEditorUi.RegistrationCommandAlignsSourceOntoTargetAndSupportsUndoRedo:SandboxEditorUi.RegistrationCommandFailsClosedForInvalidSelectionAndParameters:SandboxEditorUi.RegistrationCommandAlignsAcrossEntityTransforms'
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|DerivedJob|StreamingExecutor|RuntimeSceneLifecycle' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
```

## Heavy Button Inventory
- Queued through `DerivedJobRegistry` when an engine job surface is available:
  CPU K-Means; Progressive Poisson CPU point-cloud and mesh-surface sampling;
  mesh denoise/remesh/simplify; ICP registration alignment.
- Already routed through another async runtime command surface: selected mesh
  texture bake (`Extrinsic.Runtime.SelectedMeshTextureBake` schedules derived
  CPU bake work and stale-checked main-thread apply).
- Immediate/lightweight command surfaces: selection, camera-controller changes,
  render-hint toggles, visualization config/property preset edits, spatial-debug
  binding toggles, vertex-channel binding edits, progressive-slot binding edits,
  render-recipe draft/preview state changes, and undo/redo/document state
  controls that only mutate runtime-owned editor state.
- Still synchronous geometry-processing commands and therefore open under Slice
  E: mesh curvature, mesh subdivide, mesh/graph/point-cloud vertex normal
  recompute, point-cloud outlier removal, and selected mesh UV regeneration.
- File import and scene-file IO are outside this CPU method-command lane and are
  tracked by `RUNTIME-142`.

## Forbidden changes
- Adding a second ad hoc scheduler next to `StreamingExecutor`.
- Worker-thread access to the live ECS registry or renderer state.
- Changing method numerical behavior while moving execution.
- Running heavy geometry/method/IO work directly from ImGui button handlers.
- Letting renderer code own editor job scheduling or editor completion state.

## Maturity
- Target: `Operational` (the sandbox editor path is the subject); CPU gate
  contracts prove the job lifecycle, a sandbox run proves responsiveness.
- This is the canonical task for the 2026-07-05 user-reported bug where UI
  buttons stall/block rendering instead of creating async jobs; no duplicate
  `BUG-*` task is filed unless a narrower repro remains after this task lands.
