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
