---
name: intrinsicengine-sandbox-input-lifecycle
description: The runtime frame-loop wiring pitfalls around sandbox input capture, window lifecycle, and edit-flush ordering that have regressed repeatedly — ImGui capture must gate camera/gizmo/pick input, window-close must route WindowCloseEvent to RequestExit and re-check before renderer work with an idle-wait before GPU teardown, UI/gizmo edits after the fixed-step bundle need the pre-render transform flush, decode must never block the platform poll thread, and the screen-space +Y-down / orbit-camera sign and HiDPI window-vs-framebuffer cursor conventions. Use this skill whenever touching Engine::RunFrame ordering, PollEvents/ShouldClose/window-close handling, ImGui input capture gating, viewport click-pick or gizmo drive, camera controller sign conventions, drag-and-drop import on the poll thread, or shutdown/device-idle teardown ordering in the sandbox.
---

# IntrinsicEngine Sandbox Input & Lifecycle Wiring

This skill codifies the runtime frame-loop wiring pitfalls that regressed
repeatedly in `ExtrinsicSandbox`: input capture, window lifecycle, edit-flush
ordering, and camera/cursor conventions. It adds no new behavior — every item
below is an invariant a retired bug already established, cited inline.

Owner layer: `runtime` (`Runtime.Engine.cpp`,
`Runtime.Engine.FrameLoop.Internal.hpp`).
This skill owns **input/lifecycle wiring**. Wrong/black **frame content** on the
Vulkan path is `intrinsicengine-vulkan-frame-triage`; making an import *visible*
is `intrinsicengine-import-visibility-contract` (`PROC-018`).

## The wiring pitfalls

### 1. ImGui capture must gate camera / gizmo / pick input

When ImGui wants the mouse or keyboard, that input must **not** also drive engine
controls. The frame loop derives `imguiCapturesInput` from
the single `m_ImGuiEditorBridge.CaptureSnapshot()` read and its
`CapturesViewportInput()` result, then gates viewport click-pick, camera
control, and gizmo drive on it; a viewport click submits a pick request only
when neither ImGui nor a gizmo owns the click.

Evidence: `BUG-017` (clicks over UI fell through to selection / black outline),
`BUG-036` (UI-captured input leaked into engine controls).

### 2. Window-close: route, re-check before render, idle-wait before GPU teardown

Three separate regressions here (`BUG-027`, `BUG-037`, `BUG-054`). The invariant:

- A platform/native `WindowCloseEvent` routes to `RequestExit()` /
  `RequestExitFromWindowClose(...)`, not directly to teardown.
- `RunFrame()` must re-check the close/`ShouldClose()` state **immediately after
  `PollEvents()`** and abort the frame before entering ImGui/render work — do not
  record a frame for a window that is closing.
- On shutdown, keep runtime-owned GPU job resources (e.g. K-Means GPU queue)
  alive until **after** the device-idle wait, then tear down renderer/device.
  Emit an `[INFO]` breadcrumb on the close request.

### 3. Pre-render transform flush after the fixed-step bundle

UI/gizmo transform edits applied *after* the fixed-step systems ran will not
reach the rendered model matrix in the same frame unless flushed. `RunFrame()`
runs the runtime-owned `FlushPreRenderTransformState`
(transform-hierarchy → bounds → render-sync) after the variable tick, the ImGui
editor hook, and the gizmo drive — but **before** gizmo packet build and render
extraction.

Evidence: `BUG-024` (Inspector/gizmo transform edits did not move the rendered
triangle).

### 4. Never block the platform poll thread on decode

Heavy work on the event/poll thread stalls `PollEvents` and freezes input.
Drag-and-drop import publishes the file event and queues decode/materialization
off the poll thread (the runtime streaming executor), rather than decoding
synchronously in the drop handler.

Evidence: `BUG-021` (drop import blocked platform polling —
`BUG-021-sandbox-drop-import-blocks-platform-poll.md`, the drop-import member of
the duplicated `BUG-021` id, not the camera scene-table one).

### 5. Screen-space +Y-down and orbit-camera sign conventions

Camera/cursor math crosses two flipped coordinate spaces; sign errors here are
easy and were a fix-of-a-fix. Orbit pitch drag uses `+yDelta` in the quaternion
trackball update, and controller modes must keep the seed focus point so the
target stays centered. Always test the **pole-crossing** case (a vertical drag
that inverts camera up), which is where the first fix was wrong.

Evidence: `BUG-020` (camera modes / centering), `BUG-039` → `BUG-040`
(orbit rotation lock, then the vertical-drag-sign fix-of-a-fix).

### 6. HiDPI window-vs-framebuffer cursor scaling

Click-pick cursor coordinates are in window space but the pick target is in
framebuffer space; on HiDPI they differ. Pick math must scale window → framebuffer
before sampling, or picks land off-target.

Evidence: `BUG-026` (viewport click selection — cursor/readback coordinate
reconstruction).

## How to prove a change is correct

- **Default CPU/null gate** covers the input-to-pick bridge, frame-loop
  ordering, and window-close timing as runtime contract tests without a concrete
  platform backend — this is the `CPUContracted` floor.
- **Live behavior (pixels move, click selects) is `Operational`**, proven by an
  opt-in `gpu;vulkan` readback smoke that drives the real UI/command path — see
  `intrinsicengine-gpu-smoke-authoring`. CPU coverage alone does not prove the
  live frame responded.

## Related

- `intrinsicengine-vulkan-frame-triage` — when the frame content is black/wrong
  despite input/lifecycle wiring being correct.
- `intrinsicengine-gpu-smoke-authoring` — the `Operational` live-behavior proof.
- `intrinsicengine-import-visibility-contract` (`PROC-018`) — making an imported
  entity visible/selectable (distinct from the input/lifecycle wiring here).
- `intrinsicengine-core` — `runtime` composition ownership and the frame-loop
  layering rules.
