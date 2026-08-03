# Runtime Architecture

`runtime` is the composition root for IntrinsicEngine.

## Responsibilities

- Construct and wire subsystem boundaries.
- Own lifecycle/state transitions for engine execution.
- Mediate between platform, graphics, assets, ECS, physics, and geometry services.
- Own fixed-step ECS-to-physics synchronization, physics-world stepping, contact/event routing,
  and physics-to-ECS transform writeback.

## Non-responsibilities

- Runtime should not become a utility grab-bag for lower layers.
- Lower layers must remain reusable without runtime internals.
- Physics world/state lives in `physics`; runtime owns only bridge sidecars and scheduling.

## Lifecycle Composition

Sandbox startup creates its app-owned `EngineConfigControl` module before
`Engine` construction and resolves engine configuration through the module's
owned section registry.
`Runtime::ResolveEngineConfigForBoot(args, control.SectionRegistry())` starts from
`CreateReferenceEngineConfig(control.SectionRegistry())`, then checks
`--engine-config`, the
`INTRINSIC_ENGINE_CONFIG` environment variable, and an existing
`config/engine.json` default path. File parsing and generic record diagnostics
remain in the core-owned [`engine config file`](engine-config.md) lane; typed
Sandbox payload codecs live in runtime and the pre-boot registration
composition lives in `Extrinsic.Sandbox.ConfigSections`. Runtime chooses the
boot source, passes the resulting value-type `EngineConfig` into `Engine`, and
moves the same control object into `Engine::AddModule(...)`.

Live agent/CLI configuration uses the app-composed
[`runtime config control`](runtime-config-control.md) module resolved through
`Engine::Services()`. That facade previews render recipes and engine
config documents without ImGui, activates recipes through the same renderer
override path used by startup and the Sandbox Editor, and hot-applies only the current
`render.default_recipe_config_path` plus registered `app.sections` records.
Changed section names are deterministic and callbacks run only after a complete
successful commit. Other engine-config differences remain boot-only and are
reported without mutating the live engine.

Recipe startup remains an Engine responsibility because it must affect frame
zero even when the optional live-control module is absent. Engine builds a
narrow borrowed activation capability after renderer initialization, resets it
unconditionally, and uses shared free functions to apply a non-empty startup
path. A composed control copies the resulting transient startup state during
registration and then owns the persistent live state; omission leaves editor
and agent control unavailable without changing the rendered boot result.

`Engine::RunFrame()` is the promoted runtime lifecycle pipeline. Runtime owns the
cross-layer composition, while reusable phase contracts live in
`Extrinsic.Core.FrameLoop` so `core` stays dependency-free.
The runtime-side hook adapters and per-frame helpers are implementation-local
textual glue in `Runtime.Engine.FrameLoop.Internal.hpp`, included only by
`Runtime.Engine.cpp`; they are not a module surface or an independently owned
subsystem.

Runtime modules compose through `Engine::AddModule(...)` before
`Engine::Initialize()`. Boot sorts modules by stable name, invokes every
`IRuntimeModule::OnRegister(EngineSetup&)`, then invokes every
`IRuntimeModule::OnResolve(EngineSetup&)` after the two-phase
`ServiceRegistry` has all provided services. `EngineSetup` exposes only the
kernel command, event, job, world, and service capabilities; the startup
render-recipe capability and initialized-state observation; and registration-
phase generic frame-hook and typed viewport-input-hook registrars. It does not
expose `Engine&`. Resolve receives the same capability context with both hook
registrars closed, so dependency resolution cannot mutate the finalized hook
shape. The typed viewport context is separate from the six `FramePhase`
values and from
`RuntimeFrameHookContext`: it exists only at the stable post-`UiEndCapture`,
post-render-input-initialization, pre-gizmo insertion point.

The `RUNTIME-185` production audit found ten implementors: nine runtime-owned
responsibilities plus the Sandbox-local optional frame-pacing capture module.
They register exactly seven generic frame hooks (Editor UI: three, texture
bake: one, scene interaction: two, frame-pacing capture: one) and two typed
viewport-input hooks (camera and scene interaction). `RuntimeModuleSchedule`
was the intermediate owner established by that audit. `RUNTIME-203`
subsequently retired its one-consumer BMI: `Engine::Impl` now owns only the
same deterministic frame-hook and viewport-hook records, sorted by
phase/module/registration sequence or module/registration sequence. There is
no module sim-system registrar, descriptor/context, causal signal DAG, helper
schedule object, or fixed-step schedule branch. `PHYSICS-004` subsequently
adds one runtime-owned implementor and one generic `Simulation` hook, bringing
the current totals to eleven implementors and eight generic hooks without
restoring any of those retired scheduling surfaces. Engine directly registers the
three promoted ECS systems as the complete current fixed-step contribution.

Two-phase `Provide`/`Require`/`OnResolve` remains behavior-backed: texture bake,
asset workflow, scene document, editor UI, camera, interaction, and config
control resolve real cross-owner capabilities independent of module insertion
order, and missing required providers fail boot. Engine publishes only the six
built-in capabilities with production lookup consumers: `JobService`,
`RenderExtractionCache`, `RHI::IDevice`, `Platform::IWindow`,
`Graphics::IRenderer`, and `RuntimeInputActionRegistry`. Registry statistics
and a copied boot-error list were removed; fail-closed validation retains
`HasBootErrors`, `LastBootError`, and `ValidateBoot`. Module shutdown runs after
`RuntimeShutdownAnnounced` has been published and pumped. A module that owns a
published service withdraws that exact borrowed instance before destroying it.
`ServiceRegistry::Withdraw(...)` is owner-only and phase-independent so it can
serve both partial-registration rollback and locked shutdown; an expected
missing rollback entry returns an error without adding a boot diagnostic.
Later module shutdown callbacks therefore observe absence rather than a
dangling pointer.

