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

Sandbox startup resolves engine configuration before `Engine` construction.
`Runtime::ResolveEngineConfigForBoot(...)` starts from
`CreateReferenceEngineConfig()`, then checks `--engine-config`, the
`INTRINSIC_ENGINE_CONFIG` environment variable, and an existing
`config/engine.json` default path. File parsing and diagnostics remain in the
core-owned [`engine config file`](engine-config.md) lane; runtime only chooses
the boot source and passes the resulting value-type `EngineConfig` into
`Engine`.

Live agent/CLI configuration uses the runtime-owned
[`runtime config control`](runtime-config-control.md) subsystem exposed through
`Engine::GetConfigControl()`. That facade previews render recipes and engine
config documents without ImGui, activates recipes through the same renderer
override path used by startup and the Sandbox Editor, and hot-applies only the current
`render.default_recipe_config_path` and `sandbox.progressive_poisson`
engine-config subset. Other engine-config differences remain boot-only and are
reported without mutating the live engine.

`Engine::RunFrame()` is the promoted runtime lifecycle pipeline. Runtime owns the
cross-layer composition, while reusable phase contracts live in
`Extrinsic.Core.FrameLoop` so `core` stays dependency-free.

Runtime modules compose through `Engine::AddModule(...)` before
`Engine::Initialize()`. Boot sorts modules by stable name, invokes every
`IRuntimeModule::OnRegister(EngineSetup&)`, then invokes every
`IRuntimeModule::OnResolve(EngineSetup&)` after the two-phase
`ServiceRegistry` has all provided services. `EngineSetup` exposes only the
kernel command, event, job, world, service, sim-system, and frame-hook seams; it
does not expose `Engine&`. Module sim systems join the fixed-step `FrameGraph`
with declared wait/signal labels so registration order does not decide pass
order. Before any per-tick passes can be appended, schedule finalization rejects
duplicate `(module, system)` identities with `InvalidArgument` and rejects
cyclic or unprovided signal dependencies with `InvalidState`. The schedule seam
returns these errors for direct contract testing; `Engine::Initialize()` logs
the error and terminates boot under the engine's fail-closed initialization
policy, so an invalid schedule cannot execute even once (BUG-070). Module
shutdown runs after `RuntimeShutdownAnnounced` has been published and pumped.

`Extrinsic.Runtime.ClusteringModule` is the first extracted domain module on
this contract. Sandbox composes it from app startup, not from the kernel engine:
the module provides `ClusteringService`, registers the `RunKMeans` command,
copies active-world geometry into a `JobService` CPU snapshot, publishes a
completion event at the main-thread job gate, commits labels during event pump B,
and emits `ClusterLabelsChanged` as the standing visualization refresh reaction.
`Runtime.Engine.cppm` and `Runtime.Engine.cpp` do not import or name the K-Means
modules; direct Vulkan K-Means queue ownership remains with the existing Sandbox
editor GPU participant path until the `RUNTIME-137` GPU-job target follow-up.

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
4. fixed-step simulation and CPU `FrameGraph` execution: the promoted baseline
   ECS system bundle is appended **first**, before module-registered sim
   systems, so a module system that reads a baseline output (e.g.
   `Transform::WorldMatrix`) or waits on the baseline `TransformUpdate` signal is
   ordered after its producer — the core `FrameGraph` preserves insertion order
   for passes that share a resource, so bundle-last would place such a module
   before its producer (BUG-069). `Runtime.ModuleSchedule` then canonicalizes sim
   systems by stable module/pass identity under explicit named-signal
   dependencies so the applied order among modules is independent of module
   registration order (ARCH-011, BUG-066);
5. drain `Engine::Jobs()` completions before pump B; `JobService` checks token
   and world-scope cancellation on the main thread, drops suppressed results
   whole, and publishes completion events only for survivors per
   [ADR-0024](../adr/0024-kernel-module-architecture.md) D8 (ARCH-009);
6. pump the queued kernel event bus post-simulation, before UI/extraction;
7. ImGui begin-frame, variable application tick, runtime-module `UiBuild`
   hooks, and ImGui end-frame;
8. build `Graphics::RenderFrameInput`, update the active camera controller,
   dispatch registered input actions, drain one coalesced selection pick, and
   run runtime-module `BeforeExtraction` hooks;
9. execute the render-frame contract: begin frame, runtime render extraction,
   renderer world extraction, prepare, execute, and end frame;
10. present the completed frame;
11. execute maintenance: transfer retirement, streaming drain/apply/submit/pump,
   asset-service tick, GPU asset cache tick, material texture re-resolution, and
   render-extraction deferred-retire ticks, terminal `JobService` reaping, and
   runtime-module `Maintenance` hooks;
12. drain completed pick readbacks through the incrementally maintained
   stable-entity lookup, release the consumed `RenderWorldPool` slot, apply
   deferred `WorldRegistry` active/destroy operations, and finalize the frame
   clock.

