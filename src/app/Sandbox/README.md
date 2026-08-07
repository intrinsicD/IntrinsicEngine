# App/Sandbox

This directory contains the `Sandbox` module/files.

`Sandbox` is the generic reference integration target. `main.cpp` is the visible
composition root: it constructs `Runtime::Engine`, adds each concrete runtime
module (including separate `SceneDocumentModule` and
`SceneInteractionModule` values), initializes the kernel, installs initial app
state, runs, and tears down explicitly. `Sandbox.cppm` exports only the concrete
app-owned `SandboxSession`; it owns reference-content/default-policy/editor
state and receives narrow config/world/service capabilities rather than an
`Engine&`. Engine feature wiring, frame phases, and subsystem behavior remain
in `Runtime` or lower engine layers. The executable obtains its default
configuration through `Runtime` and does not import lower layers directly.

Shutdown is deliberately two-stage. `Engine::BeginShutdown()` discards final
commands, publishes the shutdown announcement, drains GPU participants, and
waits for device idle while worlds and module services are still live.
`SandboxSession::Shutdown()` then removes app-owned policies/editor/reference
content, after which `Engine::Shutdown()` reverses modules and tears down the
kernel. Frame-pacing capture writes into main-owned report state, so the report
remains valid after its module has been destroyed. The executable imports
`Extrinsic.Runtime.FramePacingDiagnostics` directly; Engine no longer
re-exports that record. Its read-only
`GetLastFramePacingDiagnostics()` remains one of the five exact kernel
observations because this report is its production reader. Sandbox default
input policy similarly imports `Extrinsic.Runtime.InputActions` and registers
against the published `RuntimeInputActionRegistry`, with no Engine forwarding
method.
`Sandbox.ConfigSections` is the pre-boot composition surface for the current
`sandbox.clustering`, `sandbox.progressive_poisson`,
`sandbox.parameterization`, `sandbox.point_cloud_consolidation`, and
`sandbox.physics` records.
Their runtime feature modules own the typed DTOs/codecs. `main.cpp` constructs the
app-composed `Runtime::EngineConfigControl` first, gives it the registered
Sandbox section registry, resolves boot config through that exact control's
`SectionRegistry()`, and then moves the same control object into
`Engine::AddModule(...)`.

Sandbox constructs the optional `Runtime::PhysicsModule` before that registry
so the `sandbox.physics` post-commit callback can target the exact composed
instance, then adds config control before physics for order-independent service
resolution. Physics is disabled in the reference config. While disabled it
owns no physics world; when enabled, its generic simulation hook performs
per-world ECS authoring sync, bounded `SolveStep` execution, and dynamic-only
transform writeback. No dedicated physics window exists, and any future UI
must use the same validated engine-config lane as file, agent/CLI, and
programmatic callers.

The editor session resolves live recipe/config control through
`Engine::Services().Find<EngineConfigControl>()`. When a host omits the optional
module, startup recipe-file behavior remains active in `Engine::Initialize()`,
but editor recipe and engine-config command surfaces truthfully remain
unavailable with null state and empty callbacks.

Sandbox also composes the optional `Runtime::SceneDocumentModule`. The
presentation-free scene operation path resolves that exact module and its exact owned
`Runtime::EditorCommandHistory` through `Engine::Services()`; it never calls an
Engine document/history/scene getter. `File / Scene` save/open/new/close,
last-file-event presentation, dirty state, undo, and redo therefore share the
module's one validated active-world binding. Sandbox also composes
`AsyncWorkModule`, so save/open use queued snapshot/parse work and commit only
after main-thread generation/world/registry revalidation. At the runtime
contract the async capability remains optional: synchronous document operations
still work without it, while queued operations return `InvalidState`.
Omitting `SceneDocumentModule` leaves Engine and the active world operational
and makes the document/history callbacks explicitly unavailable.

The existing `view.frame_graph` window owns GPU-profiling presentation; no
profiling-specific window or editor contribution is registered. Its default-off
toggle copies the active engine config, sets
`render.enable_gpu_profiling`, serializes and previews the candidate, and then
uses the existing hot-apply callback with the Editor source. Rejection
preserves the committed value and displays config-control diagnostics. If the
optional control state or either preview/apply callback is unavailable, the
toggle is disabled while renderer statistics remain readable. The same panel
shows the profiler status/source, fresh or stale state, resolved submitted-frame
key/slot/age, per-queue envelopes, and named pass rows. It never presents a
summed cross-queue duration, and the displayed timestamps are diagnostics
rather than a performance claim.