[ADR-0027](../adr/0027-right-sized-runtime-composition.md) distinguishes that
current mechanism from the accepted ownership target. `IRuntimeModule` remains
the current lean type-erased app-to-runtime lifecycle boundary; the bounded
extractions test which real owners need it, and a domain responsibility does
not need a wrapper merely to satisfy the architecture.
`EngineSetup` remains the no-`Engine&` capability context. `RUNTIME-185`
completed the deletion test: behavior-backed two-phase service resolution and
the exact two hook kinds remain, while unused simulation/DAG, phase, registrar,
provision, and diagnostic branches are gone.

## Final Engine boundary

`RUNTIME-186` settled the caller-facing API before representation changed.
Engine no longer re-exports `Runtime.FramePacingDiagnostics` or
`Runtime.InputActions`; callers that name those records import their owning
modules. Input registration goes through the `RuntimeInputActionRegistry`
published during composition. Render-extraction statistics come from the
published `RenderExtractionCache`, visualization binding/revision state comes
from that same owner, and renderer diagnostics remain on `IRenderer`. The
frame graph and render-world pool have no public Engine forwarding surface.
The retained `GetLastFramePacingDiagnostics()` is a read-only kernel
observation with the Sandbox frame-pacing report as its production reader.

`RUNTIME-187` then moved all private state into `Engine::Impl` without changing
those declarations or frame behavior. The public interface now has the exact
convergence snapshot `12/0/0/5`: twelve plain imports, zero domain imports,
zero re-exports, and five allowed `GetX` names. The twelve imports are:

- `Extrinsic.Core.Config.Engine`
- `Extrinsic.RHI.Device`
- `Extrinsic.Platform.Window`
- `Extrinsic.Graphics.Renderer`
- `Extrinsic.Runtime.CommandBus`
- `Extrinsic.Runtime.FramePacingDiagnostics`
- `Extrinsic.Runtime.JobService`
- `Extrinsic.Runtime.KernelEvents`
- `Extrinsic.Runtime.Module`
- `Extrinsic.Runtime.ServiceRegistry`
- `Extrinsic.Runtime.WorldHandle`
- `Extrinsic.Runtime.WorldRegistry`

The exact allowed getter/type/owner set is:

| Getter | Return type | Owning import |
| --- | --- | --- |
| `GetDevice` | `RHI::IDevice&` | `Extrinsic.RHI.Device` |
| `GetEngineConfig` | `const Core::Config::EngineConfig&` | `Extrinsic.Core.Config.Engine` |
| `GetLastFramePacingDiagnostics` | `const RuntimeFramePacingDiagnostics&` | `Extrinsic.Runtime.FramePacingDiagnostics` |
| `GetRenderer` | `Graphics::IRenderer&` | `Extrinsic.Graphics.Renderer` |
| `GetWindow` | `Platform::IWindow&` | `Extrinsic.Platform.Window` |

`tools/repo/check_kernel_convergence.py` compares the exact import and
re-export sets, getter names, return/owning types, and owning imports. A
same-count substitution, an unused/new import, a stale policy entry, or a
getter type change therefore fails instead of fitting under a numerical cap.

Module granularity follows
[ADR-0026](../adr/0026-runtime-module-scope-by-consumer-contract.md) only after
ADR-0024 has established that a responsibility belongs in runtime composition.
Two integrations share a module only when app lifecycle, durable-state scope,
dependency/cancellation/commit ownership, and published-state consumer
reactions are cohesive. Independent composition, incompatible state lifetime,
independently owned commit or cancellation boundaries, or different consumer
meaning requires a split; an extra service or different execution mechanism
alone does not. Algorithm family and result shape alone do not decide the
boundary, and command, status, completion, and diagnostic records stay
method-specific until two production callers prove identical semantics. This
grouping rule does not ratify a C++ wrapper. ADR-0027 records the current
interface's bounded retention and deletion tests; `REVIEW-003` later audits
the resulting live surface.

`Extrinsic.Runtime.ClusteringModule` is the first extracted domain module on
this contract. Sandbox composes it from app startup, not from the kernel engine:
the module provides the sole typed `ClusteringService::RunKMeans` operation,
copies active-world geometry/property identities into a world-scoped snapshot,
and routes CPU-reference or Vulkan-compute work from one request. CPU work uses
`JobService`; Vulkan work uses one private GPU participant, a non-exported
recorder/cache partition, and shared transfer readback. Both paths rejoin the
same cancellation/stale validation gate, publish `KMeansRunCompleted`, commit
labels/colors during the main-thread event pump, and emit
`ClusterLabelsChanged` as the standing visualization refresh reaction.
`Runtime.Engine.cppm` and `Runtime.Engine.cpp` do not import or name the K-Means
module. The focused editor workspace and geometry-operation paths borrow the
service and last typed completion; they own no backend-specific DTO or queue.
Config files, UI, and agent/CLI hot apply use
the registered `sandbox.clustering` section and the same request mapper.

`Extrinsic.Runtime.AsyncWorkModule` is the app-composed lifecycle owner for the
kernel's single persistent `JobService`. Sandbox explicitly composes it; Engine
never imports or names the concrete module. Registration publishes the exact
borrowed kernel service before asset/document dependencies resolve it, and
shutdown withdraws that registration and cancels every surviving job. Engine
drains at most eight completed jobs before the second event pump, so completion
application is deterministic and bounded without a parallel frame-hook path.
An application that omits the module retains the kernel service for direct
Engine use but publishes no app-registry job capability.

`Extrinsic.Runtime.SceneDocumentModule` is the optional app-composed document
owner. Registration binds the exact active `{WorldHandle, Registry*}` and
publishes the concrete module plus its exact owned `EditorCommandHistory`;
resolution optionally discovers `JobService`. Document path, last file
event and sequence, history, and queued-operation handles belong to one binding
epoch. A direct active-handle or registry mismatch advances the epoch, cancels
owned tasks, and resets that complete durable state before rebinding. There is
no per-world cache, so switching away and back never restores path, history, or
event identity. Shutdown announcement closes document operations, invalidates
module generation and binding epoch, and drains task ownership while dependent
participants may still release their exact registration handles before reverse
module teardown. Omitting the module leaves Engine and the active world
operational but publishes no document or history capability.

