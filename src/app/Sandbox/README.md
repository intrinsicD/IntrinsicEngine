# App/Sandbox

This directory contains the `Sandbox` module/files.

`Sandbox` is the generic reference integration target. The executable obtains
its concrete runtime application through `Sandbox::CreateSandboxApp()` so the
module-private app implementation remains policy-light: it may observe
lifecycle hooks, but engine feature wiring, frame phases, and subsystem behavior
belong in `Runtime` or lower engine layers. The executable obtains its default
configuration through `Runtime` and should not import lower layers directly.

The sandbox app attaches the promoted runtime-owned `SandboxEditorUi` shell
through application lifecycle hooks and installs the sandbox default runtime
policy bundle through `Runtime::RegisterSandboxDefaultRuntimePolicies(engine)`.
It unregisters the returned handles during shutdown before the engine tears down.
The app remains a runtime-only consumer: the editor shell registers with
`Engine::SetImGuiEditorCallback`, reads scene and selection state through
runtime APIs, emits selection and local-transform edit commands through
runtime-owned seams, replaces runtime camera-controller slots through the
engine-owned registry, toggles mesh edge/vertex primitive views through runtime
extraction-cache settings, routes selected-entity spatial-debug options through
`SpatialDebugBinding`, routes material/scalar/color visualization choices
through `VisualizationConfig`, routes visualization adapter bindings through
runtime extraction-cache state, and submits file/import path commands through
`Engine::ImportAssetFromPath(...)`. Asset routing, decoding, `AssetService`
mutation, model-scene materialization, texture-upload requests, and default
import/input policy implementation remain runtime/asset owned; the sandbox app
implementation only composes the runtime-provided defaults.

The `File / Import` editor window also polls
`Engine::GetAssetImportQueueSnapshot()` for the runtime-owned AssetIO queue.
Rows show queued/running/apply/upload/terminal import stages, payload kind,
path basename, elapsed time, determinate progress where available, indeterminate
stage labels where decoder progress is unknown, and failure/cancellation
diagnostics. Clear-completed and cancellable dropped-geometry commands route
back to `Engine`; the sandbox app and UI never own asset, ECS, or graphics
state.

The promoted EditorUI also exposes stable top-level ImGui menu slots for
`PointCloud`, `Graph`, and `Mesh`. Their submenu items open selected-entity
domain windows for render-hint status, visualization/spatial-debug controls,
primitive-selection details, and processing-discovery affordances. These
windows are an EditorUI workflow only: they reuse `SandboxEditorUi` models and
runtime-owned command surfaces, and the sandbox app still does not own
selection, ECS mutation, rendering, or asset state.

With the standard reference configuration, runtime creates `ReferenceTriangle`
through `Extrinsic.Runtime.ReferenceScene::TriangleProvider` as an ordinary
ECS mesh-domain `GeometrySources` entity with `RenderSurface`, durable
`StableId`, `Selection::SelectableTag`, and white `VisualizationConfig`.
The sandbox app does not create, render, select, or special-case the triangle.

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

## Working Sandbox Acceptance

`RUNTIME-095` retires the scoped working-sandbox acceptance path: on the
default CPU/null gate, `RuntimeSandboxAcceptance.*` proves mesh, graph, and point
cloud residency, finite camera-controller output, entity and primitive
selection, selection-outline snapshot handoff, and deterministic editor-panel
models. On Vulkan-capable hosts, the opt-in
`RuntimeSandboxAcceptanceGpuSmoke.AcceptanceSceneReachesOperationalDefaultRecipePresent`
smoke drives bounded `Engine::Run()` frames with the same mesh/graph/point-cloud
scene and the `SandboxEditorUi` attached, then asserts canonical default-recipe
`Present` plus no canonical `SkippedUnavailable` pass.

Run the scoped operational smoke with:

```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests ExtrinsicSandbox
ctest --test-dir build/ci-vulkan --output-on-failure -R RuntimeSandboxAcceptanceGpuSmoke --timeout 120
```

This acceptance does not claim every asset format, KTX decode, post-upload
material re-resolution, advanced PBR, transparent selection, Gaussian splats, or
scene serialization parity.

## Frame-Pacing Diagnostics

`UI-030` adds an explicit bounded capture mode to `ExtrinsicSandbox`:

```bash
build/ci-vulkan/bin/ExtrinsicSandbox \
  --frame-pacing-report /tmp/intrinsic-frame-pacing.json \
  --frame-pacing-frames 120
```

The report is JSON (`intrinsic.frame_pacing.v1`) containing one sample per
completed frame, plus aggregate phase totals and the highest-total phase. The
capture wrapper stops the app after the requested frame count, reads
`Engine::GetLastFramePacingDiagnostics()`, and writes only copied runtime
diagnostics; the sandbox app still imports runtime only and does not branch on
Vulkan backend internals. The opt-in CTest
`ExtrinsicSandbox.FramePacingDiagnosticCapture` validates this mode in the
`ci-vulkan` preset.

## Contents

- `CMakeLists.txt`
- `Sandbox.cppm`
- `main.cpp`
