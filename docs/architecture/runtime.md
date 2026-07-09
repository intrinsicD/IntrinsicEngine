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
[`runtime config control`](runtime-config-control.md) facade on `Engine`.
That facade previews render recipes and engine config documents without ImGui,
activates recipes through the same renderer override path used by startup and
the Sandbox Editor, and hot-applies only the current
`render.default_recipe_config_path` and `sandbox.progressive_poisson`
engine-config subset. Other engine-config differences remain boot-only and are
reported without mutating the live engine.

`Engine::RunFrame()` is the promoted runtime lifecycle pipeline. Runtime owns the
cross-layer composition, while reusable phase contracts live in
`Extrinsic.Core.FrameLoop` so `core` stays dependency-free.

The frame order is:

1. poll platform events and handle minimized/resize skip paths;
2. drain the kernel command bus (`Engine::Commands()`) — the single
   pre-simulation mutation window per
   [ADR-0024](../adr/0024-kernel-module-architecture.md) D5; commands enqueue
   thread-safely from any phase and execute here in enqueue order, fail-closed
   when no handler is registered (ARCH-007);
3. pump the kernel event bus (`Engine::Events()`) at pump A — post-command
   drain and pre-simulation per
   [ADR-0024](../adr/0024-kernel-module-architecture.md) D7; command effects
   published as events become visible before fixed-step simulation, while
   listener-published events wait for the next pump (ARCH-008);
4. fixed-step simulation and CPU `FrameGraph` execution;
5. drain the kernel job-service completion gate — workers deposit results into
   `Engine::Jobs()` only, and the service publishes survivor completion events
   after token/world cancellation checks, main-thread, before pump B per
   ADR-0024 D8 (ARCH-009);
6. pump the kernel event bus at pump B — post-simulation and pre-UI/extraction
   per ADR-0024 D7, so simulation/completion events reach listeners before
   the frame builds UI and render products;
7. ImGui begin-frame, variable application tick, and ImGui end-frame;
8. build `Graphics::RenderFrameInput`, update the active camera controller,
   dispatch registered input actions, and drain one coalesced selection pick;
9. execute the render-frame contract: begin frame, runtime render extraction,
   renderer world extraction, prepare, execute, and end frame;
10. present the completed frame;
11. execute maintenance: transfer retirement, streaming drain/apply/submit/pump,
   job-service reaping, asset-service tick, GPU asset cache tick, material
   texture re-resolution, and render-extraction deferred-retire ticks;
12. drain completed pick readbacks through the incrementally maintained
   stable-entity lookup, release the consumed `RenderWorldPool` slot, and
   finalize the frame clock.

The internal `RuntimeFrameContext` record carries the data that must survive
between those phases: frame delta, fixed-step interpolation alpha, render frame
index, render input, extraction stats, and the acquired render-world pool slot.
It is intentionally not exported as public runtime API.

Runtime has two CPU scheduling tiers. `Core::FrameGraph` is the in-frame DAG:
systems add passes during a fixed-step tick, and the engine compiles, executes,
and resets that graph before the next tick. `Runtime.JobService` is the
multi-frame background lane for snapshot-in/result-out work: jobs receive only
captured value snapshots plus a cooperative `JobCancellation` view, never live
world or ECS references. Worker functions run on the shared `Core::Tasks`
pool and place their result back into the service; they do not publish
completion events. The frame thread drains the service before event pump B,
drops cancelled or world-scoped-invalid results whole, and publishes only
surviving completion events onto `Runtime.KernelEvents`. The `GpuQueue` target
is reserved for the later GPU-job participant/readback integration owned by
`RUNTIME-137`.

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
`Engine::LoadSceneFromPath(...)` deserializes into a temporary
`ECS::Scene::Registry`; only a successful parse reaches the live scene. Before
replacement, runtime drains scene-local sidecars through
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