Undoable entity edits keep their typed capture, validation, atomic application,
and dirty-stamp policy with the owning runtime feature. The runtime-internal
`ExecuteUndoableEntityMutation(...)` template turns those callbacks plus stable
world/entity identity, expected owner state/generations, and typed before/after
values into one generic `EditorCommandHistory` record. Every initial apply,
undo, and redo validates before publishing; rejection leaves both feature state
and the history cursor unchanged. Direct/ICP transforms, coalesced gizmo
transforms, and default or lane-targeted visualization config edits use this
shape. Mesh, graph, and point-cloud vertex-normal publishers use it for both
immediate and queued completion paths, validating exact non-output source
properties plus the optional current normal property. Clustering likewise
captures exact input points and the optional label/color/scalar output cohort;
its CPU and Vulkan completions enter the transaction when document history is
composed, while the module stays independently usable without it.
Progressive Poisson vertex publication stages its four optional scalar
properties and entity visualization before committing them together. One
element-domain lane consumes the existing `Vertices` source for mesh, graph,
and point-cloud entities; it preserves topology, element order, provenance,
non-target properties, and existing surface/edge presentation. Both immediate
and queued lanes reject intervening source, output, topology, or owned
presentation changes before publication and before every history transition.
The snapshot comparison includes the production typed mesh/graph connectivity
records; an erased property type it cannot compare rejects the transition
rather than accepting descriptor equality.
LOP-family consolidation uses the broader property-domain form of the same
contract. Its request carries named finite `vec3` input/output properties on
any resolved mesh, graph, or point-cloud element domain, so a mesh face-center
property is a valid point set without becoming a vertex property or a new
entity. Same-cardinality publication changes only the named output properties;
exact topology and unrelated/custom property storage remain owned by the
source. Mesh/graph count changes fail during the shared availability preflight
before a job is queued. Only a topology-free point-cloud point domain may take
the existing canonical full-source replacement path, with exact undo/redo.
Geometry-presentation slot edits additionally validate and monotonically
advance the presentation recipe generation on apply, undo, and redo instead of
restoring a captured generation and admitting an ABA stale-output match. The
mesh-denoise publisher validates both geometry metadata and the exact live
`v:position` snapshot before each transition. Mesh-curvature publication also
validates that source plus the exact mean, Gaussian, and principal-direction
property snapshots it owns. Remesh, subdivide, and simplify publication
validate geometry metadata plus the complete canonical position/connectivity
state, including before queued output may publish. UV regeneration uses the
same transaction mechanics with its owner-specific semantic source snapshot:
exact live positions, edge/halfedge/face connectivity, and known vertex/face
property values are revalidated before queued publication and every history
transition. Its apply stamp requests the required full GPU rebuild only after
the regenerated topology is published; the other mesh owners stamp their
normal deferred geometry dirty tags after publication. Point-cloud outlier
replacement also uses the transaction with an exact full point-property/deleted
slot snapshot; queued output and undo/redo reject any intervening point
attribute or metadata mutation, and full replacement dirty tags are stamped
only after publication. Parameterization UV publication validates geometry
metadata plus the exact semantic triangle topology, finite positions, and
current optional `v:texcoord` state consumed by the solver. Its initial apply
and every undo/redo transition stamp texcoord/attribute dirtiness only after
the UV property is replaced or removed. Generic render-hint and mesh
primitive-view edits snapshot the complete optional `RenderSurface`,
`RenderEdges`, and `RenderPoints` component cohort. Each transition validates
that exact cohort before replacing it, so an intervening lane edit leaves ECS
and history unchanged. The public history module therefore owns history
mechanics rather than transform, visualization, or primitive-view component
DTOs. Retired `RUNTIME-201`'s production census found no parallel undo stack,
inverse-history hook, specialized mutation builder, or undoable entity edit
outside the common transaction.

Asset import is intentionally outside that undoable editor-mutation set.
Successful scene-changing materialization calls
`EditorCommandHistory::MarkDirty` to advance document dirty/revision state
without adding an undo record; entity creation, authoring defaults, and
post-import enrichment remain one automatic import lifecycle. Deferred
direct-mesh enrichment captures the complete published mesh-source generation:
active domain and topology markers, all vertex/edge/halfedge/face property
metadata and values, deleted counts, and vertex-channel binding generation and
property references. Its world-scoped job reaches the main-thread apply only
while the asset-workflow binding epoch still names the same active world and
scene, the raw entity is still live, its entity-sidecar token still names that
job, and the captured generation matches exactly. Apply and unpublished
finalization resolve the scene through `WorldRegistry` at callback time; they
never retain a scene reference across worker execution. A world switch,
document replacement, destroyed world, recycled entity, or generation mismatch
therefore terminates without targeting retired storage. A stale discard
performs no ECS, history, or selection write. The same sidecar projects
pending/terminal status plus a nonempty reason into the selected-entity
processing model; while pending, that model exposes no geometry-mutating
action. `RUNTIME-200` owns migration into the staged import recipe and must
preserve this validation, lifetime, and readiness contract.

`Extrinsic.Runtime.CameraModule` is the optional app-composed global viewport
owner. During registration it binds `WorldRegistry::ActiveWorld()`, publishes
the exact `CameraControllerRegistry`, subscribes to active-world change and
world-destruction events, and contributes one typed viewport-input hook.
Registry slots, poses, pending transitions, and the optional seed are bound to
exactly one valid `WorldHandle`; every reset clears them before rebinding, even
when handle bits compare equal. The hook repeats the handle check before it
reads config, seed, or controller state, then lazily constructs the configured
main controller, applies capture-gated motion, writes
`RenderFrameInput::Camera`, and consumes the one-shot transition. Shutdown
withdraws the exact registry and resets it invalid. Omitting the module leaves
camera output untouched and does not affect generic input actions, import
selection, non-camera editor models, or reference-content extraction.

The frame order is:

1. poll platform events and handle minimized/resize skip paths;
2. drain the kernel command bus (`Engine::Commands()`) — the single
   pre-simulation mutation window per
   [ADR-0024](../adr/0024-kernel-module-architecture.md) D5; commands enqueue
   thread-safely from any phase and execute here in enqueue order, fail-closed
   when no handler is registered (ARCH-007);
