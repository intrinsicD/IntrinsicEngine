---
id: RUNTIME-143
theme: F
depends_on: []
maturity_target: Operational
completed: 2026-07-05
---
# RUNTIME-143 — Multi-subscriber frame-command hook and K-Means decoupling from Engine

## Status
- Retired on 2026-07-05 at `Operational` on local `main`; PR not opened.
- PR/commit: this retirement commit.
- `IRenderer` now exposes a multi-subscriber runtime frame-command hook
  registry; `Engine` owns only a generic runtime GPU participant lifecycle
  registry; `SandboxEditorUi` owns the concrete K-Means GPU queue while
  attached and registers it through that generic seam.

## Goal
- Remove the K-Means-specific wiring from the Engine composition root: the
  renderer's runtime frame-command hook becomes a multi-subscriber registry,
  the K-Means GPU job queue registers through generic seams like any other
  derived GPU job producer, and `Engine`'s public API loses its
  K-Means-specific methods.

## Non-goals
- No change to K-Means GPU job behavior, parity, or telemetry (BUG-053
  semantics preserved).
- No relocation of the K-Means editor panel (that is `ARCH-006`).
- No generalization of `DerivedJobGraph` to GPU domains beyond what this
  queue needs — coordinate with `RUNTIME-129`'s GPU-lane decision (its open
  question about a render-thread GPU queue vs a `GpuGraphics` job domain);
  whichever seam that task lands, this queue should ride the same one.

## Context
- Owner/layer: `runtime` (Engine composition, `Runtime.KMeansGpuJobQueue`)
  and `graphics/renderer` (hook surface).
- Before this task, the composition root hardcoded one method:
  `Engine` owns `std::unique_ptr<RuntimeKMeansGpuJobQueue> m_KMeansGpuJobs`
  and public `SubmitKMeansGpuJob`/`ConsumeCompletedKMeansGpuJob`
  (`src/runtime/Runtime.Engine.cppm:36, 505-508, 566`); it monopolizes the
  renderer's single `SetRuntimeFrameCommandHook` slot
  (`src/runtime/Runtime.Engine.cpp:2475-2480`) — any app that sets the hook
  silently disconnects K-Means; it drains K-Means transfers explicitly in
  maintenance Phase 10 (`:3010-3011`) and has a K-Means-specific shutdown
  `WaitIdle` ordering (`:2588-2594`, BUG-054).
- Origin: `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  finding R2.

## Required changes
- [x] Replace `IRenderer::SetRuntimeFrameCommandHook` (single slot) with a
      registration API (add/remove, deterministic invocation order,
      documented recording-phase contract); update the frame-graph doc if
      the public surface changes.
- [x] Add a generic Engine seam for per-frame GPU job producers: register a
      command-recording participant + a maintenance-phase drain + a
      shutdown-ordering participant (the three touchpoints K-Means needs
      today), owned by the feature, not enumerated by Engine.
- [x] Move `RuntimeKMeansGpuJobQueue` construction/registration out of
      Engine internals to the feature's composition site; delete the
      K-Means members and public methods from `Engine`; route the sandbox
      panel through the generic seam.
- [x] Preserve BUG-054's shutdown guarantee (queue resources outlive the
      device-idle wait) through the generic shutdown-ordering contract, not
      a hardcoded special case.

## Tests
- [x] Contract: two registered frame-command participants both record each
      frame in deterministic order; unregistering one does not disturb the
      other.
- [x] Contract: K-Means GPU jobs submit/complete identically through the
      generic seam (existing BUG-053 coverage keeps passing).
- [x] Contract: shutdown with in-flight K-Means jobs stays clean
      (BUG-054 regression keeps passing).

## Docs
- [x] Update `src/runtime/README.md` and
      `src/graphics/renderer/README.md` hook-registry contracts.
- [x] Regenerate `docs/api/generated/module_inventory.md` for changed
      `.cppm` surfaces.

## Acceptance criteria
- [x] `Runtime.Engine.cppm`/`.cpp` contain no K-Means identifiers
      (grep-clean), and no single-slot frame hook remains.
- [x] K-Means sandbox behavior unchanged end-to-end.
- [x] Default CPU gate green; layering gate green.

## Notes
- 2026-07-05: Replaced the renderer's single runtime frame-command hook with
  `RegisterRuntimeFrameCommandHook(...)` /
  `UnregisterRuntimeFrameCommandHook(...)`, preserving command-context
  borrowing and adding deterministic registration-order invocation.
- 2026-07-05: Added `Engine::RegisterRuntimeGpuJobParticipant(...)` and
  `UnregisterRuntimeGpuJobParticipant(...)` as a domain-free lifecycle seam
  for frame command recording, maintenance transfer drains, in-flight checks,
  and post-device-idle teardown.
- 2026-07-05: Moved `RuntimeKMeansGpuJobQueue` ownership into
  `SandboxEditorUi::Attach(...)`. The attached editor registers the queue as a
  runtime GPU participant and exposes the same submit/consume command surface
  to K-Means UI commands. `Runtime.Engine.cppm` and `.cpp` are grep-clean for
  `KMeans`.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicGraphicsContractTests
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
cmake --build --preset ci --target IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeEngineLayering\.(ShutdownWaitsIdleBeforeDestroyingRuntimeGpuJobQueue|ShutdownWaitsIdleBeforeDestroyingRuntimeGpuParticipants)|RendererFrameLifecycle\.(RuntimeFrameCommandHookRunsInsideExecuteFrameCommandContext|RuntimeFrameCommandHooksRunInsideExecuteFrameCommandContext)|SandboxEditorUi\.KMeans(VulkanRequestFallsBackToCpuReference|VulkanRequestQueuesGpuJobWhenSurfaceAccepts|CpuRequestQueuesDerivedJobAndPublishesOnApply)' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_pr_contract.py
git diff --check
python3 tools/repo/check_root_hygiene.py --root .
```

`check_root_hygiene.py` remains warning-mode only and reported the
pre-existing `ara/` and `imgui.ini` root entries.

## Forbidden changes
- Breaking the BUG-053/BUG-054 guarantees.
- Giving graphics-layer code knowledge of K-Means or any specific job
  producer (the registry stays type-blind).
- Growing Engine public API with another method-specific surface.
