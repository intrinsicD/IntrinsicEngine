---
id: RUNTIME-160
theme: F
depends_on: [RUNTIME-159]
maturity_target: Operational
completed: 2026-07-09
---
# RUNTIME-160 — Extract JobService GPU queue bridge out of Engine

## Goal
- Move the renderer frame-command hook token, JobService GPU-queue command-recording bridge, and GPU-participant shutdown ordering out of `Runtime.Engine.cppm` / `Runtime.Engine.cpp` into a focused runtime module while preserving the existing Engine-owned lifecycle order.

## Non-goals
- Changing `Extrinsic.Runtime.JobService`, `GpuQueueParticipantDesc`, participant registration/unregistration semantics, completion draining, or world-scoped cancellation.
- Changing `Extrinsic.Graphics.IRenderer::RegisterRuntimeFrameCommandHook(...)` / `UnregisterRuntimeFrameCommandHook(...)` behavior.
- Changing object-space normal bake queue submission, K-Means GPU participant behavior, renderer command recording, or Vulkan/backend execution policy.
- Changing frame phase ordering, maintenance timing, shutdown contract ordering, or device-idle semantics.

## Context
- Owner: `runtime`; this is runtime composition glue between the domain-free `JobService` GPU-queue participant registry and the renderer's frame-command hook.
- `Runtime.Engine.cppm` currently stores `Graphics::RuntimeFrameCommandHookHandle m_JobServiceGpuQueueHook` and declares `InstallJobServiceGpuQueueFrameHook()`, `UninstallJobServiceGpuQueueFrameHook()`, and `ShutdownJobServiceGpuQueueParticipants()`.
- `Runtime.Engine.cpp` currently registers a renderer frame-command hook that calls `JobService::RecordGpuQueueFrameCommands(...)`, unregisters the hook on shutdown, then calls `JobService::ShutdownGpuQueueParticipants(...)` with a device-idle wait callback.
- This follows the `RUNTIME-146` through `RUNTIME-159` decomposition pattern: `Engine` remains the composition root and lifecycle caller, but subsystem-local hook ownership and shutdown sequencing live behind a runtime-owned module.

## Required changes
- [x] Add `Extrinsic.Runtime.JobServiceGpuQueueBridge` owning the renderer runtime-frame hook handle.
- [x] Move renderer hook install/uninstall and `JobService::RecordGpuQueueFrameCommands(...)` delegation behind the bridge.
- [x] Move GPU-participant shutdown sequencing behind the bridge while preserving the order: detach renderer hook, then ask `JobService` to shut down participants with the Engine-provided device-idle wait callback.
- [x] Update `Runtime.Engine.cppm` to store the bridge instead of the raw graphics hook handle and remove the Engine-private GPU-queue helper declarations.
- [x] Update `Runtime.Engine.cpp` so `Initialize()` and `Shutdown()` delegate through the bridge.
- [x] Add the new module to `src/runtime/CMakeLists.txt`.

## Tests
- [x] Add or update runtime source-contract coverage proving the graphics hook token and GPU-queue lifecycle helper bodies no longer live in `Runtime.Engine.cppm` / `Runtime.Engine.cpp`.
- [x] Preserve runtime JobService GPU-participant tests and Engine layering coverage.
- [x] Run the default CPU-supported correctness gate before retirement.

## Docs
- [x] Update `src/runtime/README.md` to document `Extrinsic.Runtime.JobServiceGpuQueueBridge` and revise the `Engine` / `JobService` current-state wording.
- [x] Update `tasks/backlog/runtime/README.md` with the factual decomposition state.
- [x] Update `tasks/backlog/README.md` if the Theme F Engine-decomposition summary changes.
- [x] Regenerate `docs/api/generated/module_inventory.md`.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening and after retirement.

## Acceptance criteria
- [x] `Runtime.Engine.cppm` no longer imports or stores `Graphics::RuntimeFrameCommandHookHandle`.
- [x] `Runtime.Engine.cpp` no longer calls `RegisterRuntimeFrameCommandHook(...)`, `UnregisterRuntimeFrameCommandHook(...)`, or `JobService::RecordGpuQueueFrameCommands(...)` directly.
- [x] Existing behavior remains unchanged: GPU queue participants record inside the renderer's open frame command context, shutdown detaches the hook before participant teardown, and participant resource release still happens after the Engine-provided idle wait.
- [x] Strict task, docs, layering, and test-layout checks pass, aside from pre-existing warning-mode root/task-state findings if unchanged by this slice.

## Verification
```bash
python3 tools/agents/generate_session_brief.py
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeJobService|JobServiceGpuQueueBridge|RuntimeEngineLayering|ObjectSpaceNormalBakeGpuQueue' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
cmake --build --preset ci --target IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeEngineLayering' --timeout 180
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_state_links.py --root .
python3 tools/repo/check_root_hygiene.py --root .
git diff --check
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Changing JobService participant registry semantics, renderer frame-hook invocation order, object-space normal bake behavior, or K-Means GPU participant behavior.
- Changing frame phase ordering, maintenance timing, shutdown contract ordering, or device bootstrap/backend policy.
- Introducing a generic abstraction that is not exercised by the current renderer + JobService bridge.
- Reverting unrelated dirty worktree changes.

## Maturity
- Target: `Operational`.
- This slice closes at `Operational` when the live Engine initialize/shutdown path delegates GPU-queue bridge ownership to the new runtime module and focused JobService/layering tests plus the default CPU gate pass.

## Status
- Retired on 2026-07-09 at maturity `Operational`.
- `Extrinsic.Runtime.JobServiceGpuQueueBridge` now owns the renderer runtime-frame hook token, installs the JobService GPU-queue command-recording bridge, detaches the hook before participant teardown, and forwards the Engine-owned device-idle wait callback into participant shutdown.
- `Runtime.Engine` keeps lifecycle ordering, renderer/device ownership, and JobService ownership; raw frame-hook storage and direct `JobService::RecordGpuQueueFrameCommands(...)` delegation no longer live in `Runtime.Engine.cppm` / `.cpp`.
- Verification passed:
  - `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
  - `ctest --test-dir build/ci --output-on-failure -R 'RuntimeJobService|JobServiceGpuQueueBridge|RuntimeEngineLayering|ObjectSpaceNormalBakeGpuQueue' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180` (12/12)
  - `cmake --build --preset ci --target IntrinsicRuntimeIntegrationTests`
  - `ctest --test-dir build/ci --output-on-failure -R 'RuntimeEngineLayering' --timeout 180` (19/19)
  - `cmake --build --preset ci --target IntrinsicTests`
  - `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` (3646/3646)
  - strict task/docs/layering/test-layout validators
- Warning-mode checks remain unchanged: `check_task_state_links.py` still reports retired `ARCH-007`..`ARCH-013` links in `tasks/backlog/architecture/README.md`, and `check_root_hygiene.py` still reports root entries `ara/` and `imgui.ini`.
- PR/commit: pending.