3. pump the queued kernel event bus (`Engine::Events()`) post-command-drain;
   command-published events become visible before simulation, and events
   published by listeners defer to the next pump per
   [ADR-0024](../adr/0024-kernel-module-architecture.md) D7 (ARCH-008);
4. fixed-step simulation and CPU `FrameGraph` execution: the complete promoted
   ECS system bundle appends `TransformHierarchy`, `BoundsPropagation`, and
   `RenderSync`. Each substep finishes with
   `Compile` → `Execute` → `ResetForReplay`: exact repeated descriptors reuse
   topology with freshly bound callbacks (CORE-008);
5. drain `Engine::Jobs()` completions before pump B; `JobService` checks token
   and world-scope cancellation on the main thread, drops suppressed results
   whole, and publishes completion events only for survivors per
   [ADR-0024](../adr/0024-kernel-module-architecture.md) D8 (ARCH-009);
6. pump the queued kernel event bus post-simulation, before UI/extraction;
7. runtime-module `UiBegin`, `UiBuild`, then `UiEndCapture` hooks. The optional
   `EditorUiModule` opens the ImGui frame in `UiBegin`, draws registered
   contributions in `UiBuild`, and closes the frame plus writes capture in
   `UiEndCapture`;
8. build `Graphics::RenderFrameInput`, then dispatch deterministic typed
   viewport-input hooks. Module-name order places optional Camera population
   before optional SceneInteraction gizmo/pick input; both see completed editor
   capture. Flush pre-render transforms, dispatch generic input actions, then
   run `BeforeExtraction`, where SceneInteraction drains one pending pick,
   builds gizmo packets, and submits its copied render snapshot. This is not a
   seventh generic frame phase;
9. execute the render-frame contract: begin frame, runtime render extraction,
   renderer world extraction, prepare, execute, and end frame;
10. present the completed frame;
11. execute maintenance: transfer retirement, optional app-provided streaming
   drain/apply, asset-service tick, optional streaming submit/pump, GPU asset
   cache tick, material texture re-resolution, and render-extraction
   deferred-retire ticks and terminal `JobService` reaping, then runtime-module
   `Maintenance` hooks. SceneInteraction drains completed readbacks here and
   rejects zero/unknown/wrong-world/wrong-epoch sequences before controller or
   refinement work;
12. release the consumed `RenderWorldPool` slot, apply deferred
   `WorldRegistry` active/destroy operations, and finalize the frame clock.

Editor UI contribution is data-driven through
`Extrinsic.Runtime.EditorWindowRegistry`: contributors provide stable ids,
structured menu paths, open state, and draw callbacks, and closed or globally
hidden windows receive no callback. The app-composed
`Extrinsic.Runtime.EditorUiModule` owns the ImGui adapter, graphics overlay,
paired frame hooks, and unsuppressed global `G` visibility action. It requires
only the exact built-in `Platform::IWindow`, `Graphics::IRenderer`, and
`RuntimeInputActionRegistry` services, then publishes an Engine-free
`Extrinsic.Runtime.EditorUiHost`. The host owns the registry and parameterless
frame contributions; it passes neither `Engine&` nor application state to
contributors. The app-owned `Extrinsic.Sandbox.Editor.Shell` resolves that
host during attachment, registers one owned frame contribution plus the ten
core Sandbox windows and app panel registrations, and unregisters them before
detach. Sandbox-aware callbacks receive the app-owned, frame-local
`SandboxEditorContext`, composed from copied runtime snapshots and focused
scene, geometry, visualization, render-recipe, and workspace-query handles,
never `Engine&`; the private all-feature attachment binding remains in runtime.
Registered paths are merged into the menu tree without a fixed runtime enum or
draw-switch table.
The frame loop owns one `EditorInputCaptureSnapshot`, resets it at frame
start, and lends the same value by reference to every hook context.
`EditorUiModule` copies the adapter's completed capture into that value only
after `EndFrame`; typed Camera and SceneInteraction hooks, input actions, and
later hooks consume the same snapshot rather than reading ImGui capture flags
independently.
Omitting the module leaves the value unclaimed and all ImGui pacing counters
zero. Its ImGui context owns a paired ImPlot context.

The interaction-to-render boundary is one
`RuntimeSceneInteractionRenderSnapshot`: a world handle plus owned vectors for
selected render ids and gizmo packets and copied hover identity. Submission
copies caller storage into reusable extraction-owned storage. Extraction
accepts it only for the current world and otherwise supplies empty interaction
data. No controller, module pointer, pick/refinement context, or ECS handle is
retained by graphics.
`Extrinsic.Runtime.EditorPropertyWidgets` keeps scalar-property selector and
finite-sample histogram models CPU-testable while its ImGui/ImPlot draw code and
the manifest-managed `implot` dependency remain private to runtime.
`src/runtime/Editor` contains only these generic host, registry, and property
widget facilities. The former `Extrinsic.Runtime.SandboxEditorUi` module and
runtime-owned Sandbox presentation are retired. Runtime publishes focused
`EditorWorkspaceSnapshots`, `EditorJobProjection`,
`SceneEditingOperations`, `GeometryProcessingOperations`,
`VisualizationEditingOperations`, and `RenderRecipeEditingOperations`
contracts. Their shared detail BMI is implementation-only and direct app
imports are source-ratcheted. The app copies the prepared bindings/snapshot
into `SandboxEditorContext` and `SandboxEditorFrame`, so Sandbox window and
frame composition remain app-owned.
Operation bodies compile with their owning feature modules. The private editor
detail BMI contains only attachment bindings and the workspace-session
declaration; its session implementation is limited to attachment epochs, job
identity/result retention, and prepared-frame lifecycle. Presentation-free
workspace model construction is a separate workspace implementation unit, and
neutral service-to-feature-context projection remains an internal runtime
composition adapter.

