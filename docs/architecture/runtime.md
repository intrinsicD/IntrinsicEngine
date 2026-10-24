# Runtime Architecture

Point-neighbor consumers share the runtime-owned [spatial index cache](spatial-indices.md). Vulkan k-means submits through the same cache's GPU compute path and retains its idle workspace across runs. When the final centroid update moves a center, a final assignment and diagnostic readback refresh labels, inertia and the farthest-point index before terminal presentation, without adding a Lloyd iteration.

`runtime` is the composition root for IntrinsicEngine.

## Responsibilities

- Editor render-hint transactions share component equality, including bitwise
  scalar-source comparison. Visualization snapshots additionally own stored
  surface visualization config; scene snapshots retain their narrower scope.

- Private geometry plan builders share explicit normal/color preparation
  across graph and point-cloud uploads and explicit color resolution for mesh
  uploads. Topology, mesh automatic color discovery and corner expansion
  remain with their respective builders.

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

`Engine::RunFrame()` is the promoted runtime lifecycle pipeline. Runtime owns
the cross-layer composition and writes its fixed platform, operational,
render, maintenance, and shutdown order directly; Core exports no
runtime-specific hook family. Per-frame data and fixed-step helpers remain
implementation-local textual glue in
`Runtime.Engine.FrameLoop.Internal.hpp`, included only by
`Runtime.Engine.cpp`; they are not a module surface or an independently owned
subsystem. Optional asset maintenance uses the already-published concrete
`AssetWorkflowModule::RunFrameMaintenance()` operation between transfer
collection and geometry-residency retirement.

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

`EngineSetup` construction and non-template hook registration compile in
`Kernel/Runtime.Module.cpp`. The interface retains its records, small accessors
and typed command/event templates, so registration-body edits do not change the
module interface. The class and its out-of-line definitions share C++ linkage.

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
built-in capabilities with production lookup consumers: `CommandBus`,
`RenderExtractionCache`, `RHI::IDevice`, `Platform::IWindow`,
`Graphics::IRenderer`, and `RuntimeInputActionRegistry`. The editor session uses
`CommandBus` for deferred point-input validation. `AsyncWorkModule` publishes
the kernel-owned `JobService` for configured asynchronous consumers. Registry statistics
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
- `Extrinsic.Runtime.CommandBus`
- `Extrinsic.Runtime.FramePacingDiagnostics`
- `Extrinsic.Runtime.JobService`
- `Extrinsic.Runtime.KernelEvents`
- `Extrinsic.Runtime.ModuleLifecycle`
- `Extrinsic.Runtime.RenderRecipeActivation`
- `Extrinsic.Runtime.ServiceRegistry`
- `Extrinsic.Runtime.WorldHandle`
- `Extrinsic.Runtime.WorldRegistry`

`RUNTIME-238` replaced the last two implementation imports without changing the
count. `Extrinsic.Runtime.ModuleLifecycle` supplies `IRuntimeModule` for
`AddModule`/`EmplaceModule`, and `Extrinsic.Runtime.RenderRecipeActivation`
supplies the activation kernel and state the private recipe helper names, so
`Extrinsic.Runtime.Module`'s frame-hook composition closure is no longer part of
Engine's interface. `Graphics::IRenderer`, `FramePhase` and
`EditorInputCaptureSnapshot` are named through `extern "C++"` declarations that
match the definitions in `Extrinsic.Graphics.Renderer` and
`Extrinsic.Runtime.Module`; the definitions stay with their owners and Engine
adds no forward header.

The exact allowed getter/type/owner set is:

| Getter | Return type | Owning import |
| --- | --- | --- |
| `GetDevice` | `RHI::IDevice&` | `Extrinsic.RHI.Device` |
| `GetEngineConfig` | `const Core::Config::EngineConfig&` | `Extrinsic.Core.Config.Engine` |
| `GetLastFramePacingDiagnostics` | `const RuntimeFramePacingDiagnostics&` | `Extrinsic.Runtime.FramePacingDiagnostics` |
| `GetRenderer` | `Graphics::IRenderer&` | `Extrinsic.Graphics.Renderer` (borrowed) |
| `GetWindow` | `Platform::IWindow&` | `Extrinsic.Platform.Window` |

`tools/repo/check_kernel_convergence.py` compares the exact import and
re-export sets, getter names, return/owning types, and owning imports. A
same-count substitution, an unused/new import, a stale policy entry, or a
getter type change therefore fails instead of fitting under a numerical cap.
`GetRenderer`'s owning module is recorded in the policy's `borrowed_imports`:
it is a declared owner Engine must not import, and importing it again fails.

## Kernel type identity and borrowed command context

`Extrinsic.Core.Hash` owns the kernel's compile-time type identity.
`Core::TypeToken<T>()` and its `Core::Detail::TypeSig<T>()` signature source live
there beside `Hash::HashString64`, the one constexpr 64-bit FNV-1a byte hash
(an optional seed continues a previous hash; the RHI sampler cache and the
procedural geometry params hash reuse it).
`Extrinsic.Core.Dag.TaskGraph` reuses that hash for its own
`Detail::TypeTokenValue<T>()`; the two signature sources stay distinct, so the
graphs keep their existing token values and nothing merges them. The 32-bit
`HashString`/`StringID` naming lane is unchanged. Tokens are compiler-specific
values with the high bit masked off, not a frozen cross-compiler ABI.
`Core::TypeName<T>()` exposes the same signature text for diagnostics only;
the command bus, kernel events, job results and service registry log it, and
nothing may parse it.

The four kernel interfaces that need only erased identity —
`Runtime.CommandBus`, `Runtime.KernelEvents`, `Runtime.ServiceRegistry` and
`Runtime.JobService` — therefore import `Extrinsic.Core.Hash` and not
`Extrinsic.Core.FrameGraph`. Callers that actually build an ECS execution graph
import the frame graph themselves.

Command contexts borrow `ECS::Scene::Registry` and `WorldRegistry` through
references and pointers. `Runtime.CommandBus` uses matching, non-exported
`extern "C++"` declarations for these globally attached types; neither its
interface nor its drain implementation needs the registry definitions.
Handlers that access scene storage or world lifecycle operations import the
corresponding owner directly. `KernelCompilationLocality.Commands` guards both
registry boundaries using the compiler's dependency graph.

Setup/frame-hook contexts in `Runtime.Module` use the same registry declarations;
`Runtime.InputActions` likewise borrows the scene without importing its storage.
Their compiled implementations only pass the references to callbacks. The
callbacks import complete owners when they invoke scene or world operations.
`KernelCompilationLocality.ContextBorrows` guards these interface dependencies.
Input-action dispatch names `Core::Extent2D` directly; the window interface's
alias adds no type information. `KernelCompilationLocality.InputActions` keeps
`Platform.Window` out of the input-action interface's compiler dependencies.

`JobService` names `RHI::ICommandContext` only by reference, for GPU queue
participants. It declares the class with a non-exported `extern "C++"`
declaration that matches the sole definition in `Extrinsic.RHI.CommandContext`;
that definition and all four out-of-line member definitions carry the same
language linkage, so the vtable key function stays globally attached. CPU job
declarations consequently stay out of the RHI handle/descriptor closure, while
every participant that records commands, and every implementation unit that
calls a member, imports the owner. The borrow is valid only while the surface
stays a reference: a command member or a stored command value belongs in an
implementation unit, not in this interface.

`KernelCompilationLocality.Commands`, `.Events`, `.Services` and `.Jobs` check
these boundaries against the configured compiler module graph.

The clustering and consolidation lifecycle interfaces similarly borrow
`RHI::IDevice` and `WorldRegistry` without importing their definitions;
consolidation also borrows `SpatialIndexCache`. Their implementations import the
complete owners and algorithms. `EditorCompilationLocality.ClusteringLifecycle`
and `.ConsolidationLifecycle` check these boundaries. Public records remain in
`Runtime.ClusteringTypes` and `Runtime.PointCloudConsolidationTypes`.

## Texture-bake interface boundary

`runtime.texture-bake-interface-locality` keeps bake records and operations
separate from private composition dependencies. `TextureBakeService::Impl` in
`Runtime.TextureBakeModule.cpp` owns binding, target changes, GPU participant
registration and teardown. The existing friend `TextureBakeModule` accesses
that always-constructed implementation directly. Asset services, scene registry,
RHI device, renderer, GPU asset cache, render extraction and command history
stay out of the public bake interface's compiler dependency closure.
Property references and value kinds come from `Runtime.GeometryProperty.Types`
and `Geometry.Properties.Types`; live property storage, geometry availability,
geometry source components and presentation stay in the implementation.

