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
kernel command, event, job, world, service, sim-system, generic frame-hook, and
typed viewport-input-hook seams; it does not expose `Engine&`. The typed
viewport context is separate from the six `FramePhase` values and from
`RuntimeFrameHookContext`: it exists only at the stable post-`UiEndCapture`,
post-render-input-initialization, pre-gizmo insertion point. Module sim systems
join the fixed-step `FrameGraph`
with declared wait/signal labels so registration order does not decide pass
order. Because every `SimSystemContext` exposes the live active world, runtime
also declares `StructuralRead()` for every module sim system. Baseline or
module pass setup that adds/removes components declares `StructuralWrite()`;
component-specific hazards alone do not serialize EnTT's registry-wide
storage-map mutation. Before any per-tick passes can be appended, schedule
finalization rejects
duplicate `(module, system)` identities with `InvalidArgument` and rejects
cyclic or unprovided signal dependencies with `InvalidState`. The schedule seam
remains open through all `OnResolve` callbacks: sim systems and frame hooks may
be registered there when their contribution depends on a resolved service, and
the engine finalizes only after the complete register-plus-resolve contribution
set has been collected. The seam returns validation errors for direct contract
testing; `Engine::Initialize()` logs the error and terminates boot under the
engine's fail-closed initialization policy, so an invalid schedule cannot
execute even once (BUG-070, BUG-071). Module shutdown runs after
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
`EngineSetup` remains the no-`Engine&` capability context. The service
registry's two-phase `Require`/`OnResolve` path and the general module schedule
are conditional, however: each must gain a production consumer during the
convergence work or be removed/narrowed by `RUNTIME-185`. The current schedule
behavior above remains factual until those children land; it is not a
requirement to preserve an unused DAG or registrar.

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
the module provides `ClusteringService`, registers the `RunKMeans` command,
copies active-world geometry into a `JobService` CPU snapshot, publishes a
completion event at the main-thread job gate, commits labels during event pump B,
and emits `ClusterLabelsChanged` as the standing visualization refresh reaction.
`Runtime.Engine.cppm` and `Runtime.Engine.cpp` do not import or name the K-Means
modules. The public `Extrinsic.Runtime.SandboxEditorFacades` surface retains the
K-Means GPU request, submission, result, and status DTOs used for command
injection, while the queue class is private implementation glue attached to that
module. Its Sandbox-owned `JobService` `GpuQueue` participant still records and
drains Vulkan K-Means work inside the normal renderer frame context.

`Extrinsic.Runtime.AsyncWorkModule` is the global app-composed owner for the
persistent `StreamingExecutor` and `DerivedJobRegistry`. Sandbox explicitly
composes it; Engine never imports or names the concrete module. Registration
runs after the boot world exists and before asset/document dependencies borrow
the module's `StreamingExecutor` and `DerivedJobRegistry` services. The module
also provides the existing domain-free `Core::IStreamingFrameHooks` capability,
which lets Engine preserve the core Maintenance contract's exact transfer
collect → async drain/apply → asset tick → async submit/pump order without a
module-specific branch or forwarding object. The capability is optional: an
application that omits the module still performs transfer collection followed
by the asset tick.

`Extrinsic.Runtime.SceneDocumentModule` is the optional app-composed document
owner. Registration binds the exact active `{WorldHandle, Registry*}` and
publishes the concrete module plus its exact owned `EditorCommandHistory`;
resolution optionally discovers `StreamingExecutor`. Document path, last file
event and sequence, history, and queued-operation handles belong to one binding
epoch. A direct active-handle or registry mismatch advances the epoch, cancels
owned tasks, and resets that complete durable state before rebinding. There is
no per-world cache, so switching away and back never restores path, history, or
event identity. Shutdown announcement closes document operations, invalidates
module generation and binding epoch, and drains task ownership while dependent
participants may still release their exact registration handles before reverse
module teardown. Omitting the module leaves Engine and the active world
operational but publishes no document or history capability.

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
   when no handler is registered (ARCH-007), then run runtime-module
   `AfterCommandDrain` hooks;
