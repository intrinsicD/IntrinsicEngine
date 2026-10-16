# Platform

`src/platform` owns windowing, input, and local stream sockets used by the
engine. The public surface is a port-style interface; concrete backends live in
explicit backend modules under `backends/`.

## Public module surface

- `Extrinsic.Platform.Window` (interface; see `Platform.IWindow.cppm`)
- `Extrinsic.Platform.Input`
- `Extrinsic.Platform.LocalSocket` — owner-only (0600) Unix-domain stream
  listener/connection with timeout-bounded, non-throwing I/O for the opt-in agent
  control lane; `Unsupported` on platforms without Unix-domain sockets.
  Writes poll after nonblocking sends stall. Listener cleanup removes only its
  original socket inode; an ownership probe must confirm a stale endpoint before
  replacement.

## Backends

- `backends/null/Platform.Backend.Null.cppm` — deterministic headless window,
  event queue, resize state, input buffering, clipboard, and cursor-mode fake.
- `backends/glfw/Platform.Backend.Glfw.cppm` — GLFW window ownership, callback
  to platform event buffering, resize handling, clipboard, cursor modes, and
  input state updates.

`Platform.CreateWindow.cpp` is the only selected-backend bridge for the public
`Extrinsic::Platform::CreateWindow` factory. Configure with:

```bash
cmake --preset ci -DINTRINSIC_PLATFORM_BACKEND=Null
cmake --preset ci -DINTRINSIC_HEADLESS_NO_GLFW=OFF -DINTRINSIC_PLATFORM_BACKEND=Glfw
```

`INTRINSIC_PLATFORM_BACKEND=Auto` selects `Glfw` for non-headless Linux/Vulkan
builds and `Null` otherwise. `INTRINSIC_HEADLESS_NO_GLFW=ON` forces the null
backend unless `Glfw` is requested explicitly, which is a configure error.

At runtime, `Core::Config::WindowConfig::Backend` defaults to
`WindowBackend::Configured`, which preserves the CMake-selected backend above.
Tests that need deterministic headless `Engine::Run()` coverage can explicitly
set `WindowBackend::Null`; this routes `Platform::CreateWindow` to the
always-compiled Null backend. A failed configured GLFW window remains born
closed and does not silently fall back to Null.

## Directory layout

```text
Platform.IWindow.cppm
Platform.Input.cppm
Platform.LocalSocket.cppm
Platform.CreateWindow.cpp
backends/
  null/
    Platform.Backend.Null.cppm
  glfw/
    Platform.Backend.Glfw.cppm
```

## Dependency note

`Platform` depends on `Core` only. It must not import `Graphics`, `ECS`, or
`Runtime`. Platform surfaces are consumed by `Runtime` (composition root). Runtime passes
the native window handle to graphics through `RHI::DeviceCreateDesc`; the Vulkan
backend creates the surface itself.

The interface/backend split is deliberate: headless tests and alternative
backends plug in by adding a sibling backend directory under `backends/`
without touching the interface modules.

X11 and Wayland are platforms of the existing GLFW backend, not separate
backends: on Linux GLFW is built with both and picks the session's platform at
`glfwInit` (native Wayland inside a Wayland session, X11 under X11); the
backend logs the selected platform and GLFW version once. Under native Wayland
programmatic focus changes are not granted without user input, so
`GlfwPlatformSmoke.NativeFocusChangesEmitWindowFocusEvents` skips there; its
native focus evidence is X11.

Platform owns window/input ports and local stream sockets. It exposes input state/events to runtime;
it does not create graphics camera snapshots, pick requests, gizmo packets, or
transform mutations directly.

## GLFW/X11 sanitizer lifetime contract

The GLFW backend owns one process-static lifetime and terminates GLFW once at
process teardown. `GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl` checks
that ordering with leak detection enabled and an intentional engine-leak control.
It skips when ASan or a usable X11 display is unavailable. The bounded libX11
XIM suppression and its negative control are described in
[the test guide](../../tests/README.md) and enforced by
[the lifecycle runner](../../tests/regression/platform/Run.GlfwLifecycleLsanContract.cmake).

## Event contract and editor file boundary

`Extrinsic.Platform.Window` exposes the current editor/runtime event contract as
data-only payloads:

- `WindowResizeEvent` carries framebuffer-pixel width/height and drives
  resize state. Either zero framebuffer dimension is minimized for Null and
  GLFW; on X11, GLFW also reports native iconification with a nonzero framebuffer.
  Native Wayland cannot report iconification through GLFW; zero extent remains
  the available signal there.
- `KeyEvent`, `MouseButtonEvent`, `ScrollEvent`, and `CursorEvent` update the
  per-frame `Platform.Input::Context` state owned by the window port. Press edges
  survive a press and release within one poll, and clear at the next poll; held
  input does not produce another edge. Null listeners can queue events for the
  next poll without changing the current delivery batch. Listener arguments
  remain valid through their invocation even when they drain the queue. The same
  events are drained by `Runtime.ImGuiAdapter`, which is the only consumer that
  translates them into ImGui IO; the platform layer names no ImGui type and
  forwards to no ImGui platform backend because none is initialized. The
  unreachable `ImGui_ImplGlfw_*` forwards are absent (see `BUG-139`).
  `KeyEvent::KeyCode` is GLFW's key numbering, the same space
  `Platform.Input::Key` publishes; `GLFW_REPEAT` is filtered out at the
  callback, and ImGui synthesises its own key repeat from hold duration.
- `CharEvent` is UTF-32 codepoint input for simple text entry. Full IME
  composition is not a promoted platform goal yet.
- `WindowDropEvent` carries dropped file paths only. Runtime owns import,
  ingest, scene replacement, and status reporting.
- `WindowFocusEvent` reports native window focus gain/loss (GLFW focus
  callback; Null through `QueueEvent`). Runtime forwards it to ImGui IO and
  republishes it on the kernel event bus, where a loss cancels a running
  transform-gizmo drag. A real focus change emits `{false}` on the window
  losing focus and `{true}` on the one gaining it.
  Evidence: `NullPlatform.DeliversWindowFocusEventsInOrder` (CPU) and
  `GlfwPlatformSmoke.NativeFocusChangesEmitWindowFocusEvents` (two real GLFW
  windows, focus requested A → B → A). The GLFW smoke needs a display and a
  window manager that grants programmatic focus; it passed on an unlocked
  GNOME/X11 session and skips with its reason where focus is refused (for
  example a locked session).
- Clipboard text and cursor mode are exposed through `IWindow` methods so ImGui
  and editor adapters can remain backend-neutral.

The platform layer does not own native file dialogs. Current editor workflows use
runtime/UI path-entry commands and OS drag/drop (`WindowDropEvent`). A
platform-native dialog service, multi-window behavior, non-Linux backend parity,
or IME composition support requires a separate task with a concrete runtime/UI
consumer; it must still preserve `platform -> core` only.