`Extrinsic.Sandbox.Editor.Shell` owns hierarchy, inspector, selection,
file/import, frame-graph, render-recipe/artifact, camera, and visualization
presentation. All core windows are closed by default and use the shared
registry. Mesh Appearance forwards the workspace's callback-scoped borrowed
selected-mesh vertex-property view to the runtime-owned generic scalar-property
widget; app presentation does not retain that view or import geometry directly.
`Extrinsic.Sandbox.Editor.MethodPanels` registers
the K-Means and Progressive Poisson windows for PointCloud, Graph, and Mesh
plus the mesh parameterization window from the application layer. All three
Progressive Poisson registrations consume the same copied readiness/config/run
surface; readiness resolves a non-empty finite `vec3` `v:position` property and
supplies a disabled reason without exposing ECS state to the app. Their ImGui state and
result presentation are app-owned, while model construction, command execution,
job scheduling, config validation, and result publication remain runtime-owned.
`Extrinsic.Sandbox.Editor.MeshProcessingPanels` applies the same boundary to ICP
registration, mesh denoise/curvature/remesh/subdivide/simplify, and the
mesh/graph/point-cloud vertex-normal windows. Runtime retains their exported
models, command validation/execution, undo/history integration, derived-job
submission, stale-result rejection, and result sinks; the application owns the
stable registrations, menu paths, lazy per-frame domain-model cache, widget
state, and result presentation.
`Extrinsic.Sandbox.Editor.DomainPanels` owns the ten remaining domain windows:
Appearance, Properties, and Selection for PointCloud, Graph, and Mesh, plus
PointCloud / Processing / Remove Outliers. It preserves their stable ids, menu
paths, titles, closed defaults, controls, per-frame lazy model cache, and
immediate/asynchronous result publication. Runtime retains the exported domain
models, callback-scoped borrowed property view, command/job execution,
UV/outlier result state, and result sinks; the app module imports runtime only.
Progressive Poisson operation bodies and clustering config-control helpers compile
in the private `Runtime.GeometryProcessingOperations.cpp` implementation unit. K-Means
execution goes directly from the app panel to the borrowed `ClusteringService`;
the Vulkan recorder/cache/readback partition is private to
`Extrinsic.Runtime.ClusteringModule`. Render-recipe and artifact operations compile
in their own private implementation unit. The app-to-runtime dependency
direction is unchanged.

The internal `RuntimeFrameContext` record carries the data that must survive
between those phases: frame delta, fixed-step interpolation alpha, render frame
index, render input, extraction stats, and the acquired render-world pool slot.
It is intentionally not exported as public runtime API.

Dropped asset imports, Sandbox editor model-scene/texture import commands, and
Sandbox editor scene-file save/open commands use the persistent runtime
`JobService` instead of doing file IO or decode/parse/serialize work
directly from the platform-event or ImGui-callback phase. Geometry,
model-scene, and texture drops plus queued editor model/texture imports create
ingest records and route diagnostics on the frame thread, run file read/decode
work on the worker lane, then apply the decoded CPU payload from the bounded
main-thread apply drain. Queued editor scene saves copy the persisted ECS
surface into a temporary snapshot registry on the frame thread, then serialize
and write that snapshot on the worker lane. Queued editor scene loads read and
parse into a temporary registry on the worker lane, then run the documented
scene-replacement lifecycle from the same main-thread apply drain. The apply
step is the only place that mutates `AssetService`, ECS scene state,
texture/model-scene residency state, selection/focus state, stable entity lookup, or
editor document history.

The assets-owned model-scene payload consumed at this boundary is CPU-only. It
identifies the active-scene roots, stores reachable nodes in deterministic
pre-order with column-major local transforms, and lets those nodes reference
shared primitive prototypes. The workflow's private model materializer creates one ECS node
entity per reachable node and one primitive leaf per node primitive reference,
preserving authored child order, local transforms, and distinct world-space
instances while reusing the decoded CPU prototype as the source for each
entity-owned `GeometrySources` record. Runtime rejects node matrices that are
non-finite, non-affine, or cannot round-trip through the ECS TRS representation
before creating any scene entities.

`AssetWorkflowModule` is the sole published import service. Every import enters
through one validated `AssetImportRecipe` and emits typed copied results for the
ordered route → decode → CPU materialize → ECS author → postprocess → GPU
residency → complete stages. The copied execution identity carries the request,
world, binding generation, and cancellation generation; malformed ordering,
stale identities, and publication after a terminal stage fail closed. Worker
stages own only copied CPU data and use `JobService`; the bounded main-thread
apply boundary remains the only place that mutates imported ECS or asset state.
Direct synchronous imports produce the same seven-stage trace.

The default recipe authors renderable/selectable state, runs the named
direct-mesh normal/UV/property-texture postprocess, and requests one final
selection/focus action. Optional `SelectionController` and
`CameraControllerRegistry` services decide whether those completion requests
can run. The former import-authoring, postprocessor, and completion callback
registries, their role modules, the exported
`Extrinsic.Runtime.SandboxDefaultPolicies` module, and the public
`AssetImportPipeline` are absent. Sandbox installs only the separate `F` input
action when both optional camera and selection services exist.
Model-scene imports use the same contract: every primitive leaf is authored as
a mesh in deterministic scene order, then exactly one model-scene completion
receives only those leaves and an aggregate focus target enclosing their finite
world-space bounds. With the default recipe, this makes every leaf
renderable and mouse-pick eligible, selects the first leaf, and focuses the
camera once after the complete hierarchy is ready.

### Geometry presentation recipe and extraction

`Extrinsic.Runtime.GeometryPresentation` is the sole general presentation
contract for mesh, graph, point-cloud, composition, and procedural geometry.
Its `GeometryPresentationRecipe` is authored scene intent: shape, lane,
presentation, slot semantics and sources, stable asset ids, canonical
`GeometryPropertyRef` identities, uniform defaults, and generated-output
policy. It contains no readiness, generated result, diagnostic, job, borrowed
property view, ECS identity, graphics handle, or live service pointer.

`GeometryPresentationRuntimeState` is a separate ECS sidecar for operational
slot status, generated assets, diagnostics, and exact recipe/source/output
generations. A scene load always starts this sidecar at its default state.
Scene serialization writes only `GeometryPresentationRecipe` under the
`geometryPresentation` key; it accepts the retired `progressiveRenderData` key
on read for document compatibility without retaining the retired component or
module surface.