The existing implementation also owns target detachment and scene-asset cleanup;
module calls use those methods directly, preserving their `noexcept` boundary.
Unregistration and device-idle ordering precede unbinding. Public request/result
records and service behavior keep their existing ownership and value semantics.
The four `EditorCompilationLocality.*` tests enforce the boundary for baking,
visualization operations, workspace snapshots and private workspace attachment.

## Render diagnostics and recipe-override boundary

The runtime data contracts that name renderer-produced records import narrow
graphics owners rather than the renderer:

- `Extrinsic.Runtime.FramePacingDiagnostics` contains copied timings/counters
  and has no module imports. The engine copies renderer compile/execute times
  at its sample-publication boundary; EditorUiModule supplies ImGui timings.
  `Extrinsic.Graphics.RenderDiagnostics` remains the canonical owner of
  `RenderGraphFrameStats` and its complete diagnostic records.
- `Extrinsic.Runtime.RenderRecipeActivation` imports
  `Extrinsic.Graphics.RenderRecipeConfig`, which owns `FrameRecipeOverride` and
  its diagnostics alongside the config schema. Activation installs an override
  through the kernel's `SetFrameRecipeOverride` callback, so it needs no
  renderer reference.
- `Extrinsic.Runtime.RenderRecipeEditingOperations` imports the diagnostics
  owner for editor inspection of `RenderGraphFrameStats`.

`Extrinsic.Runtime.Module` re-exports the activation kernel and imports the
pacing record, so every runtime module author inherits these narrow contracts
instead of the renderer's execution closure. `Extrinsic.Graphics.Renderer`
re-exports both owners plus `Extrinsic.Graphics.FrameRecipe` because
`IRenderer` names their types; the records have one definition each and no
alias or wrapper. `Extrinsic.Graphics.FrameRecipe` owns
`ProjectFrameRecipeOverride(...)`, which projects an override onto the derived
`FrameRecipeFeatures`. Callers that execute rendering or reach renderer-owned
subsystems import `Extrinsic.Graphics.Renderer` themselves — `Engine.cpp` and
the rendering-facing tests do, while `Runtime.Engine.cppm` only returns the
borrowed reference.

CTest `RenderCompilationLocality.RuntimeDiagnostics` reads the configured
Clang/CMake module graph and fails if any of those runtime contracts, or the
diagnostics owner itself, reaches `Graphics.Renderer`, the subsystem registry,
the prep pipeline, a concrete rendering system or an upload helper.
`RenderCompilationLocality.FramePacing` additionally keeps rendering diagnostics
and device interfaces out of the pacing record's compiler dependency closure.
`RenderCompilationLocality.EngineInterface` applies the same closure check to
`Runtime.Engine.cppm`, additionally forbidding `Extrinsic.Runtime.Module`.

The shared `EditorFeatureTestContext.hpp` also imports the diagnostics owner.
`EditorCompilationLocality.TestContext` keeps its compiled helper and the
method/model/presentation test cohort free of `Graphics.Renderer`; tests that
call renderer methods retain their explicit facade import.

The workspace-model implementation unit
`Runtime.EditorWorkspaceSnapshots.Models.cpp` imports
`Extrinsic.Graphics.RenderDiagnostics` for frame/GPU/command diagnostic records
and `Extrinsic.Graphics.RenderCommandRouter` for `RenderCommandPassStatus`.
The latter is not re-exported by the diagnostics owner. The same unit imports
`Extrinsic.Runtime.VisualizationRecipes` directly for the visualization recipe
records it names. This implementation's dependency boundary is checked per
change with the compiler-boundary tool; the `RuntimeDiagnostics`
CTest above covers the public contracts.

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

`JobService` dispatches ready work through the Core scheduler's five worker-only
priority lanes, from `Critical` through `Background`. Equal-priority jobs follow
scheduler enqueue order; running work is not preempted, and starvation freedom
is not promised. Main-thread frame waits cannot execute these jobs inline.
An explicit cancellation counts once in `CancelledJobs`; only cancellation
caused by an unsuccessful dependency also increments `DependencyCancelledJobs`.

`JobService::Stats().InFlightJobs` counts all retained nonterminal jobs, including
dependency waits and parked apply gates. Invalid and terminal records are
excluded; pending unpublished finalizers have their own counter. Aggregate
accounting reuses the same terminal classification as completion and reaping.
Waiting gauges reflect the latest drain/dependency pass; off-thread readers
must not assume all fields form one atomic snapshot.

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

Queued saves capture the document revision and all serializable scene components,
including vertex-channel bindings, before dispatch. The completion marks that
captured revision saved, so later edits remain dirty. Save jobs form one dependency
chain in submission order; an interrupted chain cancels its queued followers,
and a subsequent fresh save can start after the chain terminates. World/document
replacement invalidates publication through the binding epoch but retains any
unfinished save dependency until its completion/finalizer: cancellation alone
does not imply that a worker has stopped writing.
Synchronous `SaveSceneToPath` returns `InvalidState` while any queued save remains
unfinished, including one targeting a different path. It neither waits nor writes
or changes document/history state on this rejection; synchronous saving is available
again after the queued tail's completion/finalizer settles.

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
properties plus the optional current normal property. The canonical normal workflow
also publishes on edge, halfedge and face domains through one output-only history
transaction, watching only consumed property revisions and retaining unrelated edits.
Sampling, scalar-field, normal and position publishers validate captured input
and output revisions before their initial publication. Related history commands
share an output revision guard and check exact output presence, type and stored
value bits on replay. Successful owned writes advance that shared guard before
notifications; non-output inputs retain their original captured revisions.
Foreign writes or storage replacement reject replay without moving the cursor,
even when the stored bits are unchanged. A new command after a foreign edit gets
a separate guard and cannot legitimize older commands. Other existing property
history authors preserve mixed-command replay by advancing only guards whose
full watch matched immediately before their successful writes. Whole-property-set
replacements apply that rule to tracked keys in the replaced domains, including
absent outputs; they do not weaken the non-output input guards. These authors
retain their own replay guards: propagation preserves previously accepted undo
steps, but does not enable replay sequences they already rejected. In particular,
CPD, bilateral and outlier-analysis records retain their per-record revisions
after an older command is replayed.
See [normal estimation](normal-estimation.md) for config, topology and spatial-cache ownership. Clustering likewise
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
Consolidation's `PrepareAvailability` keeps position/normal finite verdicts in
its existing lifecycle owner. Preparation reads metadata and queues a missing
check through the command bus; the main-thread drain revalidates world epoch,
entity, domain, property revision and count before scanning. Two bounded slots
retain the currently requested position and normal verdicts independently.
Pending checks disable the shared action button and supply its tooltip reason.
Metadata and configuration are checked before any finite lookup, so invalid
requests do not queue scans. Submission still scans through the same preflight
without trusting cached verdicts. The cache serves one active preview; changing
the requested source supersedes pending checks. Retained mutable property borrows
must follow the canonical `MarkModified()` contract. The editor service
operations import the service Types owner, without the consolidation lifecycle.