3. pump the queued kernel event bus (`Engine::Events()`) post-command-drain;
   command-published events become visible before simulation, and events
   published by listeners defer to the next pump per
   [ADR-0024](../adr/0024-kernel-module-architecture.md) D7 (ARCH-008);
4. fixed-step simulation and CPU `FrameGraph` execution: application
   `OnSimTick` contributions are appended first, then the promoted baseline ECS
   system bundle, then module-registered sim systems. This ensures a module
   system that reads a baseline output (e.g.
   `Transform::WorldMatrix`) or waits on the baseline `TransformUpdate` signal is
   ordered after its producer — the core `FrameGraph` preserves insertion order
   for passes that share a resource, so bundle-last would place such a module
   before its producer (BUG-069). `Runtime.ModuleSchedule` then canonicalizes sim
   systems by stable module/pass identity under explicit named-signal
   dependencies so the applied order among modules is independent of module
   registration order (ARCH-011, BUG-066). Each substep finishes with
   `Compile` → `Execute` → `ResetForReplay`: exact repeated descriptors reuse
   topology with freshly bound callbacks, while application/module shape
   changes rebuild transparently (CORE-008);
5. drain `Engine::Jobs()` completions before pump B; `JobService` checks token
   and world-scope cancellation on the main thread, drops suppressed results
   whole, and publishes completion events only for survivors per
   [ADR-0024](../adr/0024-kernel-module-architecture.md) D8 (ARCH-009);
6. pump the queued kernel event bus post-simulation, before UI/extraction;
7. runtime-module `UiBegin` hooks, the variable application tick,
   runtime-module `UiBuild` hooks, then `UiEndCapture` hooks. The optional
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
detach. Sandbox-aware callbacks receive a
frame-local `SandboxEditorContext` from
`Extrinsic.Runtime.SandboxEditorFacades`, never `Engine&`; registered paths are
merged into the menu tree without a fixed runtime enum or draw-switch table.
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
runtime-owned Sandbox presentation are retired. The surviving
`Extrinsic.Runtime.SandboxEditorFacades` module is a presentation-free public
contract for data models, commands, result sinks, and session wiring; its
app-facing lower-layer type spellings are compatibility aliases rather than
new ownership boundaries.

`Extrinsic.Sandbox.Editor.Shell` owns hierarchy, inspector, selection,
file/import, frame-graph, render-recipe/artifact, camera, and visualization
presentation. All core windows are closed by default and use the shared
registry. Mesh Appearance forwards the facade's callback-scoped borrowed
selected-mesh vertex-property view to the runtime-owned generic scalar-property
widget; app presentation does not retain that view or import geometry directly.
`Extrinsic.Sandbox.Editor.MethodPanels` registers
the K-Means windows for PointCloud, Graph, and Mesh plus the PointCloud and Mesh
Progressive Poisson windows from the application layer. Their ImGui state and
result presentation are app-owned, while model construction, command execution,
job queues, config validation, and result publication remain runtime-owned.
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
K-Means and Progressive Poisson facade bodies compile in the private
`Runtime.SandboxMethodFacade.cpp` implementation unit. The K-Means GPU queue
declaration is likewise private implementation glue, with its implementation
unit attached to `Extrinsic.Runtime.SandboxEditorFacades`; only its command DTOs
remain on that public facade. Render-recipe and artifact facades compile in
their own private implementation unit. The app-to-runtime dependency direction
is unchanged.

The internal `RuntimeFrameContext` record carries the data that must survive
between those phases: frame delta, fixed-step interpolation alpha, render frame
index, render input, extraction stats, and the acquired render-world pool slot.
It is intentionally not exported as public runtime API.

