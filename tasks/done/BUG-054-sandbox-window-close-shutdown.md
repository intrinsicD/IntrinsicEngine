---
id: BUG-054
theme: F
depends_on: []
completed: 2026-07-02
---
# BUG-054 — Sandbox window close shutdown ordering

## Goal
- Ensure a sandbox window close request stops `Engine::Run()`, emits an `[INFO]` runtime breadcrumb, and tears down runtime-owned GPU K-Means resources only after in-flight GPU work is idle.

## Non-goals
- Replacing the current K-Means GPU reduction strategy.
- Adding force-termination for already-running CPU streaming tasks.
- Changing platform backend selection.

## Context
- Owner layer: `runtime`, with existing platform event input and graphics/RHI resource ownership boundaries.
- The sandbox close button enters runtime as `Platform::WindowCloseEvent`; runtime must log the user action and stop the engine loop.
- `RuntimeKMeansGpuJobQueue` owns compute pipeline handles and buffer leases used by frame-command work, so shutdown must detach the hook and wait for the device to go idle before destroying that queue.

## Completion
- Completed: 2026-07-02. Commit/PR: this local fix commit.
- Root cause: the close path did not leave an explicit runtime `[INFO]` breadcrumb, and shutdown destroyed the runtime K-Means GPU job queue before the first device-idle wait even though that queue owns GPU resources used by frame-command work.
- Fix summary: runtime close events and native close polling now route through a one-shot close helper that logs at `Info` level and requests engine exit. Shutdown detaches the renderer frame-command hook, waits for the device to go idle when the runtime GPU job queue exists, then destroys the queue before renderer/device teardown continues.

## Required changes
- [x] Add a one-shot runtime close-request log at `Info` level for platform close events and native close polling.
- [x] Keep the runtime K-Means GPU job queue alive until after the explicit device-idle wait during shutdown.
- [x] Preserve the existing close-before-render early-out.

## Tests
- [x] Add/extend a runtime contract test that verifies a platform close event logs an `Info` close breadcrumb and stops `Engine::Run()`.
- [x] Add a shutdown-ordering contract so runtime GPU job resources are not destroyed before the idle wait.

## Docs
- [x] Record the bug and fix in this done task and the active bug index.

## Acceptance criteria
- [x] Clicking the sandbox window close button routes through runtime close handling.
- [x] The close action is visible in logs as an `[INFO]` runtime message.
- [x] Pending K-Means GPU frame work cannot have its queue-owned GPU resources destroyed before the device idle wait.
- [x] Focused runtime tests pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi.*PlatformCloseEventStopsEngineRunState' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeEngineLayering\.(RunFrameStopsAfterPlatformCloseBeforeRendererContract|ShutdownWaitsIdleBeforeDestroyingRuntimeGpuJobQueue)' --timeout 60
cmake --build --preset dev --target ExtrinsicSandbox
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
