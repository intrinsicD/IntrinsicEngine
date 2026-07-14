# RORG-035 — Core 2D extent ownership

## Goal
Move generic 2D extent/rectangle value types out of the platform window module into core so graphics, RHI, runtime camera controllers, and platform can share viewport dimensions without depending on `Extrinsic.Platform.Window` for data-only geometry.

## Non-goals
- Do not move camera controller ownership in this task.
- Do not move `Graphics.CameraSnapshots` out of graphics in this task.
- Do not alter renderer behavior, camera math, swapchain behavior, or input policy.
- Do not remove compatibility aliases that keep existing platform callers building.

## Context
Owned by core/platform/graphics architecture cleanup. `Platform::Extent2D` is a generic width/height pair currently exported from `Extrinsic.Platform.Window`; graphics and RHI modules import platform only to use that value type. Per repository layering, data-only dimensions needed by lower/rendering layers should live in `core`, while `platform` should own live window/input ports.

## Required changes
- [x] Add `Extrinsic.Core.Geometry2D` with `Core::Extent2D`, `Core::Offset2D`, and `Core::Rect2D` POD value types.
- [x] Re-export compatibility aliases from `Extrinsic.Platform.Window` for existing `Platform::Extent2D` users.
- [x] Update graphics camera/render-frame/render-world APIs to use `Core::Extent2D` instead of `Platform::Extent2D`.
- [x] Update RHI backbuffer extent APIs and backend implementations to use `Core::Extent2D`.
- [x] Update runtime camera controllers to consume `Core::Extent2D` and stop importing `Extrinsic.Platform.Window` only for extents.

## Tests
- [x] Build `IntrinsicTests`.
- [x] Run focused camera/render/layering tests.
- [x] Run repository layering check.
- [x] Run task policy check.

## Docs
- [x] Update runtime/graphics documentation to describe core-owned viewport extent values.
- [x] Regenerate `docs/api/generated/module_inventory.md` after adding the core module.

## Acceptance criteria
- [x] Graphics camera/render-frame/render-world modules no longer import `Extrinsic.Platform.Window` only for viewport dimensions.
- [x] RHI `GetBackbufferExtent()` no longer exposes a platform-owned value type.
- [x] Platform window remains the owner of live window/input ports.
- [x] Current tests pass for the touched scope.

## Verification
- `/home/alex/.local/bin/cmake --build --preset ci --target IntrinsicTests`
- `ctest --test-dir build/ci --output-on-failure -R 'RuntimeCameraControllers|RuntimeFrameLoopContract|RuntimeEngineLayering|RenderWorldContract' --timeout 60`
- `python3 tools/repo/check_layering.py --root src --strict`
- `python3 tools/agents/check_task_policy.py --root . --strict`

## Forbidden changes
- No ECS camera component design changes in this task.
- No `Graphics.CameraSnapshots` module move in this task.
- No live platform/window/input dependency may be introduced into graphics camera snapshot math.


## Completion metadata

- Completion date: 2026-05-13.
- Commit reference: pending current workspace/PR.
