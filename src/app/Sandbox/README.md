# App/Sandbox

This directory contains the `Sandbox` module/files.

`Sandbox` is the generic reference integration target. `Sandbox::App` should
remain policy-light: it may observe lifecycle hooks, but engine feature wiring,
frame phases, and subsystem behavior belong in `Runtime` or lower engine layers.
The executable obtains its default configuration through `Runtime` and should
not import lower layers directly.

## Shader artifacts

`ExtrinsicSandbox` invokes `cmake/CompileShaders.cmake` through the
`ExtrinsicSandbox_Shaders` build target. The helper compiles
`assets/shaders/**.{vert,frag,comp}` to SPIR-V under the configured runtime
output directory (`${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders`, normally
`build/<preset>/bin/shaders`) so runtime pipeline paths can resolve `.spv`
files next to the executable.

The host must provide `glslc` (for example from the Vulkan SDK or distro shader
tooling package). If `glslc` is unavailable, configure emits a warning and the
non-shader targets continue to build, but Sandbox pipelines that require SPIR-V
artifacts will fail to load and renderer fallback diagnostics may increment.

## Contents

- `CMakeLists.txt`
- `Sandbox.cppm`
- `main.cpp`