Runtime extraction calls the pure
`BuildGeometryPresentationSnapshot(recipe, state, sourceView)` projection and
submits only the resulting copied `GeometryPresentationSnapshot` through the
existing render-world boundary. Uniform defaults and retained previous outputs
are resolved there, while stale source or recipe generations fail closed.
Graphics consumes the copied slot/material/property requests and never sees
the recipe, runtime sidecar, ECS registry, or bake/job ownership. Asset/model
handoff, selected texture baking, object-space normal completion, and Sandbox
models and commands all mutate or observe this same recipe/state pair; no
second feature-specific presentation pipeline remains.

### GPU object-space normal bake lifecycle

`AssetWorkflowModule` owns the object-space normal bake as private runtime
composition, not as an Engine or graphics-domain service. Eligible
non-progressive model, progressive model, direct-mesh, and selected-mesh
producers resolve UVs and normals first, then build one versioned canonical
content identity from the exact packed position bytes, fan-triangulated surface
index order, resolved UV and normal bytes, element counts, and resolved
extent/padding/normal-space/epsilon options. Float `-0` is canonicalized to
`+0`. World and binding epoch, the raw generation-qualified entity handle,
stable render id, presentation/semantic, and expected geometry-presentation
recipe generation form a separate target record so reusable content never hides a
destroyed/recycled entity or scene replacement.

Queue scheduling does not fabricate or expose an output `AssetId`. During the
asset tick, the private service allocates a runtime metadata asset through the
live `AssetService`: reusable identities use a deterministic digest path with
bounded collision probes, while identity-less requests receive distinct
non-reusable paths. Service provenance distinguishes queued, exact pending
`{AssetId, GpuAssetCache generation}`, and exact proven-ready states.
Same-identity pending requests attach as waiters without opening another cache
generation; only a proven generation that is still the cache's current
`Ready` texture may fast-bind.

The production plan provider resolves the target's current extraction surface
and `Graphics::GpuGeometryResidencyView`. It requires the identity's exact
fingerprints, byte/count metadata, nonzero content revision, managed index
buffer, tightly packed bake-readable position/UV/normal/index layouts, and
channel device addresses. It revalidates that residency immediately before
recording, submits pending managed-buffer upload barriers, preserves the live
`SurfaceFirstIndex` for a shared index-buffer slice, and keeps indexed-draw
base vertex zero. Raster and optional dilation commands are recorded by the
existing `JobService` `GpuQueue` participant in the renderer's already-open
frame command context. This path never acquires, submits, or presents a second
frame. Readiness remains conservatively frame-based at
`issueFrame + framesInFlight`; exact-generation publication prevents an older
ticket from stamping or failing a replacement.

Pipeline and extent-keyed dilation resources are retained rather than created
per frame. Fixed entry/byte caps bound queued identities, in-flight/proven
outputs, and dilation scratch; at most one bake records per frame so shared
dilation state is serialized. The output finishes in
`ShaderReadOnly` and includes transfer-source usage for acceptance readback;
dilation scratch records its actual post-use layout for safe reuse.

Completion is a transactional runtime merge. Before mutation it rechecks the
current world/epoch, raw entity lifetime and render id, latest request, exact
cache generation, live geometry content revision/identity, presentation key,
and expected recipe generation. It changes only the material
normal `AssetId` and `ObjectSpaceNormal` metadata, preserving albedo,
metallic-roughness, and emissive, then marks the matching normal-slot runtime
status ready with the generated asset and increments its output generation
without changing authored recipe generation. Any mismatch
rejects the whole completion, leaving vertex-normal shading active; there is no
CPU fallback for a non-operational device, record failure, stale completion, or
capacity rejection. The CPU normal-texture path is compatibility behavior only
for callers that compose no workflow queue.

Scene replacement detaches outgoing queued work and target waiters but retains
already-recorded tickets until their safe completion/retirement frame. The
participant reports retained pipeline, dilation, cache, and provenance state
through `HasInFlightWork()`, so the generic GPU-queue shutdown bridge performs
the existing device-idle boundary before exact pending generations and
generated assets are retired and resource leases are released.

Runtime uses two tiers for CPU work. The fixed-step `FrameGraph` is the
per-substep ECS/system DAG: it runs inside the simulation phase and may
read/write the live active world under the normal frame contract. `JobService` is the
multi-frame background tier from ADR-0024 D8: callers submit immutable snapshots
and a `WorldHandle` scope, workers receive only `JobCancellation`, and workers
deposit opaque result envelopes back into the service. The service-owned
main-thread completion gate runs after fixed-step simulation and before event
pump B; it suppresses cancelled/world-scoped results before publishing any
completion event. Commit handlers therefore run as kernel-event listeners at
pump B, never on worker threads and never by holding live ECS references inside
job work.

Runtime world ownership is split between mechanism and policy per ADR-0024 D2.
`WorldRegistry` is the kernel mechanism: it owns `ECS::Scene::Registry`
instances behind opaque `WorldHandle`s, creates the boot world before frame 0,
tracks exactly one active world, and applies active-world switches or destroy
requests only at the Maintenance boundary. Destroy is two phase: Maintenance
publishes `WorldWillBeDestroyed` and cancels jobs scoped to that world, the
event pumps on a later frame, and only a later Maintenance pass tears the
registry down. Destruction has precedence over activation: requesting activation
of a destroy-pending or destroy-announced world fails with `ResourceBusy`, and
Maintenance discards an earlier queued activation if a later destroy request
means its target is no longer `Live`. After an active-world change is applied,
`Engine` refreshes its
active scene pointer and immediately rebinds scene-borrowing asset handoffs,
import-pipeline dependencies, selection lookup, and stable-entity lookup. This
ordering removes references to the previous registry before its deferred
destruction pass; if no active scene exists, the borrowers and lookups are
detached. Higher-level preview/readiness/switch UX policy is deliberately not in
the registry; later runtime modules compose those behaviors through the kernel
events, jobs, and explicit world handles.

