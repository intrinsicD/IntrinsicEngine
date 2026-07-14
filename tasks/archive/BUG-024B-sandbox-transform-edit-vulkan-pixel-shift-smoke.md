---
id: BUG-024B
theme: G
depends_on: [BUG-024]
maturity_target: Operational
---
# BUG-024B — Vulkan pixel-shift smoke for sandbox transform edits

## Goal

- Prove the BUG-024 fix `Operational` on a Vulkan-capable host: an opt-in `gpu;vulkan` smoke edits the `ReferenceTriangle` local position through the promoted UI/editor command path and observes the rendered pixels move.

## Non-goals

- No changes to the runtime pre-render transform flush shipped by `BUG-024` unless the smoke exposes a real defect.
- No shader or pipeline changes unless the smoke fails with correct CPU snapshot state (BUG-024 hypothesis 3/4 territory; open a focused follow-up if that happens).
- No CPU-gate test changes; the CPU/null contract is owned and closed by `BUG-024`.
- No new editor/UI features.

## Context

- `BUG-024` (done) fixed the stale-`WorldMatrix` path by adding `FlushPreRenderTransformState` to `Engine::RunFrame()` and closed at `CPUContracted` with engine-level and extraction-level CPU regression coverage. The Vulkan operational proof was explicitly deferred to this task per the BUG-024 Maturity statement.
- The smoke landed in `tests/integration/runtime/Test.RuntimeSandboxAcceptanceGpuSmoke.cpp` (`InspectorTransformEditShiftsReferenceTrianglePixels`) next to the existing default-recipe present smokes, using the existing readback helpers. A new `EditTriangleViaInspectorApp` applies `ApplySandboxEditorTransformEdit` through the live `EditorCommandHistory` path on a mid-run frame (after that frame's fixed-step bundle), and `BootstrapDefaultSandboxAppEngine` was refactored onto `BootstrapDefaultSandboxAppEngineWithApp` so the smoke could supply the custom app. A `ProjectReferenceCameraPixel` helper mirrors the reference camera projection (position (0,0,3), fovy 45°, Vulkan Y-flip) to locate the shifted triangle sample analytically for any backbuffer extent.
- The edit goes through the UI/editor command path — not the manual `SetEntityPosition` helper that writes `Transform::WorldMatrix` directly (forbidden by BUG-024).
- The bounded high-fps run schedules effectively no fixed substeps after the mid-run edit, so without the BUG-024 pre-render flush the triangle would keep rendering at the origin and the smoke would fail — it is a genuine operational regression gate for the flush.
- Completed: 2026-06-10.
- PR/commit: pending local commit.

## Required changes

- [x] Add an opt-in `gpu;vulkan;runtime` smoke in `Test.RuntimeSandboxAcceptanceGpuSmoke.cpp` (target labels `gpu vulkan integration runtime graphics`) that renders the default `ReferenceTriangle`, applies a UI/editor-command transform edit (+1.5 world X) large enough to move the triangle out of the original sample location, runs 8 bounded frames for the pre-render flush plus swapchain latency, and reads back pixels.
- [x] Assert the original triangle sample location (frame center) returns to the background color and the expected shifted sample location (projected shifted centroid) contains the triangle color; also assert the live-engine CPU contract (world matrix at the edited position, `IsDirtyTag` cleared) and recorded `SurfacePass`/`Present`.
- [x] Pixels moved on the operational host, so no pass-diagnostics escalation was needed.

## Tests

- [x] The new smoke runs under `ctest --test-dir build/ci-vulkan -L gpu -L vulkan` and is excluded from the default CPU gate by its labels.
- [x] Existing `RuntimeSandboxAcceptanceGpuSmoke` cases keep passing on the same host/run (6/6 passed).

## Docs

- [x] Recorded the executed Vulkan verification (host/device, pass counts) in this task file on retirement.
- [x] Updated `tasks/backlog/bugs/index.md` and the retirement log on closing.

## Acceptance criteria

- [x] The pixel-shift smoke passes on a Vulkan-operational host and is cited in `Verification` as actually run.
- [x] BUG-024's fix maturity is upgraded to `Operational` by this task's retirement narrative.

## Verification

Commands actually run for retirement (2026-06-10), host NVIDIA GeForce RTX 3050, driver 590.48.01:

```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'RuntimeSandboxAcceptanceGpuSmoke.InspectorTransformEditShiftsReferenceTrianglePixels' -L 'gpu' -L 'vulkan' --timeout 120   # 1/1 passed
ctest --test-dir build/ci-vulkan --output-on-failure -R 'RuntimeSandboxAcceptanceGpuSmoke' -L 'gpu' -L 'vulkan' --timeout 120   # 6/6 passed
```

## Forbidden changes

- Do not write `Transform::WorldMatrix` or GPU instance buffers directly from the test to make pixels move.
- Do not weaken or skip the existing CPU regression coverage from `BUG-024`.
- Do not change default-recipe shaders/pipelines without a failing smoke plus recorded diagnostics.

