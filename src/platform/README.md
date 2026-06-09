# Platform

`src/platform` owns the windowing and input abstractions used by the
engine. The public surface is a port-style interface; concrete backends live in
explicit backend modules under `backends/`.

## Public module surface

- `Extrinsic.Platform.Window` (interface; see `Platform.IWindow.cppm`)
- `Extrinsic.Platform.Input`

## Backends

- `backends/null/Platform.Backend.Null.cppm` — deterministic headless window,
  event queue, resize state, input buffering, clipboard, and cursor-mode fake.
- `backends/glfw/Platform.Backend.Glfw.cppm` — GLFW window ownership, callback
  to platform event buffering, resize handling, clipboard, cursor modes, and
  input state updates.
- `backends/glfw/Platform.Backend.GlfwVulkanSurface.cppm` — GLFW Vulkan surface
  creation policy isolated from `Graphics` and `Runtime` imports.

`Platform.CreateWindow.cpp` is the only selected-backend bridge for the public
`Extrinsic::Platform::CreateWindow` factory. Configure with:

```bash
cmake --preset ci -DINTRINSIC_PLATFORM_BACKEND=Null
cmake --preset ci -DINTRINSIC_PLATFORM_BACKEND=Glfw
```

`INTRINSIC_PLATFORM_BACKEND=Auto` selects `Glfw` for non-headless Linux/Vulkan
builds and `Null` otherwise. `INTRINSIC_HEADLESS_NO_GLFW=ON` forces the null
backend unless `Glfw` is requested explicitly, which is a configure error.

## Directory layout

```text
Platform.IWindow.cppm
Platform.Input.cppm
Platform.CreateWindow.cpp
backends/
  null/
    Platform.Backend.Null.cppm
  glfw/
    Platform.Backend.Glfw.cppm
    Platform.Backend.GlfwVulkanSurface.cppm
```

## Dependency note

`Platform` depends on `Core` only. It must not import `Graphics`, `ECS`, or
`Runtime`. Platform surfaces are consumed by `Runtime` (composition root) and
by graphics/RHI backends through public platform handles or explicit surface
helpers.

The interface/backend split is deliberate: headless tests and alternative
platforms (Windows/macOS/Wayland) plug in by adding a sibling backend directory
under `backends/` without touching the interface modules.

Platform owns window/input ports only. It exposes input state/events to runtime;
it does not create graphics camera snapshots, pick requests, gizmo packets, or
transform mutations directly.

## Event contract and editor file boundary

`Extrinsic.Platform.Window` exposes the current editor/runtime event contract as
data-only payloads:

- `WindowResizeEvent` carries framebuffer-pixel width/height and drives
  resize/minimize state (`0x0` is minimized for Null and GLFW).
- `KeyEvent`, `MouseButtonEvent`, `ScrollEvent`, and `CursorEvent` update the
  per-frame `Platform.Input::Context` state owned by the window port.
- `CharEvent` is UTF-32 codepoint input for simple text entry. Full IME
  composition is not a promoted platform goal yet.
- `WindowDropEvent` carries dropped file paths only. Runtime owns import,
  ingest, scene replacement, and status reporting.
- Clipboard text and cursor mode are exposed through `IWindow` methods so ImGui
  and editor adapters can remain backend-neutral.

The platform layer does not own native file dialogs. Current editor workflows use
runtime/UI path-entry commands and OS drag/drop (`WindowDropEvent`). A
platform-native dialog service, multi-window behavior, non-Linux backend parity,
or IME composition support requires a separate task with a concrete runtime/UI
consumer; it must still preserve `platform -> core` only.