Sandbox separately composes optional `Runtime::SceneInteractionModule`. It
owns one active-world interaction cohort—selection, stable lookup, pick
readback/refinement, gizmo drag/undo/scratch/packets—and publishes the exact
module plus exact `SelectionController`. The editor operations and app default policy
resolve those services once; they never call an Engine interaction getter.
Camera and completed UI capture reach interaction through deterministic typed
viewport hooks. Render extraction receives only a copied, world-tagged
selection/hover/gizmo snapshot and treats omission or mismatch as empty.
Document New/Load/Close, active-world switch/retirement, and shutdown clear the
cohort without resurrecting old state.

Sandbox also explicitly composes optional `Runtime::AssetWorkflowModule` after
the document and interaction owners. The module is the exact published import
service and owns the per-boot asset authority, GPU residency, private staged
import executor, and object-space-normal-bake state that must survive async
drain. Editor consumers resolve the workflow and its published asset services
locally. Omitting the module leaves the generic
Engine, world, renderer, transfer, async, and render-extraction geometry
maintenance paths operational; asset commands are unavailable and platform
drops fail closed.

The app-owned `Sandbox.Editor.Controller` owns `Sandbox.Editor.Shell` and all
panel-family lifetimes behind one attach/detach interface. Sandbox composes the
optional `Runtime.EditorUiModule`; the shell resolves its Engine-free
`Runtime.EditorUiHost` after attachment, owns one frame contribution and every
registered window handle, and unregisters them before detach. The shell owns
the ten core Sandbox windows, menu composition, ImGui state, and frame
presentation while the module owns adapter/overlay/action/hook lifecycle.
Runtime exposes presentation-free workspace snapshots, job projections, and
focused scene, geometry, visualization, and render-recipe operations. The shell
copies prepared bindings/snapshots into app-owned `SandboxEditorContext` and
`SandboxEditorFrame` records. `Sandbox.Editor.MethodPanels` owns the K-Means,
Progressive Poisson, parameterization, and point-cloud consolidation ImGui
controls and registers eight
domain windows through the shell's context-aware contribution seam. Progressive
Poisson appears under Mesh, Graph, and PointCloud Processing and all three
registrations share one copied readiness model, validated config/apply path,
run action, result readout, and source-cardinality visualization-channel state.
The panel orders the selected entity's existing finite `Vertices`; it has no
surface-sampling, conversion, or topology-replacement path. The app continues
to import the same runtime module surface. The single consolidation entry is a
semantic point-set window rather than a provenance window: its Position and
optional Normal slots enumerate compatible typed properties from every
resolved mesh, graph, and point-cloud element domain, and its output slots keep
the chosen originating domain. While a consolidation request is queued, the
panel names the analysis, backend execution, and publication pipeline. The
terminal readout shows requested/selected Auto ranks, any work-budget backoff,
actual backend/fallback, and measured average/maximum displacement.
`Sandbox.Editor.MeshProcessingPanels`
owns the ICP registration window plus the mesh denoise, curvature, remesh,
subdivide, simplify, and mesh/graph/point-cloud vertex-normal windows. It
registers those nine windows under their existing menu paths, owns their ImGui
input/result-presentation state, and consumes only runtime snapshots and typed
operations. `Sandbox.Editor.DomainPanels` registers the existing Appearance,
Properties, and Selection windows for Mesh, Graph, and PointCloud plus
PointCloud Remove Outliers. It owns their menu paths, lazy per-frame model
cache, texture-bake and property-widget draft state, outlier controls, and
result presentation. K-Means and Progressive Poisson command/config/result
implementations compile in a private runtime operation unit; all other feature
snapshots, processing commands, history/jobs, validation, and result sinks likewise
remain runtime-owned, so app panels expose no geometry, ECS, graphics, or RHI
dependencies. Import authoring, direct-mesh postprocess, selection, and focus
are fields of the runtime-owned `AssetImportRecipe`; Sandbox owns no import
callback registry or registration handles. The app installs only its separate
default `F` action without an exported lifecycle owner. It resolves the built-in
`Runtime::RuntimeInputActionRegistry` plus optional
`Runtime::CameraControllerRegistry` and `Runtime::SelectionController`; the
action is registered only when both optional services exist, and teardown
unregisters that exact handle.
The app remains a runtime-only consumer: `EditorShell` registers its
parameterless frame contribution and windows through the resolved host, reads
scene and selection state through runtime APIs, emits selection and
local-transform edit commands through
runtime-owned seams, replaces runtime camera-controller slots through the
optional service-registry lookup, toggles persistent mesh edge/vertex primitive
views by authoring ECS `RenderEdges` / `RenderPoints` through runtime
command/history seams, routes material/scalar/color visualization choices
through `VisualizationConfig`, routes copied visualization recipes through
runtime extraction-cache state, and submits frame-driven file/import commands
through the exact published `AssetWorkflowModule::QueueAssetImport(...)` with a
validated recipe. Every supported `File / Import` payload is
therefore queued before decode; the worker reads and decodes while bounded
main-thread completion owns `AssetService`, ECS, selection, focus, and document
history mutation. The direct `ImportAssetFromPath(...)` API remains synchronous
for explicit non-frame callers and is not used by the Sandbox ImGui callback.
Asset routing, decoding, materialization, texture-upload requests, and default
import recipe policy remain runtime/asset owned; the sandbox app only requests
the workflow.