Editor UI contribution is data-driven through
`Extrinsic.Runtime.EditorWindowRegistry`: contributors provide stable ids,
structured menu paths, open state, and draw callbacks, and closed or globally
hidden windows receive no callback. `Extrinsic.Runtime.EditorUiHost` owns the
generic Engine callback attachment, the registry, and the unsuppressed global
`G` visibility action; its parameterless frame callback does not pass
`Engine&` to contributors. During the ARCH-006 migration,
`SandboxEditorUi::{Register,Unregister}EditorWindow` delegates to that host as
the compatibility composition surface. Sandbox-aware contributions receive a
frame-local `SandboxEditorContext` facade, never `Engine&`; registered paths are
merged into the menu tree without adding legacy enum or draw-switch entries.
`ImGuiAdapter` records one
`EditorInputCaptureSnapshot` after the visible editor callback; camera, gizmo,
picking, and viewport routing consume that snapshot rather than reading ImGui
capture flags independently. Its ImGui context owns a paired ImPlot context.
`Extrinsic.Runtime.EditorPropertyWidgets` keeps scalar-property selector and
finite-sample histogram models CPU-testable while its ImGui/ImPlot draw code and
the manifest-managed `implot` dependency remain private to runtime.
`SandboxEditorUi` registers `Mesh / Appearance` and
`Mesh / Processing / Simplify` as the first two registry-owned windows; they
share one lazy mesh-domain model per frame, and Appearance embeds the generic
vertex-property histogram. `Extrinsic.Sandbox.Editor.MethodPanels` registers
the K-Means windows for PointCloud, Graph, and Mesh plus the PointCloud and Mesh
Progressive Poisson windows from the application layer. Their ImGui state and
result presentation are app-owned, while model construction, command execution,
job queues, config validation, and result publication remain runtime-owned.
Those K-Means and Progressive Poisson facade bodies compile in the private
`Runtime.SandboxMethodFacade.cpp` implementation unit; the public
`Extrinsic.Runtime.SandboxEditorUi` surface and the app-to-runtime dependency
direction are unchanged.
The remaining fixed domain windows stay behind the legacy section table until
later `ARCH-006` slices relocate them.

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
After a geometry payload creates an entity, runtime invokes ordered
import-authoring policies, populates the decoded geometry, then invokes ordered
post-import processors with the decoded payload context. Processors may enqueue
deferred work through `StreamingExecutor`, but the main-thread apply boundary
remains the only place that mutates imported ECS or asset state. Once the import
is materialized, ordered import-completed handlers receive the created entity
span plus an optional focus target. Sandbox/default composition installs the
current direct-mesh generated-normal processor, import authoring defaults,
focus-on-import handler, and auto-select behavior through
`Extrinsic.Runtime.SandboxDefaultPolicies`; a bare `Engine` with no
registrations still materializes geometry without those policies.

Runtime uses two tiers for CPU work. The fixed-step `FrameGraph` is the
per-frame ECS/system DAG: it runs inside the simulation phase and may read/write
the live active world under the normal frame contract. `JobService` is the
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
registry down. After an active-world change is applied, `Engine` refreshes its
active scene pointer and immediately rebinds scene-borrowing asset handoffs,
import-pipeline dependencies, selection lookup, and stable-entity lookup. This
ordering removes references to the previous registry before its deferred
destruction pass; if no active scene exists, the borrowers and lookups are
detached. Higher-level preview/readiness/switch UX policy is deliberately not in
the registry; later runtime modules compose those behaviors through the kernel
events, jobs, and explicit world handles.

### Camera focus command

`Extrinsic.Runtime.CameraFocusCommand` is a reusable, deterministic command that
reframes a camera controller so a chosen set of objects is centered and fully
visible. It aggregates the world bounding spheres of the target entities into a
`CameraFocusTarget` — the center of mass (mean of the per-entity centers) and the
largest enclosing extent `max_i(|C − Cᵢ| + Rᵢ)`, so every target is contained —
then routes it to a controller slot via `ICameraController::Focus(...)` and marks
an explicit camera transition. `FocusCameraOnEntities(...)` focuses any object
set; `FocusCameraOnSelection(...)` focuses the current `SelectionController`
selection. Phase 4 of `RunFrame` dispatches registered input actions after the
pre-render transform/bounds flush (`FlushPreRenderTransformState`, BUG-024), so
focus actions read `World::Bounds` already refreshed for this frame's transform
edits. The sandbox default action binds the `F` ("focus") key edge to the
selection wrapper for the `Main` slot when installed by sandbox/default
composition, suppresses it while Dear ImGui owns the keyboard, and rebuilds the
render camera after a successful focus so the reframed view reaches extraction
the same frame. The per-controller framing distance math is unchanged and
remains owned by the controllers
(`Extrinsic.Runtime.CameraControllers`).

Operational promotion is gated on `RHI::IDevice::IsOperational()` and renderer
resource rebuild success. Vulkan-specific diagnostics are recorded by the Vulkan
backend/runtime breadcrumb path, but runtime frame control does not branch on
Vulkan diagnostics.

Shutdown is deterministic and runs through `ExecuteShutdownContract`: stop
running, wait idle, application shutdown, streaming shutdown/drain, scene
teardown, asset/GPU-asset handoff teardown, streaming state teardown, frame graph
teardown, render-extraction plus renderer shutdown, device shutdown, window
destruction, scheduler shutdown, and initialized-state clear. The Dear ImGui
adapter is detached before this contract while the window and overlay system are
still live.

## Scene Replacement Lifecycle

Scene load/new/close operations are runtime-owned lifecycle transitions.
`Engine::GetSceneDocument().LoadSceneFromPath(...)` deserializes into a
temporary `ECS::Scene::Registry`; only a successful parse reaches the live
scene. Before replacement, `Extrinsic.Runtime.SceneDocument` drains
scene-local sidecars through
`RenderExtractionCache::ClearSceneState(...)`, clears selected/hovered/pending
pick state through `SelectionController::ClearSceneState(...)`, disconnects the
stable-entity component-event hooks, and resets the refined-primitive cache.
Load then swaps in the parsed registry, reconnects the hooks, and rebuilds
`StableEntityLookup` once at the replacement boundary; new/close clear the live
registry and lookup before reconnecting hooks on the empty registry.

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