LOP-family consolidation uses the broader property-domain form of the same
contract. Its request carries named finite `vec3` input/output properties on
any resolved mesh, graph, or point-cloud element domain, so a mesh face-center
property is a valid point set without becoming a vertex property or a new
entity. Same-cardinality publication changes only the named output properties;
exact topology and unrelated/custom property storage remain owned by the
source. Mesh/graph count changes fail during the shared availability preflight
before a job is queued. Only a topology-free point-cloud point domain may take
the existing canonical full-source replacement path, with exact undo/redo.
LOP can also lease the selected property index from `SpatialIndexCache` via
`cpu_lbvh`; moving-sample indices remain private to its worker. The existing
config and UI choose that backend, and completion reports index reuse without
silent fallback. Other strategies retain their existing backend eligibility.
The `vulkan_lbvh` choice covers LOP, isotropic/anisotropic WLOP, CLOP and EAR.
It uses framed complete radius queries against the leased source and a private
moving workspace per iteration, then reuses CPU projection arithmetic. WLOP/EAR
source density is computed once; self-inclusive moving rows feed density and
normal refinement before repulsion excludes self. CLOP's mixture fitting and
continuous attraction, initial normal estimation and EAR's progressive insertion
remain CPU work. Config exposes query batch/capacity limits; GPU overflow, stale
source and cancellation reject publication. Completion identifies
`vulkan_lbvh_cpu_projection`, reports workspace/query counts and names the CPU
stages. The existing grid backend and LOP-only `cpu_lbvh` eligibility are unchanged.
Geometry-presentation slot edits additionally validate and monotonically
advance the presentation recipe generation on apply, undo, and redo instead of
restoring a captured generation and admitting an ABA stale-output match. The
mesh-denoise publisher validates both geometry metadata and the exact live
`v:position` snapshot before each transition. Mesh-curvature publication also
validates that source plus the exact mean, Gaussian, minimum-principal,
maximum-principal, and principal-direction property snapshots it owns. The four
scalar fields publish together in one transaction because
`Geometry::Curvature` derives mean and Gaussian curvature from the two
principal values; a partial publication would expose a stale principal field
beside a freshly derived mean one. Remesh, subdivide, and simplify publication
validate geometry metadata plus the complete canonical position/connectivity
state, including before queued output may publish. UV regeneration uses the
same transaction mechanics with its owner-specific semantic source snapshot:
exact live positions, edge/halfedge/face connectivity, canonical corner UVs,
and known vertex/face property values are revalidated before queued publication
and every history transition. It writes UVs and atlas region/chart labels into
the original source slots, preserving topology and unrelated properties. Its
apply stamp requests the required full GPU rebuild after publication; the other mesh owners stamp their
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
post-import enrichment remain one automatic import lifecycle. Queued
direct-mesh import prepares topology, normals, bounds, and owned ECS component
buffers in the decode worker; main-thread publication moves the prepared buffers.
Authored vertex/corner UVs are available at first publication. Only meshes without
texture coordinates queue a separate background atlas job. That job reuses the
prepared topology and publishes only UV attributes, retaining existing position,
normal, topology, and unrelated property buffers. Existing UVs bypass atlas work;
generated-normal texture baking remains a separate follow-up.

Deferred direct-mesh enrichment captures the published mesh-source generation:
active domain and topology markers, all vertex/edge/halfedge/face property
metadata and mutation revisions, deleted counts, and vertex-channel binding generation and
property references. Its world-scoped job reaches the main-thread apply only
while the asset-workflow binding epoch still names the same active world and
scene, the raw entity is still live, its entity-sidecar token still names that
job, and its geometry and UV inputs remain current. An unchanged revision
signature takes the fast path. Changed revisions trigger a geometry comparison
against the prepared mesh; component relocation and unrelated attribute edits
therefore retain a valid UV result. Apply and unpublished
finalization resolve the scene through `WorldRegistry` at callback time; they
never retain a scene reference across worker execution. A world switch,
document replacement, destroyed world, recycled entity, or generation mismatch
therefore terminates without targeting retired storage. A stale discard
performs no ECS, history, or selection write. The same sidecar projects
pending/terminal status plus a nonempty reason into the selected-entity
processing model. Pending enrichment does not gate geometry processing:
each action retains its canonical property/topology and kernel-readiness
checks, so a geometry-only mesh can run curvature before UV/texture work
finishes. An intervening geometry, UV, or channel-binding edit invalidates the pending
result. Changes to unrelated attributes, including curvature results, survive
UV publication because only the UV property is applied.
UV-dependent operations still require usable UVs, and enrichment progress and
terminal diagnostics remain visible independently of action readiness.

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

Editor UI contribution is data-driven through
`Extrinsic.Runtime.EditorWindowRegistry`: contributors provide stable ids,
structured menu paths, open state, and draw callbacks, and closed or globally
hidden windows receive no callback. The app-composed
`Extrinsic.Runtime.EditorUiModule` owns the ImGui adapter, graphics overlay,
paired frame hooks, and the global `G` visibility toggle, which it applies in
`UiBegin` independent of ImGui keyboard capture so that a hide reaches the
pre-render transform flush. It requires only the exact built-in
`Platform::IWindow` and `Graphics::IRenderer` services, then publishes an
Engine-free `Extrinsic.Runtime.EditorUiHost`. The host owns the registry,
parameterless frame contributions and the per-frame viewport input request
(OR of contributions, reset by the owner each `UiBegin`, dropped on hide or
non-operation); it passes neither `Engine&` nor application state to
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
`ImGuiAdapterDiagnostics` exposes overlay-transport byte
counters alongside the existing draw-list counts: `LastFrameFontAtlasCopyBytes`
is non-zero only when the atlas bytes are recopied into an overlay frame, while
`LastFrameVertexCopyBytes`, `LastFrameIndexCopyBytes`,
`LastFrameCommandCopyBytes`, and `LastFrameOverlayCopyBytes` report the POD
vertex/index/command payload copied for the current submitted frame. These
counters are runtime-side observability only; graphics still owns retained atlas
resources and upload buffers. The same diagnostics include
microsecond timings for `BeginFrame`, the editor callback, `ImGui::Render()`,
draw-data/font/list copying, and total `EndFrame()` so runtime frame-pacing
samples can separate editor CPU work from ImGui producer-copy cost.

`EditorUiModule` copies the adapter's completed capture into that value only
after `EndFrame`, then ORs a host viewport claim into it as mouse and keyboard
capture and publishes the merged value as `CapturesViewportInput`; typed Camera
and SceneInteraction hooks, input actions, and later hooks consume the same
snapshot rather than reading ImGui capture flags independently. A claim is not
a cancel: the claiming frontend's session survives a claimed frame. Native focus loss reaches it as a
kernel event (`Platform::WindowFocusEvent`, republished by Engine) and cancels
a running session at delivery; its `Idle` hook keeps minimized frames pumping
such events (see [ADR 0006](../adr/0006-camera-picking-and-gizmo-runtime-handoff.md)).
The Sandbox's ImGuizmo frontend reads `SceneInteractionModule::PrepareGizmo` in
`UiBuild`: the Main controller's unjittered view/projection before this frame's
camera update (unchanged on a claimed frame, so it equals the rendered camera)
and the scene rectangle resolved in framebuffer pixels and mapped back to
logical window coordinates. Its previews, and every cancel, precede the single
pre-render transform flush, so the first preview frame already renders the
moved entities. Operational on Vulkan only for the executed path and a pixel
ratio of 1 (claim C118; `RuntimeSandboxAcceptanceGpuSmoke.ImGuizmo*`, native
focus `GlfwPlatformSmoke.NativeFocusChangesEmitWindowFocusEvents`); other
ratios rest on CPU evidence until
[UI-079](../../tasks/active/UI-079-imguizmo-hidpi-operational-run.md).
Omitting the module leaves the value unclaimed and all ImGui pacing counters
zero. Its ImGui context owns a paired ImPlot context.

The interaction-to-render boundary is one
`RuntimeSceneInteractionRenderSnapshot`: a world handle plus owned vectors for
selected render ids, gizmo packets (none from the Sandbox editor, whose
ImGuizmo frontend draws in the UI) and copied hover identity. Submission
copies caller storage into reusable extraction-owned storage. Extraction
accepts it only for the current world and otherwise supplies empty interaction
data. No controller, module pointer, pick/refinement context, or ECS handle is
retained by graphics.

`RenderExtractionCache` keeps its data in one private state owner. Public
lookups and updates access that state in their existing implementation units;
shared extraction, residency and shutdown algorithms remain private state
methods. Visualization recipes and their encoding batch are a value member
of that state. The public interface borrows `IRenderer` through its globally
attached declaration. Its private declaration partition likewise needs only
renderer and EnTT declarations. The main and geometry extraction units import
the renderer and full registry where they execute those operations. Recipe
encoding imports its packet, geometry-availability and recipe owners directly;
it needs neither the renderer interface nor full EnTT/GLM headers. The partition
retains the material-system dependency
through its owned material lease. `RenderCompilationLocality.Extraction` checks
the public interface's compiler boundary.

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
detail BMI declares attachment bindings, workspace-session state, shared
import/property/vertex-channel preflight rules, geometry metadata signatures,
stable-entity lookup, and transform/history adapters. Actions and copied
workspace models call the shared implementations alongside the context
adapters, including validation timing and scratch-allocation accounting.
Direct actions and queued completions also share import/scene status wording;
workspace and UV-job views use one job-record projection.
Transform edits and registration use the same undo validation and dirty stamp;
feature-specific render-hint transactions retain their own state contracts.
Its session
implementation is limited to attachment epochs, job
identity/result retention, and prepared-frame lifecycle. Presentation-free
workspace model construction is a separate workspace implementation unit, and
neutral service-to-feature-context projection remains an internal runtime
composition adapter.

