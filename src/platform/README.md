# Platform

`src/platform` owns the windowing and input abstractions used by the
engine. The public surface is a port-style interface; concrete backends live in
per-platform subdirectories.

## Public module surface

- `Extrinsic.Platform.Window` (interface; see `Platform.IWindow.cppm`)
- `Extrinsic.Platform.Input`

## Backends

- `LinuxGlfwVulkan/` — GLFW-based window and Vulkan surface creation, plus GLFW
  input event routing.
  - `Platform.Window.cpp`
  - `Platform.Input.cpp`

## Directory layout

```text
Platform.IWindow.cppm
Platform.Input.cppm
LinuxGlfwVulkan/
  Platform.Window.cpp
  Platform.Input.cpp
```

## Dependency note

`Platform` depends on `Core` only. It must not import `Graphics`, `ECS`, or
`Runtime`. Platform surfaces are consumed by `Runtime` (composition root) and
by `Graphics` backends that need to bind a Vulkan surface to a window.

The interface/backend split is deliberate: headless tests and alternative
platforms (Windows/macOS/Wayland) plug in by adding a sibling backend directory
alongside `LinuxGlfwVulkan/` without touching the interface modules.
