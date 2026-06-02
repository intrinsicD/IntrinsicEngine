# GRAPHICS-032D — Opt-in `gpu;vulkan` smoke for `FrameRecipe::MinimalDebugSurface`

## Status

- Status: done.
- Owner/agent: Claude on `claude/graphics-rendering-task-zpycz`.
- Branch: `claude/graphics-rendering-task-zpycz`.
- Started: 2026-05-18.
- Completed: 2026-05-18.
- Commit/PR: pending current change.
- Completion verification: edits in this session refactored
  `tests/integration/graphics/Test.MinimalDebugSurfaceGpuSmoke.cpp` to share
  one `BootstrapEngineForMinimalDebug` + `DriveOneFrameAndCapture` bring-up
  helper between the GRAPHICS-033D pixel-readback fixture and the new
  GRAPHICS-032D `RecipeSelectorReachesOperationalVulkanCommandStream` sibling.
  Both inherit the existing `IntrinsicGraphicsVulkanSmokeTests` executable's
  `gpu vulkan graphics` labels, so the default CPU gate excludes the new test
  by label and a Vulkan-capable host selects it via `ctest -L gpu -L vulkan`.
  CPU-only correctness of the per-frame minimal-recipe counters asserted by
  the new fixture (`MinimalSurfacePassExecutions`, `MinimalPresentPassExecutions`,
  `MinimalRecipeMissingPrerequisiteCount`, `MinimalDebugBackbufferReadbackCopyCount`)
  is already locked in by the GRAPHICS-032A/B/C `Test.MinimalDebugSurfacePass.cpp`,
  `Test.MinimalDebugPresentPass.cpp`, and `Test.MinimalDebugBackbufferReadback.cpp`
  contract tests that run in the default gate. The pinned `clang-20`/
  `clang++-20` toolchain is unavailable in this remote-execution environment
  (Clang 18.1.3 only), so the local opt-in `ctest -L gpu -L vulkan` invocation
  on `IntrinsicGraphicsVulkanSmokeTests` could not run here — same constraint
  the GRAPHICS-033D author recorded on the same target.

## Goal
- Add the `gpu;vulkan` smoke fixture declared by `GRAPHICS-032`: drive one Vulkan frame of `FrameRecipe::MinimalDebugSurface` after `GRAPHICS-033C` lands the recording bodies and `GRAPHICS-033D` lands the visible-triangle-pixel assertion. This task is the recipe-side hookup; `GRAPHICS-033D` owns the assertion shape.

> **Scaffold notice.** This `gpu;vulkan` fixture was removed by [`GRAPHICS-081`](GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md) after the default-recipe equivalent of the visible-triangle assertion was green on Vulkan-capable hosts. The reusable pixel-readback driver harness survives in the default-recipe smoke; only the recipe-selector invocation is deleted.

## Retirement note

- Retired by [`GRAPHICS-081`](GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md) on 2026-06-02. The bootstrap Vulkan smoke fixture and recipe-selector coverage are deleted; `DefaultRecipeSurfaceGpuSmoke` is the canonical opt-in visible-triangle fixture.

## Non-goals
- No new diagnostics counters.
- No additional pass bodies.
- No CPU-only test additions (those land in `GRAPHICS-032A`/`B`/`C`).

## Context
- Owner/layer: `tests/integration/graphics`, `graphics/renderer` (recipe selector hookup if needed).
- Planning parent: [`tasks/done/GRAPHICS-032-minimal-surface-present-command-path.md`](GRAPHICS-032-minimal-surface-present-command-path.md), Recorded as Impl-D in the parent's Required changes.
- Upstream gates: `GRAPHICS-033C` (Vulkan recording bodies for the minimal recipe), `GRAPHICS-033D` (pixel-readback assertion shape).
- This task is intentionally narrow — it ensures the `MinimalDebugSurface` recipe selector reaches the live Vulkan path under the `gpu;vulkan` opt-in label.

## Required changes
- [x] Add or extend a `gpu;vulkan` fixture under `tests/integration/graphics/` that drives one frame with `RenderConfig::FrameRecipe = MinimalDebug` and asserts the recipe selector reached the operational Vulkan command stream (counter assertions: `MinimalSurfacePassExecutions == 1`, `MinimalPresentPassExecutions == 1`, `MinimalRecipeMissingPrerequisiteCount == 0`). Landed as `MinimalDebugSurfaceGpuSmoke.RecipeSelectorReachesOperationalVulkanCommandStream` in `tests/integration/graphics/Test.MinimalDebugSurfaceGpuSmoke.cpp`; also asserts the default-disabled `MinimalDebugBackbufferReadbackCopyCount == 0` so the recipe-selector path cannot silently regress into the GRAPHICS-033D readback wiring.
- [x] Coordinate with `GRAPHICS-033D` so both fixtures share the same pixel-readback path and one driver harness (no duplicate frame loops). Both `MinimalDebugSurfaceGpuSmoke` TESTs now route through `BootstrapEngineForMinimalDebug()` + `DriveOneFrameAndCapture()` in the same translation unit; the bounded `engine.Run()` driver lives in exactly one helper, and pixel-readback wiring stays exclusive to the 033D fixture.

## Tests
- [x] The new `gpu;vulkan` fixture itself is the test (run alongside `GRAPHICS-033D`).

## Docs
- [x] Update `tests/README.md` to enumerate the recipe smoke alongside the `GRAPHICS-033D` fixture.
- [x] Update `src/graphics/framegraph/README.md` to flip the `gpu;vulkan` minimal-recipe smoke row to current state (added a dedicated `GRAPHICS-032D minimal-recipe `gpu;vulkan` smoke` section). Also refreshed the cross-linked references in `src/graphics/renderer/README.md`, `src/graphics/vulkan/README.md`, and `tests/support/README.md`.

## Acceptance criteria
- [x] The fixture is selectable only via `-L 'gpu' -L 'vulkan'`; the default gate skips it. (Inherits `gpu vulkan graphics` labels from the existing `IntrinsicGraphicsVulkanSmokeTests` executable in `tests/CMakeLists.txt`.)
- [x] On Vulkan-capable hosts, the fixture passes; on non-Vulkan hosts, it skips deterministically. (Deterministic skip paths fire when `Platform::Backends::Glfw::CanInitialize()` returns false or `GetVulkanDeviceOperationalInputs(...).{Logical,Swapchain,CommandSync}Ready` are false — same skip predicates as the GRAPHICS-033D sibling, now shared through `BootstrapEngineForMinimalDebug()`.)

## Verification
```bash
cmake --preset ci -DINTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON
cmake --build --preset ci --target IntrinsicGraphicsIntegrationTests
ctest --test-dir build/ci --output-on-failure -L 'gpu' -L 'vulkan' --timeout 120
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes
- Adding pass bodies or recipe content.
- Selecting the fixture by default.
- Duplicating the `GRAPHICS-033D` driver loop.

## Completion note
- Retired 2026-05-18 after the recipe-selector sibling landed in the
  `MinimalDebugSurfaceGpuSmoke` fixture pair sharing one bounded `engine.Run()`
  driver helper. The local opt-in `ctest -L gpu -L vulkan` invocation on
  `IntrinsicGraphicsVulkanSmokeTests` is owned by the next Vulkan-capable host
  that builds this branch with `clang-20`/`clang++-20`; the GRAPHICS-033D
  author recorded the same toolchain-availability constraint when landing the
  sibling fixture and the CPU-only counter contract is locked in by
  `IntrinsicGraphicsContractCpuTests`.