`Extrinsic.Sandbox.Editor.Shell` owns hierarchy, inspector, selection,
file/import, frame-graph, render-recipe/artifact, camera, and visualization
presentation. All core windows are closed by default and use the shared
registry. The same module provides shared app-local diagnostic/domain widgets,
result dismissal, texture-bake options and borrowed draw state, and visualization
command builders for the panel modules. Uniform-color commands disable baked
textures while retaining scalar styling. Mesh Appearance forwards the workspace's
callback-scoped borrowed
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
shared [normal-estimation](normal-estimation.md), [outlier-analysis](outlier-analysis.md), [kernel-density](kernel-density.md) and [point-spacing](point-spacing.md) windows, with aliases in mesh/graph/point-cloud menus. Runtime retains their exported
models, command validation/execution, undo/history integration, derived-job
submission, stale-result rejection, and result sinks; the application owns the
stable registrations, menu paths, lazy per-frame domain-model cache, widget
state, and result presentation.
`Extrinsic.Sandbox.Editor.DomainPanels` owns seven windows: one
Appearance window (`scene.appearance`, `View` menu; Vertices, Edges and Faces sections chosen by the
element-domain reading predicate, `UI-075`) and Properties and Selection for PointCloud, Graph, and Mesh. It preserves their stable ids, menu
paths, titles, closed defaults, controls, per-frame lazy model cache, and
immediate/asynchronous result publication. Runtime retains the exported domain
models, callback-scoped borrowed property view, command/job execution,
UV/outlier result state, and result sinks; the app module imports runtime only.
Appearance keeps supported Surface/Edges/Points visibility checkboxes reachable
when their render components are absent. Editor availability checks the source
capabilities for the requested layer; render extraction still requires an enabled
layer. Each visible layer has collapsed Settings for domain/type, width/size and
color/property selection, with optional color mapping, texture baking and advanced
bindings. Mesh property distributions are collapsed separately. Material/default
selects an explicit material source for that layer rather than inheriting an
entity-level visualization override. Edge property choices follow the selected
vertex/edge source domain.
Analysis display buttons for scalar, color, and integer-label properties apply
the same undoable lane configuration as Appearance; they do not install a
competing transient recipe. UInt32 labels use the same deterministic palette for
buffer and texture-baked colors. Explicit GPU-buffer recipes and vector overlays
remain separate, but do not suppress uploads for unrelated Appearance properties
or presentation slots. Mesh surface buffers use the GPU vertex/triangle layout;
point and edge lanes use separate canonical-layout keys, even when they select
the same property. Clear vis removes entity and lane overrides and the transient
recipe.
Pending scalar fronts bind through the same extraction path for Appearance,
Show recipes, and presentation slots. A front can supply its display range so
an earlier CPU property does not determine the preview range. Scalar analysis
previews use device min/max; accepted CPU values retain the existing robust
auto-range policy, so the display range can change on Accept. Split mesh
surfaces upload a canonical-element index map and sample the resident scalar
buffer through it; canonical point/edge lanes sample that buffer directly.
Accept or Discard restores extraction from the current CPU property.
Constant finite scalar ranges use one graphics-owned expansion rule for CPU
recipes, GPU fronts, component fallbacks and texture baking. Each caller retains
its preferred half-width; representable neighboring values prevent rounded-flat
endpoints, and overflow clamps only the outward endpoint to the original value.
Thus even `±FLT_MAX` has a finite nonempty display range. Nonfinite input remains
invalid and is not converted into a valid range.
After a successful FPFH histogram display request, moving the display-bin slider
selects the corresponding output property without recomputing descriptors.
Changing the analysis configuration resets this live display selection.
Progressive Poisson operation bodies and clustering config-control helpers compile
in the private `Runtime.PointSetOperations.Processing.cpp` implementation unit. K-Means
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
Recipe validation, stage-payload matching and trace appends compile once in
`Runtime.AssetWorkflowModule.cpp`; the interface exposes their declarations
alongside the copied recipe and result records.
The import executor is private to `AssetWorkflowModule`: its declaration header
is shared by the module's two implementation units, without a separate executor
BMI. It stores the existing dependency record directly. Borrowed provider pointers
remain guarded by the initialized value, binding epochs and submission identity;
the workflow keeps the executor's address stable across reinitialization.
Model materialization exposes copied entity/asset records and diagnostics. Its
implementation loads textures and seeds presentation recipes; extraction alone
owns effective material leases and texture resolution. Both its interface and
implementation are independent of renderer/material modules, enforced by
`AssetCompilationLocality.ModelMaterialization`. Model and texture imports reuse
the texture-residency owner's payload-mismatch and upload-deferral classification.
Queued geometry and model/texture imports share executor-local submission,
route/decode transitions and apply preflight using that captured identity.
Payload-specific decoding, service requirements and materialization stay in
their respective queue paths; unpublished jobs use the shared cancellation
finalizer.

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

### Property atlas and baked-texture inspection

The parameterization config's atlas section supplies the method, scalar guide,
objective, resolution, padding and solver budgets through the same validated
command used by the editor. Guide slots bind arbitrary compatible vertex/face
scalar properties. Atlas results publish canonical UVs and face-owned
`f:atlas_region` / `f:atlas_chart`; scene documents retain these labels and
corner UVs. The complete CPU method contract is documented in
[property-guided atlas](../methods/property_guided_atlas.md).

`TextureBakeService` resolves default UV authority as corner-over-vertex; explicit
bindings remain exact. Records retain resolved binding and content fingerprints
for UVs, topology, positions and source values. Snapshot queries evaluate current
freshness, while render extraction refuses stale or incomplete bindings. This
allows tabs to retain a diagnostic record after an atlas edit without sampling
old pixels against the new layout. Generated GPU pixels and the live bake catalog
remain runtime resources, not serialized texture payloads in scene documents.

The editor's scene/atlas split shares one authoritative scene rectangle with
camera projection, scene presentation and input mapping. The derived UV view
uses the complete corner-attribute split and has an atlas tab plus generated
texture tabs. See [ADR 0025](../adr/0025-parameterization-uv-view-and-split-view.md).

### GPU object-space normal bake lifecycle

`AssetWorkflowModule` prepares the source normals and presentation recipe for
eligible model and direct-mesh imports, then calls the shared
`TextureBakeService`. The caller reconciles a completed generated output into
the requested material slot while preserving unrelated channels. Pending,
failed, unknown-identity and stale records cannot bind as current output.
The producer owns source fingerprints, GPU work and generated assets; callers
do not duplicate its raster path or texture lifetime.

Automatic property bakes resolve their output width and height from the
materialized UV-atlas diagnostics. If an authored-UV path has no
atlas extent to report, it uses the atlas-scale 1024x1024 fallback rather than
the former 64x64 downsample. Generated normals use two gutter texels at this
matching resolution, equal to the default atlas chart margin. The Sandbox
manual bake controls expose width, height, and padding through the same editor
command; completion of a UV-regeneration job adopts its reported atlas extent
and requested padding once. Selected property bakes use explicit chart coverage
and chart-aware gutters for both raw-float and encoded output.
Automatic surface-appearance bakes use the exact generated-atlas extent stored
on the entity, or 1024 when authored UVs have no known extent. They never enlarge
the texture implicitly. The extent follows UV undo/redo and scene serialization;
UV edits invalidate it. An extent above the shared 8192 bake cap fails before
allocation. Records report the requested extent. Rejected requests wait for a source/atlas revision or matching
appearance-config change before retrying; unknown source identity never binds.
A source snapshot larger than the 64 MiB budget is a permanent rejection for
that revision. Failed metadata assets require removal before a fresh bake;
queue pressure remains a transient condition.
The atlas generator allows up to 16384, so the editor reports any native atlas
extent above the bake cap instead of implying a matching-resolution bake.

After canonical preflight, the producer creates value and coverage metadata
assets and publishes a pending catalog record keyed by entity and output name.
Successful rebakes preserve asset identity and advance the output generation.
CPU metadata does not stand in for GPU readiness: both exact cache generations
and the current source identity must match before output publication.

The texture-bake producer snapshots the resolved source properties, triangle
indices and authoritative corner-or-vertex UVs into its own GPU buffers. It
revalidates their source identities before recording, independently of the
renderer’s geometry residency. The existing `JobService` GPU-queue participant
records into the renderer’s open frame; it never acquires, submits or presents a
second frame. Value and coverage textures have separate cache generations and
become ready together after `issueFrame + framesInFlight`. A superseded ticket
cannot publish into either replacement generation.