Mesh import preserves the source topology. A closed manifold file materializes
as an entity with the same vertex, edge, halfedge, and face counts and no
boundary; generated UV-atlas seams are published as `h:texcoord` on the corner
domain rather than by splitting the mesh, and the per-vertex duplication an
indexed GPU vertex buffer needs happens at upload. The enrichment diagnostic on
the imported entity, and the matching `Direct mesh enrichment applied` log line,
report the resolved counts, which element domain owns the UVs, the atlas
provenance and backend, and how many vertices the GPU upload duplicates, so that
duplication is never silent. See `BUG-137`.

`File / Import` is a linear path -> payload-hint -> import workflow. The path
field remains editable whenever the window is bound, while the runtime scene snapshot
independently reports whether the payload chooser and import command are ready.
Single-payload formats may keep the `Unknown` hint as automatic resolution;
ambiguous PLY input requires an explicit mesh or point-cloud hint. Disabled
chooser rows and commands expose the runtime-owned prerequisite reason on hover,
including through ImGui's disabled-item hover path, so app code does not carry a
second extension or importer-capability table. The same disabled-tooltip
convention is used by the AssetIO queue's clear and cancel commands.

The `File / Import` editor window also polls
`AssetWorkflowModule::GetAssetImportQueueSnapshot()` through exact service
discovery for the
runtime-owned AssetIO queue. Rows show queued/running/apply/upload/terminal
import stages, payload kind, path basename, elapsed time, determinate progress
where available, indeterminate stage labels where decoder progress is unknown,
and failure/cancellation diagnostics. Clear-completed and cancellable
manual or dropped import commands route back to
that same published service; the sandbox app and UI never own asset, ECS,
or graphics state.

The promoted editor also exposes stable top-level ImGui menu slots for
`PointCloud`, `Graph`, and `Mesh`. Their submenu items open selected-entity
domain windows for render-hint status, visualization/spatial-debug controls,
primitive-selection details, and processing-discovery affordances. These
windows are registered by the app-owned `Sandbox.Editor.DomainPanels` module
through `Sandbox.Editor.Shell`'s contribution seam backed by
`Runtime.EditorWindowRegistry`; runtime has no fixed Sandbox windows or
presentation state. The panels reuse focused runtime feature snapshots,
the callback-scoped selected-mesh property view, and runtime-owned command
surfaces, and the sandbox app
still does not own selection, ECS mutation, method jobs, rendering, or asset
state.