Camera state is not one of those Engine rebinding paths. `CameraModule`
observes `ActiveWorldChanged` and resets the exact published registry to an
empty binding for the new handle; `WorldWillBeDestroyed` invalidates the
binding when it names the current world. The viewport hook also compares its
active handle with `BoundWorld()` before every config/seed/controller read, so
delayed event pumping cannot retain an old pose. No per-world cache exists and
switching away and back never restores state.

Initial reference content is application policy. Sandbox reads the boot-time
`ReferenceSceneConfig` during application initialization, calls the two plain
`BootstrapReferenceScene` / `TeardownReferenceScene` functions, and retains
the exact `{owning WorldHandle, population}` record. It creates content at
most once per initialization, optionally gives that population's seed to the
camera registry, and tears down only through the stored original world when
still live. A retired original world is a safe no-op; the active replacement
world is never used as a substitute. Generic Engine neither interprets this
config section nor owns reference population/seed state.

`WorldRegistry` publishes `WorldWillBeDestroyed` and then asks the kernel
`JobService` to cancel every job scoped to that exact generation-qualified
`WorldHandle`. Queued, running, readiness-gated, and apply-ready records all
converge on the same terminal cancellation path. Workers retain only copied
task inputs plus handle values and never borrow `WorldRegistry` or an ECS
registry.

Normal apply remains suppressed for every cancelled or stale task. A descriptor
whose consumer owns separate visible control state may provide
`FinalizeUnpublishedOnMainThread`; `JobService` invokes it exactly once outside
the service lock when cancellation, dependency cancellation, or stale-result
discard prevents publication. Asset ingest uses the finalizer only to reconcile
its queue record and publish the terminal event; scene IO uses it only to
publish terminal failure state. Neither finalizer owns decoded payload commit
or borrows the retiring world. A worker-visible terminal state is therefore not
alone sufficient for completion: while an unpublished finalizer remains
unsettled, `IsComplete()` returns false and `ReapCompleted()` retains the job.
This makes a drain-until-complete loop close over the finalizer even when an
empty drain races immediately ahead of the worker queuing it.

Active-world asset-import and scene-document operations additionally capture
the submission `{WorldHandle, Scene::Registry*}` pair on the main thread.
Asset-import apply validates the pipeline binding as before.
`SceneDocumentModule` queued callbacks capture only weak shared module state
plus owned operation state, module generation, binding epoch, world, and
registry identity. They first forget their owned task and then directly compare
`WorldRegistry::ActiveWorld()` and `WorldRegistry::Get(world)` immediately
before any commit. The epoch makes an away-and-back switch observable even when
the world handle and scene address are equal again. An active-world switch
without retirement therefore suppresses the stale scene callback; decoded work
cannot be redirected into the new active scene or mutate its path, event, or
history. No callback captures `this` or a raw document-state pointer.

Job cancellation is terminal and authoritative. A worker result that arrives
after cancellation cannot publish or replace the terminal `Cancelled` state.

### Camera focus command

`Extrinsic.Runtime.CameraFocusCommand` is a reusable, deterministic command that
reframes a camera controller so a chosen set of objects is centered and fully
visible. It aggregates the world bounding spheres of the target entities into a
`CameraFocusTarget` — the center of mass (mean of the per-entity centers) and the
largest enclosing extent `max_i(|C − Cᵢ| + Rᵢ)`, so every target is contained —
then routes it to a controller slot via `ICameraController::Focus(...)` and marks
an explicit camera transition. `FocusCameraOnEntities(...)` focuses any object
set; `FocusCameraOnSelection(...)` focuses the current `SelectionController`
selection. Phase 8 of `RunFrame` dispatches registered input actions after the
pre-render transform/bounds flush (direct
`TransformHierarchy` → `BoundsPropagation` → `RenderSync`, BUG-024), so focus
actions read `World::Bounds` already refreshed for this frame's transform
edits. The sandbox default action binds the `F` ("focus") key edge to the
selection wrapper for the `Main` slot only when Sandbox supplied an optional
camera registry during policy registration. Its callback captures that exact
registry; the generic `RuntimeInputActionServices` aggregate has no camera
field. The action suppresses itself while Dear ImGui owns the keyboard and
rebuilds the render camera after a successful focus so the reframed view
reaches extraction the same frame. Without a camera module, `F` is absent
while unrelated actions still dispatch. The per-controller framing distance
math is unchanged and remains owned by the controllers
(`Extrinsic.Runtime.CameraControllers`).

Operational promotion is gated on `RHI::IDevice::IsOperational()` and renderer
resource rebuild success. Vulkan-specific diagnostics are recorded by the Vulkan
backend/runtime breadcrumb path, but runtime frame control does not branch on
Vulkan diagnostics.

Shutdown is deterministic. After pending-command discard, Engine first marks
the initialized-state borrow false and publishes/pumps
`RuntimeShutdownAnnounced` while application, GPU participants, module
providers, renderer, device, and world are still live. Modules use that early
boundary to cancel imports, invalidate bindings, detach provider borrows, and
release strong participant handles. Engine then detaches window callbacks and
runs the Engine-private renderer-hook removal followed by the sole
`JobService::ShutdownGpuQueueParticipants(...)` participant-shutdown/device-
idle boundary. Application shutdown follows while the persistent
`AssetWorkflowModule` and `RuntimeInputActionRegistry` still exist; Sandbox
unregisters its optional `F` action. Ordinary
reverse name-sorted module teardown then shuts down AsyncWork before
AssetWorkflow and destroys providers, followed by world, frame graph,
render-extraction plus renderer, device, window, and scheduler. The Dear ImGui
adapter remains an app-composed module and detaches through ordinary reverse
teardown.

## Scene Replacement Lifecycle