Up to four bakes record per frame. Explicit source-byte and active-work limits
bound pending work. Each bake owns its raster buffers and depth attachment;
chart coverage and nearest-edge gutters do not share dilation scratch. Value
and coverage outputs finish in `ShaderReadOnly`, with transfer-source usage for
acceptance readback and one mip level. Ordinary component-wise averaging is not
a valid normal-vector reduction, and no vector-aware mip generator is composed.

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
participant reports retained bake pipeline, cache, and provenance state
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

`Extrinsic.Runtime.WorldRegistry` owns the sole `WorldRegistry` definition and
its out-of-line members with `extern "C++"` language linkage, the same pattern
`Graphics::IRenderer` and the kernel `ModuleLifecycle` records use. A module
interface that only borrows a `WorldRegistry` reference or pointer — such as
`Extrinsic.Runtime.SpatialIndexCache`, `Extrinsic.Runtime.CommandBus` and
`Extrinsic.Runtime.Module` — names
it through a matching non-exported forward declaration instead of importing the
owner and its `ECS::Scene::Registry`/`JobService` closure. The declaration and
the definition are the same type in either import order; there is no forward
header, alias or second definition, and every caller that invokes a
`WorldRegistry` method imports the owner directly.

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

Reaping also retains a terminal predecessor while an already-pending job names
it as a dependency. Cancellation propagates one dependency layer per drain;
removing that outcome earlier would let a downstream job mistake a failed,
missing predecessor for a satisfied historical token. Once consumers leave the
pending-dependency queue, the normal reaping pass releases the records. This
does not add a permanent outcome ledger for jobs submitted after reaping.

Import materialization may synchronously complete only the specific AssetService
CPU load/reload it issued and flush that asset's event. Import apply reachable
from `Engine::RunFrame()` must not use `Scheduler::WaitForAll()` as a frame-path
fence; global waits are reserved for shutdown and explicit non-frame joins.
Interactive queue cancellation is available before `JobState::AwaitingGate`;
only shutdown also cancels decoded work waiting at that gate. Direct imports and
reimports use the same ingest records but reach their terminal row inside the
synchronous call.

Active-world asset-import and scene-document operations additionally capture
the submission `{WorldHandle, Scene::Registry*}` pair on the main thread.
Asset-import apply validates the pipeline binding as before.
`SceneDocumentModule` queued callbacks capture only weak shared module state
plus owned operation state and one copied binding record containing module
generation, binding epoch, world and registry identity. Shared helpers track
accepted save/load jobs and finalize unpublished results. The apply gate
compares `WorldRegistry::ActiveWorld()` and `WorldRegistry::Get(world)` before
publication; completion forgets its owned task before save history updates or
load replacement. The unpublished finalizer forgets the task before checking
the binding and records an error event only for a current document.
The epoch makes an away-and-back switch observable even when
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
while unrelated actions still dispatch. The per-controller `Focus(...)` framing
distance math remains owned by the controllers
(`Extrinsic.Runtime.CameraControllers`).

`Extrinsic.Runtime.CameraControllers` owns the named view presets (`CameraViewPreset`: front, back,
left, right, top, bottom, isometric). `CameraPresetAxesFor` gives each preset's
orthonormal forward/up pair, `MakeCameraPresetSeed` builds the controller seed
(camera `2 * radius` behind the focus center along the preset forward), and
`ApplyCameraPreset` seeds, focuses and marks the transition. They live there, not in the focus command, so `Runtime.SceneEditingOperations` can use them without importing the scene registry. View capture
(`ViewCapturePreset`) and the editor camera pose command
(`ApplyEditorCameraPoseCommand`, `Runtime.SceneEditingOperations`) both call it,
so a screenshot preset and the editor View preset frame identically. Camera pose,
preset and focus changes are view state and do not enter the undo history.

Operational promotion is gated on `RHI::IDevice::IsOperational()` and renderer
resource rebuild success. Vulkan-specific diagnostics are recorded by the Vulkan
backend/runtime breadcrumb path, but runtime frame control does not branch on
Vulkan diagnostics.