`Mesh`, `Graph`, and `PointCloud` each expose a stable `Processing >
Consolidate (LOP/WLOP/CLOP/EAR)` entry. All three open the same panel path and
shared state; the menu location is only a discovery surface and does not gate
the selected entity's provenance. A Position slot accepts any finite `vec3`
property in the selected entity's canonical property catalog, including mesh
`v:position` or `f:centroid`, and an optional Normal slot accepts a
count-matched `vec3` on the same element domain. Named Position/Normal outputs
publish on that domain without a property alias or converted entity. Shared
controls cover CPU-reference/Vulkan-compute backend intent, Auto/Manual support radius, sampled-neighbor/contribution
limits, repulsion, stopping criteria, target count, and seed; strategy-specific
controls cover WLOP anisotropy, CLOP mixture fitting, and EAR normal policy,
refinement, and edge sensitivity. The window keeps a panel-local draft and
validates it through `Runtime.GeometryProcessingOperations` before applying the
registered `sandbox.point_cloud_consolidation` section with the `Editor`
source. Running submits those full property references and that same typed
config to
`PointCloudConsolidationService`; completion reports requested and actual
backend identities, explicit fallback diagnostics, strategy, resolved radius,
profile occupancy/work, convergence/displacement, normal, and insertion
diagnostics. Vulkan is selectable for ordinary LOP and isotropic WLOP; the
shared preflight disables anisotropic WLOP, CLOP, and EAR Vulkan pairs rather
than silently substituting another method. The runtime then
performs the single `GeometrySources`
mutation, so the viewport observes the originating entity and the window's
undo/redo affordances use the shared document history. The Run button uses the
runtime's canonical property/publication preflight: same-cardinality mesh and
graph requests are accepted, while topology-bearing count changes are disabled
with the exact runtime diagnostic. If config control, the service, a selected
compatible property, or a valid strategy is unavailable, controls fail closed
rather than using a panel-private path.

`Mesh > Processing > Parameterize (UV)` exposes exactly the four CPU strategies
implemented by `Runtime.GeometryProcessingOperations`: LSCM, harmonic cotangent, uniform Tutte, and
Boundary First Flattening. Its controls keep an explicit panel-local draft for
the selected strategy's typed values; edits remain marked as unapplied until
the user applies or reloads the draft. Applying routes through the validated
registered `sandbox.parameterization` preview/apply lane, and running the
configured strategy writes `v:texcoord` through the runtime command-history
path, with undo and redo controls in the same window. No panel-only solver or
configuration path exists.

The parameterization window stores its controls-to-UV split ratio in panel
state and exposes a draggable divider. Its config-backed view controls choose
`CPU layout` or `GPU shaded`, a grid/checker/texel-density/selected-albedo
background, and the optional conformal-distortion heatmap. These values use the
same validated `view` payload in the registered
`sandbox.parameterization` preview/apply lane
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

With the standard reference configuration, Sandbox calls the plain
`Runtime::BootstrapReferenceScene(...)` function exactly once during
application initialization. It retains the original `{WorldHandle,
ReferenceScenePopulation}` for teardown, optionally hands the population's
camera seed to the composed camera registry, and never tears it down through a
replacement world. If the original world has retired, shutdown is a safe
no-op. The private triangle implementation creates `ReferenceTriangle` as an
ordinary ECS mesh-domain `GeometrySources` entity with `RenderSurface`,
durable `StableId`, `Selection::SelectableTag`, and white
`VisualizationConfig`. Reference content renders without `CameraModule`.

The default module list explicitly composes `PhysicsModule`, `AsyncWorkModule`,
`CameraModule`, `ClusteringModule`, `PointCloudConsolidationModule`, `EditorUiModule`,
`SceneDocumentModule`, and `SceneInteractionModule`, followed by
`AssetWorkflowModule`. Camera remains optional at the runtime contract:
when omitted, Sandbox policy registration omits `F` and autofocus, editor
camera controls report unavailable, and workflow-provided import auto-selection
plus non-camera behavior continue. When interaction/selection alone is omitted,
`F` is absent but a present camera may still consume an import focus target;
materialization does not require selection. Scene document and scene
interaction remain independently optional as described above; without
interaction, generic input/rendering and component-driven primitive views
continue while selection/gizmo surfaces report unavailable. Asset workflow is
independently optional to generic Engine and render-extraction maintenance, but
requires the document/history services when it is composed.

During shutdown, the announcement first cancels imports and detaches their
provider borrows, then the generic GPU-participant bridge drains and performs
any required device-idle wait. Sandbox application shutdown next detaches the
editor and unregisters its exact `F` action while the input registry remains
live. Reverse AsyncWork/AssetWorkflow module and provider teardown follows.
Repeated app shutdown sees an empty handle record and is a no-op.

## Build presets

- `cmake --preset ci` configures the headless CPU/null gate (Sandbox executable
  disabled, promoted Vulkan disabled). With tests enabled it still builds
  `ExtrinsicSandboxEditor` and the `integration;runtime` app-composition tests,
  including the pre-boot config registry plus Null `Engine::Run()` proof.
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
- `Sandbox.ConfigSections.cppm`
- `Sandbox.ConfigSections.cpp`
- `Sandbox.cpp`
- `Sandbox.cppm`
- `main.cpp`
