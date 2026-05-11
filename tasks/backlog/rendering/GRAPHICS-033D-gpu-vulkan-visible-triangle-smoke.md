# GRAPHICS-033D — Opt-in `gpu;vulkan` visible-triangle smoke fixture

## Goal
- Add the opt-in `gpu;vulkan` smoke fixture declared by `GRAPHICS-033`: on hosts with Vulkan support, drive one frame of the GRAPHICS-032 `MinimalDebugSurface` recipe with the GRAPHICS-029B reference triangle and the GRAPHICS-031A default debug pipeline, asserting (a) the device reports `Operational` only after all 9 gate prerequisites are met, (b) the swapchain image after `Present` contains a visible triangle (pixel readback assertion at 4 sample points), and (c) no fallback counters increment during the operational frame.

## Non-goals
- No new diagnostics counters beyond those declared by `GRAPHICS-033A`/`B`.
- No additional pass bodies (Phase-2 backlog `GRAPHICS-070..076`).
- No CPU-only test additions; the entire scope of this task is the opt-in `gpu;vulkan` smoke.
- No Vulkan validation-layer policy change.
- No Wayland/X11/headless-Vulkan permutation matrix; one host environment.

## Context
- Status: not started.
- Owner/layer: `tests/integration/graphics` for the smoke; `graphics/vulkan` for any minor diagnostics surface adjustments needed by the assertion.
- Planning parent: [`tasks/done/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md`](../../done/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md), Recorded as Impl-D in the parent's Required changes.
- Upstream gates: `GRAPHICS-033C` (recording bodies), `GRAPHICS-032D` (already lists this as the gpu;vulkan smoke for the recipe; this task is the Vulkan-side coordination).
- The smoke is intentionally outside the default CPU gate (`-LE 'gpu|vulkan|slow|flaky-quarantine'`); hosts without Vulkan skip the test deterministically.

## Required changes
- [ ] Add `tests/integration/graphics/Test.MinimalDebugSurfaceGpuSmoke.cpp` labelled `gpu;vulkan` in `tests/CMakeLists.txt`.
- [ ] The smoke does the following per its single test case:
  1. Configure runtime with `INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON` build + `RenderConfig::EnablePromotedVulkanDevice = true` + `CreateReferenceEngineConfig()` + `RenderConfig::FrameRecipe = MinimalDebug`.
  2. `Engine::Initialize()`. Skip the test deterministically if `IsOperational() == false` after init (host lacks Vulkan support).
  3. Drive one `RunFrame()` call.
  4. Read the swapchain image back through the `Picking.Readback` drain pattern (or a dedicated readback path if more efficient) and assert at four sample points: triangle interior pixels match the GRAPHICS-031 `BaseColorFactor` pre-multiplied with the deterministic vertex color, and outside-triangle pixels match the clear color.
  5. Assert `VulkanFallbackToNullCount == 0`, `VulkanInitFailureCount == 0`, `VulkanValidationErrorCount == 0`, `VulkanOperationalGateFailureCount == 0`.
  6. `Engine::Shutdown()`.
- [ ] Update the test allow-list in `tests/README.md` and `tests/CMakeLists.txt` to label the new fixture `gpu;vulkan` (no new label introduced).

## Tests
- [ ] The new `gpu;vulkan` fixture itself is the test.
- [ ] Confirm the default CPU gate (`-LE 'gpu|vulkan|slow|flaky-quarantine'`) does **not** select the new fixture.
- [ ] On hosts without Vulkan support: the fixture is selected by the `gpu;vulkan` opt-in invocation and reports a deterministic `SKIPPED` with a `VulkanRequestedButNotOperational` warn breadcrumb logged once.

## Docs
- [ ] Update `tests/README.md` to enumerate the new fixture under the `gpu;vulkan` opt-in section.
- [ ] Update `src/graphics/vulkan/README.md` to flip the `gpu;vulkan` minimal-recipe smoke row to current state.

## Acceptance criteria
- [ ] On a Vulkan-capable Linux host, the fixture passes pixel-readback assertions and reports zero fallback-counter increments.
- [ ] On hosts without Vulkan support, the fixture skips deterministically; CI default gate is unaffected.
- [ ] No `gpu;vulkan` regression in any other opt-in fixture.

## Verification
```bash
cmake --preset ci -DINTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON
cmake --build --preset ci --target IntrinsicGraphicsIntegrationTests
# Default gate (skips the new fixture):
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Opt-in gate (selects only the new fixture and any sibling gpu;vulkan tests):
ctest --test-dir build/ci --output-on-failure -L 'gpu;vulkan' --timeout 120
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes
- Selecting the new fixture by default.
- Adding additional pass bodies under cover of the smoke.
- Mutating validation-layer or swapchain policy.
- Mixing mechanical file moves with semantic refactors.

## Next verification step
- Add the fixture, label it `gpu;vulkan`, run the opt-in invocation on a Vulkan-capable host, confirm pixel and counter assertions.