Runtime jobs use the shared [scheduler and wake contract](task-graphs.md#scheduler-preferences-and-wake-contract).
The [CPD editor integration](#coherent-point-drift-editor-integration) is the
current coroutine consumer.

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
unregisters its optional `F` action. Before module teardown, Engine's `Shutdown`
calls `JobService::CancelAndDrain`: it cancels every job, invokes each pending
coroutine wake callback outside the service lock, joins work through the shared
scheduler, runs unpublished main-thread finalizers exactly once and
releases all retained callback/result captures. Reentrant submissions during
this barrier are rejected. Thus job-owned GPU workspaces die while the device
and borrowed module services still exist; cancel flags alone are insufficient.
The barrier follows application teardown because application-owned work gates
may be released there; `BeginShutdown` remains the announcement/device-idle
boundary. The job service can be reused after the barrier. Ordinary
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
name-then-registration order. After validating the binding, it clears render
extraction when the kernel renderer and extraction services are present. This
also covers compositions without `AssetWorkflowModule`. It advances the binding epoch, invokes every
`BeforeReplace` callback while the outgoing registry is live, clears or moves
the replacement into that registry, invokes every `AfterReplace` callback
against the rebound registry, and only then resets the exact owned history.
The callbacks are synchronous `void` functions: no queued replacement event or
invented rollback protocol exists.

`SceneInteractionModule` retains its own strong participant handle. Before
replacement it cancels any drag while the registry is live and clears
selection/hover tags, pending and in-flight picks, readback contexts/refined
output, the gizmo session, stable lookup binding, and its copied render
snapshot. Gizmo drag release coalesces the selected transform batch into the
document-owned `EditorCommandHistory`; when that service is absent, transform
dragging is disabled rather than recorded in a second stack. After replacement
the module rebuilds lookup against the rebound registry and publishes empty
interaction data. The module owns one validated
`{WorldHandle, Registry*, interaction epoch}` binding; pick sequences remain
monotonic while old-world/old-epoch results fail closed.

`AssetWorkflowModule` retains its own strong participant handle. Before
replacement it empties import dependencies, clears normal-bake scene state,
and destroys the model-scene handoff while the outgoing
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

All of those canonical property domains share one CPU/GPU coherence boundary.
Mutable geometry-property access publishes a process-monotonic content
revision; render extraction remembers only the revisions and counts consumed by
each resident mesh, graph, point-cloud, or primitive-view lane. A changed
position or resolved attribute merges into the existing partial upload plan,
while count or consumed-topology changes request full reconciliation. Explicit
ECS dirty tags remain precise compatibility hints, but methods do not need a
renderer-specific dirty call. CPU methods finish by publishing canonical CPU
properties. Vulkan methods upload inputs before dispatch, keep iterations
GPU-local, and publish one terminal readback into those same CPU properties
before reporting an applied result. The complete ownership, staging, and
failure contract is documented in
[geometry property CPU/GPU coherence](property-coherence.md).

Scene JSON version 6 remains backend-neutral. Version 2 made graph halfedge
connectivity mandatory so a loaded graph satisfies the same
`Vertices + Halfedges + Edges` source contract as a freshly materialized one.
Version 3 retires the `PointColor`, `PointScalarField`, `LineColor` and
`LineScalarField` presentation slot semantics (RUNTIME-318): point and line
lanes are colored only by the visualization overlay (lane configs), so a
document naming one of them, and every version 2 document, fails with
`InvalidFormat` instead of being converted.
Version 4 (RUNTIME-319) adds a `properties` table to every element-domain
section (mesh `vertices`/`edges`/`halfedges`/`faces`, graph `nodes`/`edges`/
`halfedges`, point-cloud `vertices`), so processed and custom properties that
bindings, overlays and recipes name survive a reload. It holds every typed
property (bool, int32, uint32, uint64, float, double, vec2/3/4) that is neither
topology (`IsTopologyProperty`) nor a stream the section already writes
(positions, normals, texcoords, corner streams, atlas labels). Each entry is
`{name, kind, data}`, where `data` is strict base64 (`Extrinsic.Core.Base64`)
of the little-endian element bytes (bool: one 0/1 byte), so values are
bit-exact, including infinity and signed zero, at about 1.33x the raw size,
inline with no sidecar or compression. The element count is the section's own,
and the payload size must match it before decoding. The reader rejects the
whole document on a malformed, duplicate, reserved, wrongly sized,
unknown-kind or NaN-carrying entry, and an entry whose name is a canonical or
derived engine name (`v:position`, `v:normal`, `v:point`, `v:texcoord`,
`h:texcoord`, `h:normal`, `f:normal`, the atlas labels) under any other kind.
The writer never fails a save over a property: it skips one holding NaN, one
whose type has no value kind, and a dedicated stream held under another type,
and counts it in `SceneSerializationStats::UnpersistedGeometryProperties` with
a warning naming the reason (`GeometryProperties` counts the persisted ones).
Engine-derived mirrors (`v:point` on mesh, graph and point-cloud vertices,
`f:normal`) are not stored and not recreated on load: no runtime consumer reads
them, and a saved copy would go stale. Restored properties
are new storages with fresh content revisions, so renderer and residency
caches observe them as new content.
Version 5 (REVIEW-007 PK12) drops the legacy property-reference keys. A
reference is `{domain, name, valueKind?}`; an omitted `valueKind` loads as
`Unknown` (unconstrained). A reference carrying `propertyName` or
`expectedValueKind`, alone or beside the canonical keys, fails the whole
document with `InvalidFormat`.
Version 6 adds `siblingOrder` to every entity with `parentId`. The ordinal is
zero-based within its parent; roots carry neither field. The reader requires
unique contiguous ordinals and rejects missing, malformed or orphaned order
fields, unknown parents, self-parenting and cycles before replacing the current
scene. The writer and queued-save snapshot preserve the authored sibling links,
so reloads retain sibling order independently of entity IDs.

Native scene saves use an exclusively created sibling temporary file, checked
write/close and rename over the destination. Failure leaves the previous file
intact and removes the temporary. A destination symlink is replaced as a directory
entry; its target is not written through. This is atomic replacement, not an
`fsync` or power-loss durability guarantee. The explicit `IIOBackend` overload
retains that backend's own write semantics.

The reader and writer reject non-compact graph sources, endpoint indices outside
the vertex range, halfedge counts other than twice the edge count, endpoint/
halfedge-pair disagreement, out-of-range next/previous handles, non-reciprocal
next/previous links, and successor links that do not continue at the target
vertex. Versions 1 to 5 are rejected rather than upgraded. Supported
persistence is limited to current
sandbox-authoring CPU state: metadata names, stable ids, transforms, hierarchy,
selection eligibility, render hints, visualization configs, authored
`GeometryPresentationRecipe` values, structural render-attribute bindings
(`attributeBindings`, RUNTIME-315; an optional key, with
sources that do not resolve on load kept, drawn from the default and counted in
`StaleAttributeBindings`), and mesh/graph/point-cloud `GeometrySources`
including their typed property tables. The
next incompatible format change bumps `kSceneDocumentVersion`. The writer emits only `geometryPresentation`; the reader
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

## Initialization and frame order

`Engine::Initialize()` starts the shared Core scheduler before task-graph or
streaming dispatch. It creates the platform window, device and renderer, applies
an optional startup render recipe through `RenderRecipeActivation`, creates the
CPU frame graph and boot world, then registers and resolves the name-sorted
runtime modules. Registration publishes services before resolution borrows them;
the complete hook schedule is validated and locked. The app initializes its own
state afterward. Missing or invalid startup recipe files leave the derived
frame recipe active; invalid live recipe changes preserve the current override.
A requested but non-operational Vulkan device produces a diagnostic breadcrumb
and the existing Null fallback, not an Engine abort solely for that fallback.

`Engine::Run()` normalizes native close through `RequestExit()`, so `IsRunning()`
is false when it returns. The normal `RunFrame()` order is:

1. poll platform events, handle resizing, and take the minimized skip path when needed;
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
   topology with freshly bound callbacks (CORE-008); after the substeps, the
   runtime `Simulation` hook runs once per frame, including frames with no substep;
5. drain `Engine::Jobs()` completions before pump B; `JobService` checks token
   and world-scope cancellation on the main thread, drops suppressed results
   whole, and publishes completion events only for survivors per
   [ADR-0024](../adr/0024-kernel-module-architecture.md) D8 (ARCH-009);
6. pump the queued kernel event bus post-simulation, before UI/extraction;
7. runtime-module `UiBegin`, `UiBuild`, then `UiEndCapture` hooks. The optional
   `EditorUiModule` resets the host's viewport input request, applies the
   global `G` visibility toggle and opens the ImGui frame in `UiBegin`, draws
   registered contributions in `UiBuild`, and closes the frame, writes the
   adapter capture and then merges a contribution's viewport claim in
   `UiEndCapture`. SceneInteraction's `UiBegin` hook cancels a running gizmo
   session when the UI has just been hidden;
8. build `Graphics::RenderFrameInput`, then dispatch deterministic typed
   viewport-input hooks. Module-name order places optional Camera population
   before optional SceneInteraction pick input; both see completed editor
   capture. SceneInteraction cancels a gizmo session on a hide seen since
   `UiBegin`; the editor gizmo itself (ImGuizmo, app-only) previews during
   `UiBuild`. Flush pre-render transforms, dispatch generic input actions, then
   run `BeforeExtraction`, where SceneInteraction drains one pending pick and
   submits its copied render snapshot. This is not a seventh generic frame
   phase;
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

The render-world pool is used only through serialized owner-thread calls,
including its diagnostics. A scene replacement or active-world change clears
every renderer snapshot slot and advances the extraction cache's scene epoch.
The first successful publication in that epoch is consumed immediately, even
in pipelined mode; following frames resume consuming the previous front.
An exhausted pool does not acknowledge the new epoch, so a later successful
publication still starts the new scene without a stale frame.

After optional UI and input consumers, the engine discards remaining polled
window events immediately before `Renderer::BeginFrame`. This also covers a
failed renderer begin. Minimized frames retain their events so key releases and
focus changes reach the UI after restore; without an editor UI, events polled
while minimized remain queued until the next normal frame. The close/exit paths
do not drain the queue.

Minimized frames skip simulation, UI, extraction, render and normal maintenance.
When modules registered `Idle`, the bounded idle path drains commands, pumps
events, runs Idle hooks, drains completions, pumps again and reaps jobs. It does
not flush transforms or advance the render frame index. The frame clock
resamples after deliberate waiting; normal consumers use the previous completed
frame's duration. Before the first completed frame the clock returns zero and
ImGui retains its existing non-positive-delta fallback.

## Editor change and no-op policy

Geometry processing reports `Applied` from actual changed values or topology,
not a written/processed count. A no-op returns `NoChange` before history commit
and stamps no dirty tag. Panels display the result counters for both statuses.
Normal and scalar publications compare the exact stored values; an absent
output property counts as newly authored values. An epsilon cannot discard a
real stored change. Replacement meshes compare storage sizes, positions and
halfedge connectivity; counts alone miss an edge flip or tangential relaxation.
UV regeneration also compares texcoords. A mesh carrying garbage compares as
changed, conservatively. Subdivision either increases face count or fails
closed; it has no reachable successful no-op branch.

## Stable entity lookup policy

`StableEntityLookup` owns Runtime's durable `StableId` to live entity sidecar.
ECS owns only the ID value type; graphics consumes render IDs and never resolves
durable IDs. `ToRenderId` encodes the full live handle as its uint32 value plus
one, reserving zero for background. Render-ID resolution decodes and validates
the live handle instead of maintaining a second map.

For duplicate durable IDs, the live entity with the smallest `ToRenderId` wins
independently of iteration order; duplicate occurrences increment diagnostics.
Invalid IDs are never tracked. `Rebuild` re-derives winners; forgetting a winner
drops that mapping until another Track/Rebuild. Resolve lazily rejects and
prunes stale handles; `PruneStale` performs the bulk equivalent. Missing or
sentinel IDs return `nullopt`. Tracking reassignment drops that entity's previous
winner entry before inserting the new ID.

`SceneInteractionModule` owns the lookup and its registry binding, connects
StableId construction/update/destruction to Track/Forget, and lends it to the
selection controller. Scene replacement disconnects before clearing the old
registry and reconnects/rebuilds against the new contents. Active-world mismatch
resets/rebinds before resolution; no per-world lookup cache survives away/back.
Standalone selection controllers without an attached lookup retain their direct
handle decode plus validity check. Reference-scene population assigns durable
IDs; it is not an exception that leaves all reference entities transient.

## Geometry property vocabularies

`GeometryAvailability` owns canonical property naming. These distinct types
answer different questions and must not be folded together:

| Vocabulary | Meaning |
| --- | --- |
| `GeometryElementDomain` | The property's element domain. |
| `Geometry::PropertyValueKind` | Stored value type. |
| `GeometryPropertyValueKindFilter` | Accepted type (`nullopt` accepts any). |
| `GeometrySources::Domain` | Geometry provenance. |
| `GeometryRenderLane` | Consuming render lane. |
| `VertexChannel` | Structural vertex stream. |
| `Graphics::MaterialChannel` | Material slot. |

Scene serialization owns its wire spellings independently of debug names:
`ScalarFloat`/`ScalarDouble`, `GraphVertex`/`Point`, and the accepted `MeshSurface`
wire domain mapped to `Unknown`. Do not derive them from canonical debug names.
Current-format scene round-trip tests pin these spellings and reject canonical
name substitutions.

## Derived overlay representation

Cross-domain child overlays use ordinary authored/imported `GeometrySources`
entities. Mesh edge/vertex overlays use `RenderEdges`/`RenderPoints` primitive
sidecars over the parent mesh. Vector fields and isolines use copied data-only
`VisualizationRecipes` packets in `RuntimeRenderSnapshotBatch`, without child
ECS entities or graphics handles in ECS. No separate `DerivedOverlayProducer`
module is required by these current workflows.

## Consolidation radius admission

The [support-radius policy](support-radius-policy.md) owns automatic rank
selection, manual-radius admission, memory/work limits, backend preflight and
terminal telemetry for point-cloud consolidation.

## Coherent Point Drift editor integration

`Extrinsic.Runtime.RegistrationOperations` also owns Coherent Point Drift (RUNTIME-273,
reference `Geometry.Registration.CoherentPointDrift`). `StartEditorCoherentPointDrift`
validates `sandbox.coherent_point_drift` (`Extrinsic.Runtime.CoherentPointDriftConfig`,
declared in a `ConfigFieldSpec` table) and captures both operands in world space;
`StepEditorCoherentPointDrift` queues one job running N iterations or to the end, with
cancellation checked every iteration; the worker alone touches the solver and publishes a
mutex-guarded snapshot (phase, trace, moving source positions) after each iteration, read by
`SnapshotEditorCoherentPointDrift`. `ApplyEditorCoherentPointDrift` revalidates captured
property revisions and both entity transforms, then publishes one history entry: the source
Transform for rigid results (mirrored results are refused), or the source positions or a
named vec3 displacement property in source-local space for affine and nonrigid results.
Writing mesh-vertex positions also recomputes a stored `v:normal` (area-weighted, in the
same history entry). Operands are mapped with their local Transform only, so parented
entities are refused. `ApplyEditorCoherentPointDriftCommand` chains start, a full run and publication with one
terminal callback; agents reach it through `run_registration`.

The section also selects the METHOD-049 backends: `e_step` (reference, dense, truncated,
auto, fast Gauss, and the approximate METHOD-053 Nystroem with `nystrom_landmarks` and
`nystrom_error_limit`, whose sampled error estimate results report; the editor default is auto with `e_step_tolerance` 1e-6), `threads` and
the nonrigid `low_rank`, which lifts the full-kernel limit of 8192 source points. Results
carry the backend that actually ran (`cpu_auto` or `cpu_mixed` when iterations used
different policies); each trace row carries the E-step policy that ran and its error bound;
low-rank runs report the kernel rank and its sampled approximation error. The `vulkan`
E-step (METHOD-056) runs the dense two-pass form on the device while the kernel is wide:
`Extrinsic.Runtime.CoherentPointDriftGpuEStep` is a per-run broker whose `Queue`
returns an owned request event. The JobService coroutine uses the solver's
`BeginStep`/`PendingExternalRequest`/`CompleteExternalStep` path, parks while a
companion main-thread pump submits through `SpatialIndexCache::QueueGpuCompute`
as an immediate request (GRAPHICS-150),
and hands the readback back, usually within the frame. The broker leases its workspace from
`SpatialIndexCache::LeaseGpuWorkspace` for each step and returns it when the step's pump ends;
workspace buffers only grow, the float-float target is converted once and uploaded only when
the workspace last held another target generation, and the per-iteration source staging is
reused. Only the E-step is on the device: the M-step, objective and exactly evaluated rows stay
on the CPU, so each iteration still uploads the moved source (32 bytes per point, plus 4 bytes
per row of skip flags when any) and reads back `2n + 4m` doubles. Without the job
lane, an operational device with shader float64, or after a device failure or a timeout observed by the owner pump, the
iterations run the exact CPU choice; results count them in `e_step_fallbacks` and say why in
`gpu_diagnostic`, and count device iterations in `e_step_device_iterations` (kernels too narrow
for fp32 terms stay on the CPU by design and are not fallbacks). The `bayesian`
method (METHOD-050, BCPD) adds `gamma`, `kappa` (0 keeps equal mixing weights) and
`subsample` (farthest-point samples registered, deformation interpolated to every point);
like nonrigid it writes positions or a displacement property. The shared
`Extrinsic.Runtime.PointSamplingConfig` block (RUNTIME-289) makes the sampling method
selectable: `subsample_*` chooses how the Bayesian source and target subsamples
(`subsample`, `subsample_target`) are picked, `landmark_*` how the low-rank and Nystroem
landmarks are picked (every `Geometry.PointSampling` method; exact farthest point by default). Point
cloud consolidation takes the same block as `initial_*` for its initial samples (Random keeps
the seeded legacy subsample; CPU and Vulkan paths share `SelectInitialSamples`).
`subsample_backend` (CPD) and `initial_backend` (consolidation, `"cpu"` / `"vulkan"`) order
those samples on the device: a framed job runs exact farthest point through
`Extrinsic.Runtime.PointSamplingGpu` on the resident positions (CPD: captured live rows and
model; consolidation: local rows) before the solver job, which depends on it and takes the
verified order (`CPD::Params::SubsampleTargetOrder` / `SubsampleSourceOrder`,
`Consolidation::Params::InitialOrder`; the geometry rejects orders that repeat or exceed an
index). Any other case orders on the CPU and says why. CPD reports
`subsample_requested_backend`, `subsample_target_backend`, `subsample_source_backend` (empty
when that set is not subsampled) and `subsample_backend_diagnostic`; consolidation reports
`initial_sampling_requested_backend`, `initial_sampling_backend` (empty for the random
subsample) and `initial_sampling_diagnostic`, apart from its solve backend. A run that ends
before solving (cancelled, rejected input) reports the backend that was to order its samples.
CPD landmarks always stay on the CPU. Cancellation wakes the parked coroutine and
abandons the pending step before CPU fallback or the M-step. Request latency
statistics do not measure blocked worker time.


## Point sampling editor integration

`Extrinsic.Runtime.PointSamplingOperations` (RUNTIME-274) runs any `Geometry.PointSampling`
method on one entity's point domain (point clouds, graph nodes, mesh vertices) from the
`sandbox.point_sampling` section: `count` samples (0 orders every point), the shared sampling
block without a prefix, an optional float `weights` property (importance weights or Poisson
priority scores), and the output — a float rank plus a bool selection property on the source
(one undoable step) or a new point-cloud entity with the samples in world space. Points are
sampled in the entity's world frame, so parented entities are refused. The editor's "Point
Sampling" window and the agent tools `preview_point_sampling` / `run_point_sampling` use it.
CPU runs are synchronous. `backend` = Vulkan (RUNTIME-290) goes through the seam
`Extrinsic.Runtime.PointSamplingGpu`: methods with a device kernel (exact, optionally weighted,
farthest point via `Extrinsic.Graphics.FarthestPointSampling`) run as a framed GPU job in
bounded chunks whose order prefixes only grow; the first 64 samples are recomputed on the CPU
before the result may report `gpu_vulkan_compute`. Every other case (another method, no job
lane or device, no shader float64, a device failure or mismatch) runs the CPU order and
reports `requested_backend`, `backend` and `backend_diagnostic`.

Framed GPU jobs of editor operations share `EditorFeatureDetail::MakeFramedGpuJobDesc`
(`Editor/internal/Runtime.EditorFramedGpuJob.hpp`): the worker does nothing, the main thread
queues `SpatialIndexCache::QueueGpuCompute` chunks from `IsReadyToApply` until the operation's
`Observe` says done, then publishes (keypoints and point sampling use it).

## Render artifact publication

Renderer outputs become observable as runtime artifacts before any project
data changes. `Extrinsic.Runtime.RenderArtifactPublication` stores copied
`Graphics::RenderArtifactMetadata` plus runtime-owned lifecycle kind, UI status,
payload target, provenance, diagnostics, revision counters, and audit records.
Registering an artifact is a side-effect-free declaration from the project-data
point of view. Registering a newer artifact for the same renderer/snapshot/view
recipe/output purpose with different source revisions supersedes the older
record instead of silently replacing it.

Publish and apply are explicit command surfaces. Publish requires provenance and
a target URI, and moves an unpublished artifact to `Published`. Apply requires a
published `CandidateProjectResult`, provenance, a project target, and an undo
label; it records that a project mutation is authorized for the caller-owned
command path while the registry itself performs no ECS, scene, UI, renderer, RHI,
or file-persistence mutation. Runtime/editor callers can wrap the apply command
in `EditorCommandHistory`; undo returns the artifact to `Published` and appends
an audit record. UI-facing status is deterministic across `Unpublished`,
`Stale`, `Canceled`, `Failed`, `Superseded`, `Published`, and `Applied`.

## Camera controls and startup UI

`EngineConfig::Camera` selects an enabled Orbit controller by default; Fly,
FreeLook and TopDown are also constructible. Without a world seed, the main
controller uses a perspective camera at `(0, 0, 4)` looking down `-Z`. Omitting
`CameraModule` produces neither a registry nor a fallback camera.

Right- or middle-mouse drag rotates Orbit/Fly/FreeLook, WASD moves or pans,
Shift accelerates movement, and the wheel zooms Orbit/TopDown. The app-owned
focus-on-selection action exists only when both the exact camera registry and
selection controller resolve. Controller replacement marks a one-shot
`ExplicitCameraTransition` in the extracted view so graphics can discard stale
previous-frame HZB occlusion without reading Runtime state.

The Sandbox starts with only its menu bar visible. Registered windows start
closed; the app owns their open bits, and global hide preserves those bits.
Closed windows receive no draw callback and their selected-entity model
sections are omitted from the workspace snapshot request. Runtime owns the
models and commands; the shell owns widget and window state.

The ImGui adapter does not advertise native cursor-shape or mouse-warp support.
Cursor requests, including the parameterization splitter resize cursor, are not
applied to the native window; clipboard and ordinary pointer events remain
available through the platform port.

## Geometry upload and residency

`RenderExtractionCache` owns live ECS queries, typed topology adaptation, and
entity-to-instance sidecars; it no longer owns a GPU geometry allocator, cache,
or retire queue per domain. The private `Extrinsic.Runtime.GeometryPlanBuilders`
module is registered only in `ExtrinsicRuntime`'s private CMake module set. Its
free functions translate mesh, graph, point-cloud, procedural, mesh-edge, and
mesh-vertex inputs into owning `Graphics::GeometryUploadPlan` values. Each plan
contains a graphics-only stable key and generation, copied vertex/index/channel
bytes, fixed formats, an update class and channel mask, a storage hint, bounds,
and a debug name. The copy never borrows ECS property or topology storage.

Optional normal and color authoring uses the canonical runtime
`GeometryPropertyRef` (`domain`, `name`, `value kind`). Editor commands, config
state, and imported-scene handoff persist that identity; the private adapters
validate the expected domain/value kind, resolve it against the current
`GeometrySources` snapshot, and send only resolved bytes to graphics. No ECS,
property-set, or topology type crosses into `src/graphics/*`.

All plans enter the concrete graphics-owned
`GeometryResidencyCoordinator`, composed with the existing `GpuWorld`.
Entity-unique mesh, graph, point-cloud, edge-view, and vertex-view keys use
`Reconcile`; the procedural `(Kind, Hash(Params))` identity uses `Acquire` so
identical entities share one handle and reference count. The coordinator owns
validation, stale-generation rejection, same-generation reuse, in-place partial
channel updates, full-replacement fallback, retire cancellation, frame-safe
free, and hard shutdown. Runtime performs no direct `UploadGeometry`,
`UpdateGeometryChannels`, or `FreeGeometry` call. Maintenance invokes exactly
one `RenderExtractionCache::TickGeometryResidency` per frame and maps the freed
key namespace back into the existing per-domain diagnostic counters.

The domain adapters remain deliberately typed and private:

- Mesh surface plans use `Extrinsic.Runtime.MeshSurfaceTopology` as the one
  public topology-only truth for bounded ring validation, deleted-face skipping,
  fan triangulation, and triangle-to-face order. Finite UVs are preserved with a
  deterministic zero fallback; finite normals are normalized with a +Z
  fallback; optional color becomes packed unorm8. Selection refinement, UV
  diagnostics, normal-bake identity, extraction, and GPU acceptance consume the
  same topology order.
- Graph plans keep node positions in one geometry shared by independently
  submitted line and point instances. Requested-lane changes rebuild the plan
  because the line-index payload changes.
- Point-cloud plans carry positions without indices and remain valid only for a
  point lane with a uniform size source; unsupported surface/edge lanes or named
  per-point size sources fail closed.
- Mesh edge/vertex-view plans use separate residency keys and retained instances
  over the authoritative mesh source. Edge topology is explicit when available
  and otherwise derives from the canonical surface topology; a failed view does
  not disturb the surface or sibling view.
- Procedural triangle plans retain content-addressed sharing, source-conflict
  diagnostics, refcount saturation protection, and resurrection inside the
  deferred-retire window without a separate procedural cache or test seam.

Dirty vertex-channel tags request `PartialPreferred`; topology, lane-mask,
coarse dirty, vertex-count, or unsupported partial changes fall back to full
replacement through the coordinator. A failed dirty plan or rejected upload
releases stale residency and leaves the dirty signal available for recovery.
The existing `*FailedPack` statistic field names remain compatibility
observability only; they do not imply a public packer or a second lifecycle.

Runtime plan builders copy channel bytes, update class/mask, bounds, and identity
directly into the sole implemented uniform-SoA residency contract. The live
`GpuGeometryResidencyView` reports exact channel descriptors rather than a
fixed storage-lane field.

`FindRenderableSidecarForTest(stableId)` exposes handles and boolean residency
facts for procedural, mesh, graph, point-cloud, mesh-edge, and mesh-vertex lanes
without exposing coordinator keys, plan-builder scratch, reference counts, or
retire queues. Payload/layout parity is asserted through
`GpuWorld::TryGetGeometryResidencyView(...)` fingerprints and counts; lifecycle
contracts target `GeometryResidencyCoordinator` directly.

## Related references

- Historical details: `runtime-subsystem-boundaries.md` (`legacy-background`).
- Physics module ownership: [physics.md](physics.md).
- Layer policy: [layering.md](layering.md).

## Primitive selection

Editor domain/inspector and parameterization-view queries accept an optional
explicit entity. It binds property catalogs, readiness, cached analysis and UV
preview/commands without changing scene selection. Omitting it uses current
selection; an explicit zero produces an empty model.

The existing interaction module owns a shared selection service for entity and
canonical primitive domains. See [primitive selection](primitive-selection.md)
for lifecycle, config, method-input, and viewport contracts.

ICP uses the existing editor command/job owner with the shared spatial-index cache. [Registration ownership and config](registration.md) describes canonical operand binding and framed GPU correspondence batches.

### Keypoint analysis

[ISS-style keypoint analysis](keypoint-analysis.md) uses the existing geometry operations, spatial cache and jobs. Scale preparation precedes complete radius queries and CPU covariance/suppression; one revision-checked history command publishes both named output properties. Shared private property-watch/domain helpers serve keypoints, outliers and bilateral filtering without a new service or module boundary.

FPFH histogram analysis uses the shared [descriptor config, spatial queries and property publication](descriptor-analysis.md) on compatible position/normal domains.

[Compact density weights](density-weights.md) use the same geometry operations and spatial cache. Private radius-row pagination is shared with keypoints and descriptors; numerical reductions and guarded publication stay with each method.

## Editor diagnostics stream

`Extrinsic.Runtime.DiagnosticsStream` projects the core log cursor into runtime
records consumed by the Sandbox and agent operations. Engine provides one
`EditorDiagnosticsStream` through `ServiceRegistry`, freezes its device bootstrap
configuration at attachment, and detaches its borrowed device during shutdown.
Live readiness remains `IDevice::IsOperational()`; Vulkan diagnostics supply only
validation observations. The service and its bounded 256-operation history are
main-thread owned, while the core logger retains its existing mutex for concurrent
writers and snapshot copies. There is no new logging API or validation toggle.
The [agent control lane](agent-control-lane.md#diagnostics-readers) defines cursor,
filter, drop, operation-lifetime, and allocation-counter semantics.
