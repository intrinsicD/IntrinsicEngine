# App/Sandbox

This directory contains the `Sandbox` module/files.

`Sandbox` is the generic reference integration target. The executable obtains
its concrete runtime application through `Sandbox::CreateSandboxApp()` so the
module-private app implementation remains policy-light. `Sandbox.cppm` exports
only the app factory and runtime-module registration declarations; the concrete
`App` lifecycle class and its runtime imports live in `Sandbox.cpp`. It may observe
lifecycle hooks, but engine feature wiring, frame phases, and subsystem behavior
belong in `Runtime` or lower engine layers. The executable obtains its default
configuration through `Runtime` and should not import lower layers directly.

The app-owned `Sandbox.Editor.Controller` owns `Sandbox.Editor.Shell` and all
panel-family lifetimes behind one attach/detach interface. The shell owns the
ten core Sandbox windows, menu composition, ImGui state, and frame
presentation while delegating generic callback/registry/visibility lifecycle
to `Runtime.EditorUiHost`. Runtime exposes the presentation-free
`Runtime.SandboxEditorFacades` models, commands, result sinks, and attached
session. `Sandbox.Editor.MethodPanels` owns the K-Means, Progressive Poisson,
and parameterization ImGui controls and registers six domain windows through
the shell's context-aware contribution facade. The app
continues to import the same runtime module surface. `Sandbox.Editor.MeshProcessingPanels`
owns the ICP registration window plus the mesh denoise, curvature, remesh,
subdivide, simplify, and mesh/graph/point-cloud vertex-normal windows. It
registers those nine windows under their existing menu paths, owns their ImGui
input/result-presentation state, and consumes only runtime models and command
facades. `Sandbox.Editor.DomainPanels` registers the existing Appearance,
Properties, and Selection windows for Mesh, Graph, and PointCloud plus
PointCloud Remove Outliers. It owns their menu paths, lazy per-frame model
cache, texture-bake and property-widget draft state, outlier controls, and
result presentation. K-Means and Progressive Poisson command/config/result
implementations compile in a private runtime facade unit; all other panel
models, processing commands, history/jobs, validation, and result sinks likewise
remain runtime-owned, so app panels expose no geometry, ECS, graphics, or RHI
dependencies. The app also installs the sandbox default runtime policy bundle through
`Runtime::RegisterSandboxDefaultRuntimePolicies(engine)`.
It unregisters the returned handles during shutdown before the engine tears down.
The app remains a runtime-only consumer: `EditorShell` registers with
`Engine::SetImGuiEditorCallback`, reads scene and selection state through
runtime APIs, emits selection and local-transform edit commands through
runtime-owned seams, replaces runtime camera-controller slots through the
engine-owned registry, toggles mesh edge/vertex primitive views through runtime
extraction-cache settings, routes selected-entity spatial-debug options through
`SpatialDebugBinding`, routes material/scalar/color visualization choices
through `VisualizationConfig`, routes visualization adapter bindings through
runtime extraction-cache state, and submits file/import path commands through
`Engine::GetAssetImportPipeline().ImportAssetFromPath(...)`. Asset routing,
decoding, `AssetService` mutation, model-scene materialization, texture-upload
requests, and default import/input policy implementation remain runtime/asset
owned; the sandbox app implementation only composes the runtime-provided
defaults.

`File / Import` is a linear path -> payload-hint -> import workflow. The path
field remains editable whenever the window is bound, while the runtime facade
independently reports whether the payload chooser and import command are ready.
Single-payload formats may keep the `Unknown` hint as automatic resolution;
ambiguous PLY input requires an explicit mesh or point-cloud hint. Disabled
chooser rows and commands expose the runtime-owned prerequisite reason on hover,
including through ImGui's disabled-item hover path, so app code does not carry a
second extension or importer-capability table. The same disabled-tooltip
convention is used by the AssetIO queue's clear and cancel commands.

The `File / Import` editor window also polls
`Engine::GetAssetImportPipeline().GetAssetImportQueueSnapshot()` for the
runtime-owned AssetIO queue. Rows show queued/running/apply/upload/terminal
import stages, payload kind, path basename, elapsed time, determinate progress
where available, indeterminate stage labels where decoder progress is unknown,
and failure/cancellation diagnostics. Clear-completed and cancellable
dropped-geometry commands route back to `Engine::GetAssetImportPipeline()`; the
sandbox app and UI never own asset, ECS, or graphics state.