Dropped asset imports, Sandbox editor model-scene/texture import commands, and
Sandbox editor scene-file save/open commands use the persistent runtime
`StreamingExecutor` instead of doing file IO or decode/parse/serialize work
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
texture/model-scene handoffs, selection/focus state, stable entity lookup, or
editor document history.

The assets-owned model-scene payload consumed at this boundary is CPU-only. It
identifies the active-scene roots, stores reachable nodes in deterministic
pre-order with column-major local transforms, and lets those nodes reference
shared primitive prototypes. The model-scene handoff materializes one ECS node
entity per reachable node and one primitive leaf per node primitive reference,
preserving authored child order, local transforms, and distinct world-space
instances while reusing the decoded CPU prototype as the source for each
entity-owned `GeometrySources` record. Runtime rejects node matrices that are
non-finite, non-affine, or cannot round-trip through the ECS TRS representation
before creating any scene entities.

After a geometry payload creates an entity, runtime invokes ordered
import-authoring policies, populates the decoded geometry, then invokes ordered
post-import processors with the decoded payload context. Processors may enqueue
deferred work through `StreamingExecutor`, but the main-thread apply boundary
remains the only place that mutates imported ECS or asset state. Once the import
is materialized, ordered import-completed handlers receive the created entity
span plus an optional focus target. Sandbox/default composition installs the
current direct-mesh generated-normal processor, import authoring defaults,
auto-select behavior, optional focus-on-import, and optional `F` action through
`Extrinsic.Runtime.SandboxDefaultPolicies`; a bare `Engine` with no
registrations still materializes geometry without those policies. The app
passes its one optional exact `CameraControllerRegistry` lookup into policy
registration. Import-completed services contain no camera pointer: with the
registry absent, the handler still selects the first valid entity and simply
skips autofocus.
Model-scene imports use the same contract: every primitive leaf is authored as
a mesh in deterministic scene order, then exactly one model-scene completion
receives only those leaves and an aggregate focus target enclosing their finite
world-space bounds. With the sandbox defaults installed, this makes every leaf
renderable and mouse-pick eligible, selects the first leaf, and focuses the
camera once after the complete hierarchy is ready.

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

The global `AsyncWorkModule` reacts to `WorldWillBeDestroyed` at the next
main-thread event pump. Every production streaming/derived descriptor carries
the actual owning `WorldHandle`; the module first cancels matching derived
records and then retires the same generation-qualified scope in the executor.
Retirement cancels queued, running, readback-waiting, and apply-ready records,
rejects later submissions to that retired handle, and is checked again
immediately before a main-thread apply callback is removed from the queue.
Workers retain only copied task inputs plus the handle value and never borrow
`WorldRegistry` or an ECS registry. Reinitializing the module creates a fresh
executor lifetime and clears the retired-scope set; recycling a world index
with a newer generation never inherits retirement state.

Normal apply remains suppressed for every cancelled task. A descriptor whose
consumer owns separate visible control state may opt into
`FinalizeCancellationOnMainThread`; the executor queues that callback exactly
once, drains it outside the executor lock before normal bounded applies, and
does not charge it to the result-apply budget. Asset ingest uses the callback
only to transition its queue record to `Cancelled` and publish the terminal
`RuntimeAssetImportEvent`; scene IO uses it only to publish a terminal failure
event. Neither callback owns decoded payload commit or borrows the retiring
world.

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

Derived-job cancellation is terminal and authoritative. If a running worker
returns an error after cancellation, neither worker-result bookkeeping nor the
main-thread apply path may replace `Cancelled` with `Failed` or `Complete`.

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
pre-render transform/bounds flush (`FlushPreRenderTransformState`, BUG-024), so
focus actions read `World::Bounds` already refreshed for this frame's transform
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

