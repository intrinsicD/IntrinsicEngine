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

## GLFW/X11 sanitizer lifetime contract

The GLFW backend owns one process-static `GLFWLifetime`. A 2026-07-13
LeakSanitizer report attributed 408 bytes to `_XimOpenIM` after an otherwise
passing runtime contract. The clean exact repro no longer reports the
allocation, and a debugger trace proves normal process teardown reaches
`GLFWLifetime::~GLFWLifetime()` -> `glfwTerminate()` ->
`XUnregisterIMInstantiateCallback()` -> `XCloseIM()` before exit. That evidence
does not support an engine shutdown-order change or a suppression for the
GLFW/Xlib path.

`GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl` preserves the diagnosis
as a Linux + GLFW regression. Its standalone helper wraps `glfwTerminate` and
uses reverse `atexit` ordering to prove the engine's process-static lifetime
calls it exactly once before LeakSanitizer's exit sweep. A separate helper mode
allocates `Bug082SyntheticEngineLeak`; the CMake runner requires that process
to fail with a LeakSanitizer report for the named 4096-byte allocation before
accepting the clean GLFW run.

Both subprocesses explicitly use `detect_leaks=1` without a suppression file,
and the helper deliberately does not link the shared GTest/TestSupport
sanitizer defaults. The CTest entry reports an environment skip when ASan is
not active or a usable X11 display is unavailable.

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