Scene load/new/close operations are runtime-owned lifecycle transitions.
`SceneDocumentModule::LoadSceneFromPath(...)` deserializes into a temporary
`ECS::Scene::Registry`; a parse failure invokes no participant and leaves the
live registry, path, event, and history unchanged. For a successful load, new,
or close, the module snapshots live strong-handle registrations in
name-then-registration order. It advances the binding epoch, invokes every
`BeforeReplace` callback while the outgoing registry is live, clears or moves
the replacement into that registry, invokes every `AfterReplace` callback
against the rebound registry, and only then resets the exact owned history.
The callbacks are synchronous `void` functions: no queued replacement event or
invented rollback protocol exists.

`SceneInteractionModule` retains its own strong participant handle. Before
replacement it cancels any drag while the registry is live and clears
selection/hover tags, pending and in-flight picks, readback contexts/refined
output, gizmo scratch/packets, stable lookup binding, and its copied render
snapshot. Gizmo drag release coalesces the selected transform batch into the
document-owned `EditorCommandHistory`; when that service is absent, transform
dragging is disabled rather than recorded in a second stack. After replacement
the module rebuilds lookup against the rebound registry and publishes empty
interaction data. The module owns one validated
`{WorldHandle, Registry*, interaction epoch}` binding; pick sequences remain
monotonic while old-world/old-epoch results fail closed.

`AssetWorkflowModule` retains its own strong participant handle. Before
replacement it empties import dependencies, clears render-extraction and
normal-bake scene state, and destroys the model-scene handoff while the outgoing
registry remains live. After replacement it rebuilds the handoff and pipeline
dependencies against the exact callback `{WorldHandle, Registry*}`. Its
module-local binding epoch and optional narrow handoff predicate reject delayed
callbacks, direct imports, and queued applies from an old or away-and-back
binding. Shutdown announcement cancels imports, advances that epoch, destroys
the scene handoff, releases the participant, and detaches optional streaming,
selection, history, config, world, extraction, renderer, and device borrows
before provider teardown.

Active-world Maintenance is not a document replacement. The asset module
validates the current world/registry directly before asset ticks and callbacks;
on mismatch it immediately clears/rebinds its own extraction, bake, handoff, and
pipeline state without routing through the document participant.
`SceneInteractionModule` independently validates the active handle and registry
before every input, extraction, maintenance, and lookup action; a mismatch
performs the same reset/rebind before delayed events arrive and never
resurrects state from the former world. Document operations retain their
separate validated binding.

Runtime geometry extraction, method readiness, property resolution, history,
and editor snapshots consume the shared ECS element sources through
`BuildSourceAvailability`. Physical capability and semantic provenance remain
separate. Point-set methods resolve their named typed property through any of
the canonical logical domains; they do not use `VertexSource` or provenance as
the eligibility boundary. Mesh vertices, graph nodes, and point-cloud points
share `VertexSource`, while mesh/graph edges and halfedges and mesh faces expose
their own property sets. Graph algorithms that need adjacency read the real
`HalfedgeSource` connectivity plus the named edge/node properties, and a mesh
may satisfy the same graph contract from its existing sources; runtime neither
reconstructs adjacency from a weaker input nor converts the entity. The
canonical property vocabulary includes `GraphHalfedge` alongside `GraphNode`
and `GraphEdge`; availability, job scopes, property catalogs, scene wire
values, and Sandbox graph-domain models resolve each logical domain to its
physical source. Connectivity properties remain visible as catalogued
internal/connectivity rows and are not offered as visualization attributes.

Scene JSON version 2 remains backend-neutral. Version 2 makes graph halfedge
connectivity mandatory so a loaded graph satisfies the same
`Vertices + Halfedges + Edges` source contract as a freshly materialized one.
The reader and writer reject non-compact graph sources, endpoint indices outside
the vertex range, halfedge counts other than twice the edge count, endpoint/
halfedge-pair disagreement, out-of-range next/previous handles, non-reciprocal
next/previous links, and successor links that do not continue at the target
vertex. Version 1 is rejected rather than upgraded by synthesizing topology. Supported
persistence is limited to current
sandbox-authoring CPU state: metadata names, stable ids, transforms, hierarchy,
selection eligibility, render hints, visualization configs, authored
`GeometryPresentationRecipe` values, and mesh/graph/point-cloud
`GeometrySources`. The writer emits only `geometryPresentation`; the reader
also accepts the retired `progressiveRenderData` key and creates a fresh default
`GeometryPresentationRuntimeState`. Unsupported families such as lights,
shadow-caster tags, collider/rigid-body descriptors, spatial-debug bindings, and
asset-instance source references are counted in `SceneSerializationStats` but
not materialized on load. Presentation readiness, generated outputs,
diagnostics, and generations are likewise runtime-only. Renderer/RHI resources,
GPU handles, transient per-entity visualization recipes, camera controller state, and editor document
history are runtime/graphics/editor state and are not scene-file contents.

## Physics Module

`Extrinsic.Runtime.PhysicsModule` is the optional app-composed ECS/physics
owner. Sandbox places it after `EngineConfigControl`; `Engine` does not import
or instantiate the concrete module. Instead, the engine invokes the generic
`FramePhase::Simulation` hook immediately after its promoted ECS fixed-step
bundle and before post-simulation event delivery and the pre-render transform
flush.

The module privately owns one state record per encountered `WorldHandle`:

- an `Extrinsic.Physics.World`;
- a `StableId -> BodyHandle` sidecar;
- a fixed-step accumulator and descriptor-sync generation.

Each simulation hook deterministically synchronizes authored collider,
rigid-body, and transform descriptors; updates only changed descriptors;
executes a bounded number of `Physics::World::SolveStep` calls; and writes only
dynamic poses back to ECS with the standard transform dirty/update tags.
World-destroy and shutdown events remove the private states. Disabling the
module also clears every state, so the default-disabled configuration owns no
physics world.

The public surface is deliberately narrow: validated `PhysicsModuleConfig`
apply and copied `PhysicsModuleSnapshot` diagnostics. No physics world, body
handle, universal physics service, or unused module publication crosses the
runtime boundary. The former
test-only `Extrinsic.Runtime.PhysicsBridge` public module is retired.

## Related references

- Historical details: `runtime-subsystem-boundaries.md` (`legacy-background`).
- Physics module ownership: [physics.md](physics.md).
- Layer policy: [layering.md](layering.md).