Shutdown is deterministic and runs through `ExecuteShutdownContract`: stop
running, wait idle, application shutdown, then announce runtime shutdown and
run reverse module teardown while dependencies are live; afterward destroy the
scene, asset/GPU-asset handoffs, frame graph, render-extraction plus renderer,
device, window, and scheduler, then clear initialized state. The announcement
lets composed modules invalidate work and detach participant handles before
ordinary reverse shutdown. The Dear ImGui adapter is detached before this
contract while the window and overlay system are still live.

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
output, gizmo undo/scratch/packets, stable lookup binding, and its copied render
snapshot. After replacement it rebuilds lookup against the rebound registry and
publishes empty interaction data. The module owns one validated
`{WorldHandle, Registry*, interaction epoch}` binding; pick sequences remain
monotonic while old-world/old-epoch results fail closed.

`RUNTIME-183.EngineAssetHandoffTransition` is the sole remaining transitional
participant. It clears render extraction, bake, and residency borrowers before
replacement, then reconstructs the exact active-world handoffs and import
dependencies. Engine resolves the optional exact `SelectionController` only at
the initial and replacement dependency wiring sites; omission supplies null.
Active imports cancel before shutdown announcement, and the selection borrow is
replaced with null after the announcement pump and before reverse module
teardown. `RUNTIME-183` owns removal of that implementation-only transition.

Active-world Maintenance is not a document replacement. Engine immediately
clears/rebinds its remaining asset/extraction borrowers in that pass.
`SceneInteractionModule` independently validates the active handle and registry
before every input, extraction, maintenance, and lookup action; a mismatch
performs the same reset/rebind before delayed events arrive and never
resurrects state from the former world. Document operations retain their
separate validated binding.

Scene JSON remains backend-neutral. Supported persistence is limited to current
sandbox-authoring CPU state: metadata names, stable ids, transforms, hierarchy,
selection eligibility, render hints, visualization configs, and
mesh/graph/point-cloud `GeometrySources`. Unsupported families such as lights,
shadow-caster tags, collider/rigid-body descriptors, spatial-debug bindings, and
asset-instance source references are counted in `SceneSerializationStats` but
not materialized on load. Renderer/RHI resources, GPU handles, adapter bindings,
camera controller state, and editor document history are runtime/graphics/editor
state and are not scene-file contents.

## Physics Bridge

`Extrinsic.Runtime.PhysicsBridge` is the concrete runtime-owned ECS/physics
composition seam added by `PHYSICS-001`.

The bridge owns:

- an `Extrinsic.Physics.World` instance;
- a `StableId -> BodyHandle` sidecar keyed by
  `Extrinsic.ECS.Component.StableId`;
- fixed-step accumulator state;
- synchronization, writeback, and ordering diagnostics.

`SyncAuthoring(Registry&)` scans ECS entities with collider or rigid-body
authoring, sorts them by `entt` entity value for deterministic processing,
requires a valid `StableId`, converts ECS collider/rigid-body/transform
descriptors into `Physics::BodyDescriptor`, creates or updates world bodies,
and destroys stale sidecar/world bodies when entities disappear or authoring
becomes invalid. ECS components never receive physics handles.

`TickFixedStep(Registry&, frameDeltaSeconds, config)` runs in this order:

1. synchronize ECS authoring into the physics world;
2. clamp and accumulate frame delta;
3. execute zero or more fixed physics steps with `config.FixedDeltaSeconds`;
4. write dynamic body poses back to ECS transforms;
5. stamp `Transform::IsDirtyTag` and `Transform::WorldUpdatedTag` on dynamic
   writeback.

Static and kinematic bodies are not written back by this bridge; they are
diagnosed as skipped writebacks. Contact event routing is intentionally not
implemented here yet because broadphase/narrowphase contact records are owned
by `PHYSICS-002`.

## Related references

- Historical details: `runtime-subsystem-boundaries.md` (`legacy-background`).
- Physics bridge ownership: [physics.md](physics.md).
- Layer policy: [layering.md](layering.md).
