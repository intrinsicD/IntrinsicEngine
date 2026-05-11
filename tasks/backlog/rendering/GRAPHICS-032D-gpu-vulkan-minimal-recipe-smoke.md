# GRAPHICS-032D — Opt-in `gpu;vulkan` smoke for `FrameRecipe::MinimalDebugSurface`

## Goal
- Add the `gpu;vulkan` smoke fixture declared by `GRAPHICS-032`: drive one Vulkan frame of `FrameRecipe::MinimalDebugSurface` after `GRAPHICS-033C` lands the recording bodies and `GRAPHICS-033D` lands the visible-triangle-pixel assertion. This task is the recipe-side hookup; `GRAPHICS-033D` owns the assertion shape.

## Non-goals
- No new diagnostics counters.
- No additional pass bodies.
- No CPU-only test additions (those land in `GRAPHICS-032A`/`B`/`C`).

## Context
- Status: not started.
- Owner/layer: `tests/integration/graphics`, `graphics/renderer` (recipe selector hookup if needed).
- Planning parent: [`tasks/done/GRAPHICS-032-minimal-surface-present-command-path.md`](../../done/GRAPHICS-032-minimal-surface-present-command-path.md), Recorded as Impl-D in the parent's Required changes.
- Upstream gates: `GRAPHICS-033C` (Vulkan recording bodies for the minimal recipe), `GRAPHICS-033D` (pixel-readback assertion shape).
- This task is intentionally narrow — it ensures the `MinimalDebugSurface` recipe selector reaches the live Vulkan path under the `gpu;vulkan` opt-in label.

## Required changes
- [ ] Add or extend a `gpu;vulkan` fixture under `tests/integration/graphics/` that drives one frame with `RenderConfig::FrameRecipe = MinimalDebug` and asserts the recipe selector reached the operational Vulkan command stream (counter assertions: `MinimalSurfacePassExecutions == 1`, `MinimalPresentPassExecutions == 1`, `MinimalRecipeMissingPrerequisiteCount == 0`).
- [ ] Coordinate with `GRAPHICS-033D` so both fixtures share the same pixel-readback path and one driver harness (no duplicate frame loops).

## Tests
- [ ] The new `gpu;vulkan` fixture itself is the test (run alongside `GRAPHICS-033D`).

## Docs
- [ ] Update `tests/README.md` to enumerate the recipe smoke alongside the `GRAPHICS-033D` fixture.
- [ ] Update `src/graphics/framegraph/README.md` to flip the `gpu;vulkan` minimal-recipe smoke row to current state.

## Acceptance criteria
- [ ] The fixture is selectable only via `-L 'gpu;vulkan'`; the default gate skips it.
- [ ] On Vulkan-capable hosts, the fixture passes; on non-Vulkan hosts, it skips deterministically.

## Verification
```bash
cmake --preset ci -DINTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON
cmake --build --preset ci --target IntrinsicGraphicsIntegrationTests
ctest --test-dir build/ci --output-on-failure -L 'gpu;vulkan' --timeout 120
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes
- Adding pass bodies or recipe content.
- Selecting the fixture by default.
- Duplicating the `GRAPHICS-033D` driver loop.

## Next verification step
- Add the fixture and run the opt-in invocation on a Vulkan-capable host.
