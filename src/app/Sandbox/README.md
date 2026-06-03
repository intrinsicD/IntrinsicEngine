# App/Sandbox

This directory contains the `Sandbox` module/files.

`Sandbox` is the generic reference integration target. `Sandbox::App` should
remain policy-light: it may observe lifecycle hooks, but engine feature wiring,
frame phases, and subsystem behavior belong in `Runtime` or lower engine layers.
The executable obtains its default configuration through `Runtime` and should
not import lower layers directly.

`Sandbox::App` attaches the promoted runtime-owned `SandboxEditorUi` shell
through application lifecycle hooks. The app remains a runtime-only consumer:
the editor shell registers with `Engine::SetImGuiEditorCallback`, reads scene
and selection state through runtime APIs, emits selection and local-transform
edit commands through runtime-owned seams, replaces runtime camera-controller
slots through the engine-owned registry, toggles mesh edge/vertex primitive
views through runtime extraction-cache settings, routes selected-entity
spatial-debug options through `SpatialDebugBinding`, routes material/scalar/color
visualization choices through `VisualizationConfig`, routes visualization
adapter bindings through runtime extraction-cache state, and submits file/import
path commands through `Engine::ImportAssetFromPath(...)`. Asset routing,
decoding, `AssetService` mutation, model-scene materialization, and texture-upload
requests remain runtime/asset owned; `Sandbox::App` does not special-case asset
authority.

## Build presets

- `cmake --preset ci` configures the headless CPU/null gate (Sandbox disabled,
  promoted Vulkan disabled). Use this for fast CPU verification.
- `cmake --preset ci-vulkan` configures the same Debug + tests profile with
  `INTRINSIC_BUILD_SANDBOX=ON` and `INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON`
  so `ExtrinsicSandbox` runs against the promoted Vulkan backend on
  Vulkan-capable hosts (GRAPHICS-080). On hosts without Vulkan support the
  runtime falls back to Null per the GRAPHICS-033 truth table and the
  `VulkanRequestedButNotOperational` breadcrumb fires once during startup.
  Run the opt-in `gpu;vulkan` fixtures with
  `ctest --test-dir build/ci-vulkan -L 'gpu' -L 'vulkan'` (intersection
  semantics, not the regex-union `'gpu|vulkan'`).

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