The promoted editor also exposes stable top-level ImGui menu slots for
`PointCloud`, `Graph`, and `Mesh`. Their submenu items open selected-entity
domain windows for render-hint status, visualization/spatial-debug controls,
primitive-selection details, and processing-discovery affordances. These
windows are registered by the app-owned `Sandbox.Editor.DomainPanels` module
through `Sandbox.Editor.Shell`'s contribution facade backed by
`Runtime.EditorWindowRegistry`; runtime has no fixed Sandbox windows or
presentation state. The panels reuse `Runtime.SandboxEditorFacades` models,
the callback-scoped selected-mesh property view, and runtime-owned command
surfaces, and the sandbox app
still does not own selection, ECS mutation, method jobs, rendering, or asset
state.

`Mesh > Processing > Parameterize (UV)` exposes exactly the four CPU strategies
implemented by the runtime facade: LSCM, harmonic cotangent, uniform Tutte, and
Boundary First Flattening. Its controls keep an explicit panel-local draft for
the selected strategy's typed values; edits remain marked as unapplied until
the user applies or reloads the draft. Applying routes through the validated
`EngineConfig.sandbox.parameterization` preview/apply lane, and running the
configured strategy writes `v:texcoord` through the runtime command-history
path, with undo and redo controls in the same window. No panel-only solver or
configuration path exists.

The parameterization window stores its controls-to-UV split ratio in panel
state and exposes a draggable divider. Its config-backed view controls choose
`CPU layout` or `GPU shaded`, a grid/checker/texel-density/selected-albedo
background, and the optional conformal-distortion heatmap. These values use the
same validated `EngineConfig.sandbox.parameterization.view` preview/apply lane
as config-file and agent callers; they are not panel-only renderer switches.

`CPU layout` is the default and the deterministic fallback. It fits the
pointer-free runtime view model into the available rectangle, draws its
triangles with `ImDrawList`, and provides fit, cursor-centred wheel zoom, and
middle-button pan. Grid and checker render directly in this path;
texel-density and texture requests fall back to checker when a GPU target is
not ready. When `GPU shaded` is requested, runtime resolves the selected
surface's existing GPU geometry and optional resident albedo texture, then
submits copied UV-view data to the renderer-owned retained target. The panel
uses its bindless index only after the matching request and pane extent have
completed a successful `UvViewPass`; while geometry, device, resources, or a
newly resized target are unavailable, the status line reports the reason and
the CPU layout remains visible. A missing texture background falls back to
checker, and a missing face-distortion payload falls back to the plain GPU
fill rather than suppressing the view. Face distortion is submitted only when
the last successful result's canonical topology-to-face, exact-position, and
exact-UV fingerprint still matches the current mesh snapshot, so undo,
regenerated positions/UVs, and topology replacement cannot color a new layout
with stale diagnostics.

GPU submission is refreshed once per visible panel frame. Closing the
parameterization window or hiding the editor therefore disables the gated UV
pass before renderer preparation; reopening it reuses the same retained target
but waits for a newly completed matching frame before publishing its bindless
index. Pane requests larger than 4096 pixels on either axis fail closed to the
CPU layout.

The controls pane reports the last run's strategy, command/solver outcome,
evaluated/skipped/flipped face counts, boundary-edge count, and aggregate
conformal, area, and stretch diagnostics. The optional GPU heatmap consumes
the canonical face-storage-aligned conformal-distortion diagnostic; the panel
still does not synthesize charts or seams. `v:texcoord` writeback updates a 3D
material that already samples the mesh UVs (including an already-bound
UV-checker material), but this presentation panel does not create or bind such
a material. Both render modes remain derived views of the selected mesh, not
new ECS entities or scene cameras; see
[ADR-0025](../../../docs/adr/0025-parameterization-uv-view-and-split-view.md).

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
scene and the app-owned `SandboxEditorController` attached, then asserts canonical default-recipe
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
completed frame, aggregate phase totals, the highest-total phase, and the final
`IDevice::IsOperational()` result observed by the capture wrapper. The capture
wrapper stops the app after the requested frame count, reads
`Engine::GetLastFramePacingDiagnostics()`, and writes only copied runtime
diagnostics; the sandbox app still imports runtime only and does not branch on
Vulkan backend internals. The opt-in CTest
`ExtrinsicSandbox.FramePacingDiagnosticCapture` validates this mode in the
`ci-vulkan` preset. The hosted `ci-vulkan` workflow runs this case separately
under Xvfb so the GLFW window can execute frames even when the runner has no
native display. Vulkan device/extension limitations remain capability
diagnostics handled by the validator; a missing display must not collapse the
capture to zero samples.

## Contents

- `CMakeLists.txt`
- `Editor/Sandbox.DomainPanels.cppm`
- `Editor/Sandbox.DomainPanels.cpp`
- `Editor/Sandbox.MeshProcessingPanels.cppm`
- `Editor/Sandbox.MeshProcessingPanels.cpp`
- `Editor/Sandbox.MethodPanels.cppm`
- `Editor/Sandbox.MethodPanels.cpp`
- `Sandbox.cpp`
- `Sandbox.cppm`
- `main.cpp`
