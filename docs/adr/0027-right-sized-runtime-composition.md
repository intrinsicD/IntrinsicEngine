# ADR 0027: Right-sized runtime composition by ownership outcome

- **Status:** Accepted
- **Date:** 2026-07-18
- **Owners:** Runtime / Architecture
- **Related tasks:** `ARCH-014`, `ARCH-016`, `RUNTIME-129`, `RUNTIME-168`,
  `RUNTIME-172`, `RUNTIME-179`..`RUNTIME-188`, `REVIEW-003`
- **Amends:** [ADR-0024](0024-kernel-module-architecture.md) D1, D2, D3, D9,
  D10, D11, D12, and D13
- **Uses:** [ADR-0026](0026-runtime-module-scope-by-consumer-contract.md)

## Implementation update: RUNTIME-172

`RUNTIME-172` reruns the ADR-0026 cohesion audit and disproves the original
single Scene editing hypothesis:

- app-composed `SceneDocumentModule` owns one validated active-world binding,
  document path/event sequence, optional queued scene IO, and the exact
  `EditorCommandHistory`; it publishes the two concrete capabilities and
  retains no per-world state map;
- synchronous new/load/close transitions use one plain strong-handle
  participant contract. A load parses first, then callbacks run deterministically
  before and after replacement; the document owner imports no selection,
  readback, extraction, asset, bake, or GPU owner;
- temporary Engine adapters captured only the exact long-lived interaction and
  asset-handoff objects required by the two demonstrated consumers.
  `RUNTIME-188` subsequently removed the interaction adapter;
  `RUNTIME-183` subsequently removed the remaining asset adapter. Active-world
  Maintenance remains separate from document replacement;
- queued callbacks use weak module state and validate module generation,
  binding epoch, world, and registry immediately before commit. Shutdown
  announcement invalidates work while still permitting dependent modules to
  release their exact participant handles; and
- Engine's public interface loses the two document/history imports and the
  `GetScene`, `GetSceneDocument`, and `GetEditorCommandHistory` facade names.
  The implementation names the exact services only in the two tracked
  transition adapters above. The exact post-slice snapshot is 33 plain imports /
  11 domain imports / 2 re-exports / 22 public getter names.

The observed document and interaction cohorts differ in dependencies, frame
hooks, cancellation, published state, and omission behavior. The corrected
responsibility map therefore records `SceneDocument` and `SceneInteraction`
separately rather than forcing them through a wrapper bundle.

## Implementation update: RUNTIME-188

`RUNTIME-188` applies the separate SceneInteraction hypothesis:

- optional app-composed `SceneInteractionModule` privately owns selection,
  stable lookup/binding, readback/refinement context, and gizmo state behind one
  PImpl and publishes only the exact module and exact `SelectionController`;
- one validated `{WorldHandle, Registry*, interaction epoch}` binding clears as
  a unit on document replacement, active-world mismatch/retirement, shutdown,
  and reinitialize. Pick issue sequences remain monotonic and completed results
  must match a known nonzero sequence, world, and epoch;
- deterministic typed viewport input runs after Camera and completed capture;
  `BeforeExtraction` runs after transform flush/input actions and submits a
  copied world-tagged selection/hover/gizmo snapshot; `Maintenance` drains
  readbacks. No generic phase/context was added and extraction retains no
  interaction pointer;
- the module retains/releases its strong document participant. Engine retains
  only the then-named `RUNTIME-183` optional selection borrow for asset-import
  dependencies at this intermediate slice; the implementation update below
  removes it; and
- the zero-consumer Engine mesh primitive-view facade, translation module, and
  extraction settings cache are deleted. Persistent ECS `RenderEdges` /
  `RenderPoints`, component-driven packing/extraction, Sandbox history, and
  scene serialization remain independent authoring state.

The exact post-slice snapshot was 26 plain imports / 4 domain imports /
2 re-exports / 15 public getter names.

## Implementation update: RUNTIME-183

`RUNTIME-183` applies the AssetWorkflow hypothesis:

- optional app-composed `AssetWorkflowModule` keeps one persistent
  dependency-empty import pipeline and normal-bake service, recreates per-boot
  asset/cache/listener/handoffs, and publishes exactly `AssetService`,
  `AssetImportPipeline`, `GpuAssetCache`, and the existing asset-maintenance
  hook;
- Engine publishes the exact built-in device and one read-only initialized
  state borrow through `EngineSetup`. The asset module requires document and
  exact history services, optionally resolves streaming and selection, and
  adds no wrapper, owner facade, shutdown event, frame phase, or GPU bridge;
- one strong document participant and direct
  `{WorldHandle, Registry*, binding epoch}` validation clear/rebind scene
  borrowers. Direct imports and model-scene callbacks fail closed across
  delayed and away-and-back binding changes;
- shutdown announcement is split from ordinary reverse module shutdown. The
  asset listener cancels imports and detaches provider/document borrows before
  application or provider teardown; the generic GPU bridge drains the retained
  bake participant before module cleanup; and
- Engine loses all asset/import/cache/bake imports, members, transition glue,
  and five getter names. Omission retains generic world/render/transfer/async
  work plus all five render-extraction geometry-retirement lanes.

The exact post-slice snapshot is 22 plain imports / 0 domain imports /
2 re-exports / 10 public getter names.

## Implementation update: RUNTIME-168

`RUNTIME-168` applies the one-consumer composition deletion test to Sandbox
default policies:

- the exported `Extrinsic.Runtime.SandboxDefaultPolicies` module and its
  Engine-bound register/unregister lifecycle helper are deleted. The existing
  `Extrinsic.Runtime.SandboxEditorFacades` module exports four plain descriptor
  factories, while the callback bodies remain in its private
  `Runtime.SandboxDefaultPolicies.cpp` implementation unit;
- Sandbox requires the exact published `AssetImportPipeline` and exact built-in
  `RuntimeInputActionRegistry` before registering anything. One file-local
  record retains only those provider borrows and the typed handles, installs in
  fixed order, rolls partial failure back in reverse order, and makes repeated
  shutdown a no-op;
- the import-completed descriptor captures only the optional camera registry.
  Auto-selection consumes the `SelectionController` supplied by the pipeline's
  completion services; the separate `F` descriptor captures exact camera and
  selection references and is registered only when both optional services
  exist; and
- shutdown announcement cancels imports and detaches provider borrows, the
  generic GPU bridge drains participants next, application shutdown unregisters
  the app-private handles while both required registries remain live, and
  reverse AsyncWork/AssetWorkflow teardown follows.

No new owner, wrapper, registry, service, lifecycle abstraction, or Engine
surface is introduced. The exact Engine snapshot remains 22 plain imports /
0 domain imports / 2 re-exports / 10 public getter names.

## Implementation update: RUNTIME-180

`RUNTIME-180` applies the Camera hypothesis without widening the generic
schedule:

- the app-composed `CameraModule` publishes the exact
  `CameraControllerRegistry`, whose controller/pose/transition/seed state is
  bound to one `WorldHandle` and always cleared on reset;
- `Runtime.Module` adds one narrow `RuntimeViewportInputHookContext` and
  callback registrar at the established post-capture, pre-gizmo insertion
  point; `FramePhase` remains six values and `RuntimeFrameHookContext` remains
  unchanged;
- `RuntimeModuleSchedule` deterministically orders those typed hooks by module
  name and shared registration sequence and clears them with the other
  contribution records;
- Sandbox owns the exactly-once initial-world reference-content bootstrap,
  original-world teardown, and optional camera-seed handoff; generic Engine
  has no camera/reference ownership or facade; and
- the exact post-slice convergence snapshot is
  35 plain imports / 13 domain imports / 2 re-exports / 25 public getter
  names.

This is the deletion test in action: one production viewport consumer earns
one typed callback context, not a seventh generic phase, widened generic
context, camera wrapper service, or reference-provider framework.

## Context

ADR-0024 correctly requires a domain-free Engine, explicit application
composition, narrow capabilities instead of `Engine&`, and an explicit
world/global lifetime for durable state. Its target text also prescribed one
specific implementation shape for nearly every responsibility:
`IRuntimeModule`, a two-phase `ServiceRegistry`, `RuntimeModuleSchedule`,
extension-pass registration, a priority input-capture filter chain, and an
`InlineModule` builder.

At ADR intake, the live tree did not justify that whole mechanism set:

- `ClusteringModule` is the only production `IRuntimeModule` implementor and
  Sandbox contains the only production `EmplaceModule` call.
- `ClusteringModule::OnRegister` uses the command, event, job, world, and
  service capabilities of `EngineSetup`, plus shutdown. Its `OnResolve` is a
  no-op. It registers no sim system and no frame hook.
- `ServiceRegistry` receives six production provisions: five Engine-owned
  records and `ClusteringService`. Only two provided types have production
  `Find` consumers: `RenderExtractionCache` and `ClusteringService`. No
  production code calls `Require`.
- `RuntimeModuleSchedule` receives no production sim-system or frame-hook
  record. Its duplicate, cycle, signal, and ordering behavior is exercised
  only by contract tests.
- No production responsibility registers a D10 extension pass. The renderer
  currently executes the closed built-in pass vocabulary selected and
  configured by recipe data.
- ImGui is the sole production input-capture producer. It records one
  end-of-editor-frame `EditorInputCaptureSnapshot`, which gates camera, gizmo,
  picking, and input-action consumption. There is no priority chain.
- There is no `InlineModule` symbol, production experiment app, or repeated
  experiment bootstrap to extract into a template.
- `WorldRegistry` and `WorldHandle` are live kernel substrate. There is no
  production preview-world readiness or staged-switch policy requiring a
  `WorldSwitchModule`.

The final `RUNTIME-185` validation on 2026-07-23 re-ran that deletion test
after all behavior-carrying owners landed. The production tree has ten
`IRuntimeModule` implementors (nine runtime-owned responsibilities plus the
Sandbox-local optional frame-pacing capture), seven generic frame-hook
registrations, two typed viewport-input hooks, and zero sim-system
registrations or causal wait/signal edges. Real cross-owner `Require` calls
prove two-phase `OnResolve`; six Engine-built-in provisions have lookup
consumers. The audit therefore retained the lifecycle, two-phase service core,
and exact hook schedule while deleting the sim descriptor/context/registrar,
causal DAG and fixed-step branch, `AfterCommandDrain`, the hook-context phase
echo, resolve-phase registrars, three unconsumed built-in provisions, and
test-only registry statistics/copied error-list access.

The reconciliation audit recorded 42 plain imports, 21 domain imports, two
re-exports, and 31 public `Engine::GetX()` names. That domain count included
`Extrinsic.Runtime.WorldHandle`; this ADR classifies the type as kernel
substrate. The corrected exact snapshot for the same source is therefore
42 plain imports, 20 domain imports, two re-exports, and 31 getters.

Literal mechanism rows made `ARCH-014` depend on building zero-consumer
frameworks before `REVIEW-003` could review whether those frameworks should
exist. This ADR breaks that decision loop. It preserves the ownership and
coupling outcomes while making every composition mechanism pass a present-use
deletion test.

## Decision

### 1. Amendment scope and vocabulary

ADR-0024 D4 through D8 remain unchanged. In particular, command/event pump
ordering, job snapshot and cancellation rules, world teardown, and explicit
`WorldHandle` use remain kernel contracts. D2 is refined only to distinguish
the live `WorldRegistry`/`WorldHandle` substrate from the deferred,
zero-consumer `WorldSwitchModule` policy.

For D1, D9, and D12, **runtime module** is a logical term: one
ADR-0026-cohesive, app-composed responsibility with explicit ownership and
lifecycle. It does not mean that every responsibility must implement the
current C++ `IRuntimeModule` interface or own a one-to-one wrapper file.

The kernel test becomes:

> Required to execute every frame independent of application policy is kernel
> substrate. Everything else is an explicitly app-composed responsibility.

A domain noun is a discovery warning, not a proof by spelling. Conversely,
hiding domain state in an Engine-private owner does not make it substrate.
`WorldRegistry`, `WorldHandle`, command/event/job pumps, render extraction,
frame scheduling, and recipe activation are substrate because the frame loop
directly owns or iterates them.

ADR-0026 decides whether present responsibilities share one logical owner.
This ADR decides which current composition mechanisms earn their keep. Neither
decision predeclares a family taxonomy.

### 2. Accepted ownership outcomes

The amended target retains all of these outcomes:

1. `Runtime.Engine` has no domain import, domain re-export, domain-owned member,
   or domain-facade getter in its public interface.
2. The application explicitly constructs and composes every optional
   responsibility. Engine never imports a concrete domain responsibility to
   make that composition convenient.
3. No handler, setup, or composed-responsibility surface receives `Engine&`.
   Narrow contexts carry only the capabilities used by the operation.
4. Every durable owner states whether it is global or keyed by `WorldHandle`,
   and states its reset, rebind, cancellation, and destruction behavior when
   the active world changes or a world retires.
5. Commands, queued events, jobs, ECS state, and direct narrow capabilities
   remain the default communication paths. Hidden ownership, mutable
   backreferences, and ad-hoc peer lookup remain forbidden. The app may pass
   an explicit narrow non-owning construction capability when its lifetime is
   declared and it is the simplest wiring for a required peer; typed service
   discovery remains the option for order-independent or optional peers.
6. A responsibility is not considered extracted merely because an
   Engine-private class or forwarding getter has been created. The app
   composition point, owner, state scope, and consumer-facing interface must
   all be visible outside `Runtime.Engine`.

### 3. Composition-mechanism verdicts

| Mechanism | Present evidence | Verdict and deletion test | Reintroduction or expansion trigger |
| --- | --- | --- | --- |
| `IRuntimeModule` | Clustering, async work, asset workflow, texture bake, document, interaction, camera, config control, and editor UI are runtime-owned production implementors composed by the Sandbox; the app also has one optional frame-pacing capture implementor. Stable name, registration, app optionality, Engine-owned lifetime, ordered shutdown, and order-independent exact dependency resolution are live. | **Keep, narrowed and optional.** It is the type-erased app-to-runtime seam that lets Engine own optional responsibilities without importing their types. Deleting it today recreates equivalent erased callbacks, domain-specific Engine entry points, or `Engine&` app wiring, so its core complexity does not vanish. It is not a required wrapper for every cohort. `Name`, registration, resolution, and shutdown have production callers. | Add surface only when a named production responsibility needs it; a test double is not evidence. |
| `EngineSetup` and shutdown context | Production modules use commands, events, jobs, worlds, services, the active recipe/config borrow, the read-only initialized-state borrow, shutdown, the generic frame-hook registrar, and the typed viewport-input registrar without receiving `Engine&`. No production caller used the removed sim-system registrar. | **Keep the demonstrated capabilities only.** The context is load-bearing for D13 and concentrates validation. Hook registrars exist only during registration; resolution receives closed registrars. The typed viewport context stays separate from the five generic phases. | Add scheduling surface only with the first production behavior that the frame loop must iterate; use the smallest consumer-coherent context and do not add a generic registrar for a roadmap noun. |
| `ServiceRegistry` | Engine and composed modules publish exact borrowed capabilities. Sandbox resolves optional asset, document/history, camera, interaction, and config services through `Find`; multiple modules use `Require` during `OnResolve` and fail closed on missing exact providers. Exactly six Engine-built-in provisions have production lookup consumers. | **Keep the typed discovery core and two-phase resolution used by production modules.** `Provide`/`Find`/`Require`, null and duplicate rejection, provider identity diagnostics, exact withdrawal, and locking after boot avoid domain getters and make registration order irrelevant. Unconsumed provisions, aggregate statistics, and copied diagnostic lists were removed. | Expand only when a named production responsibility depends on another provider and registration order must remain irrelevant. |
| `RuntimeModuleSchedule` | Engine calls the schedule. Production registers seven generic hooks across Editor UI, texture bake, scene interaction, and Sandbox frame-pacing capture, plus two typed viewport hooks from camera and scene interaction. No production sim system or causal edge exists. | **Keep only the demonstrated hooks.** Generic hooks sort by phase/module/registration sequence; typed viewport hooks sort by module/registration sequence. `RUNTIME-185` removed sim records, fixed-step insertion, and the signal DAG rather than preserving them for tests. | A first real fixed-step contribution justifies designing its smallest seam. General causal DAG ordering is justified only when interacting production systems prove it; a second coherent viewport consumer may reuse the current typed context. |
| D10 extension-pass registry and insertion slots | Zero production extension registrations; built-in recipe kinds cover the current renderer. | **Defer the registry and slot taxonomy.** Keep a closed core pass vocabulary expressed as recipe data, with schema, capability, named-resource, and dependency validation. Point-splat lighting and order-dependent transparency remain design probes, not blockers. | A named production pass that cannot be expressed as an existing built-in kind must bring its resource contract, required insertion semantics, Null/CPU contract evidence, and capability-appropriate backend evidence. Only that pass's demonstrated slot is added. |
| D11 priority input-capture filter chain | One ImGui producer records one snapshot; several viewport consumers read it. There is no competing claimant. | **Replace the chain target with the proven snapshot contract.** One data-only kernel capture value is owned by the frame loop for the duration of a frame and resets to “unclaimed” once at frame start. Each ephemeral frame-hook context borrows that same value by reference. The EditorUi owner runs `BeginFrame` in `UiBegin`, preserves the application variable tick, draws registered app panels in `UiBuild`, then runs `EndFrame` and writes capture in `UiEndCapture`; later viewport behavior, any later hooks, and kernel input-action dispatch read that completed value. This carrier adds no registry, callback facade, or ImGui import to Engine. | A second independent simultaneous capture producer must demonstrate an actual conflicting claim that cannot be represented by extending the single snapshot. Only then is an explicit arbitration policy or chain justified. |
| D12 `InlineModule` builder | No symbol, production experiment app, or first consumer. | **Defer.** A one-file experiment may first use a concrete app-owned responsibility and the existing narrow composition seam. | A named one-file experiment that needs runtime lifecycle, scheduling, removal, and headless testing may justify the smallest builder. Extract an experiment-app template only after bootstrap is repeated by a second production experiment app. |
| `WorldSwitchModule` | `WorldRegistry` and deferred world operations are live; no production preview/readiness/switch policy exists. | **Defer the policy owner.** `WorldRegistry` and `WorldHandle` remain kernel substrate; no empty module is created. | A production app creates a secondary or preview world and needs readiness tracking plus a staged activation decision distinct from the registry's deferred mechanism. |

The keep verdict for `IRuntimeModule` is not permission to freeze its current
surface. `RUNTIME-185` measured the landed production tree and removed every
remaining method or branch retained only by a test double. Future additions
must repeat the same present-use test.

### 4. Current responsibility hypotheses and state scope

The live Engine responsibilities group into the following eight implementation
hypotheses. They are bounded child-task starting points, not an architectural
taxonomy. Each child must rerun ADR-0026 and split or merge when lifecycle,
state, commit, or consumer evidence disagrees.

| Hypothesis | Present responsibility | Required state-scope decision |
| --- | --- | --- |
| Async work | `AsyncWorkService`, the persistent streaming executor, derived-job ownership, and maintenance draining | Global owner. Submitted work carries explicit cancellation/commit scope; no worker borrows a live world. |
| Asset workflow | App-composed `AssetWorkflowModule` owns persistent import/bake objects plus per-boot CPU asset service, GPU residency/listener, and model handoffs | Global module object. Scene references carry an exact validated `{WorldHandle, Registry*, binding epoch}` and are rebound on active-world events or synchronous document replacement, never hidden ECS ownership. Shutdown announcement cancels and detaches borrows before providers; per-boot state dies only after generic GPU/async drain. Normal bake remains in this cohort while its import/residency lifecycle and consumers are cohesive. |
| Scene document | App-composed `SceneDocumentModule`, command history, scene-file event identity, and synchronous replacement coordination | Global module object with durable document state bound to one exact live active `{WorldHandle, Registry*}`. Switch, retirement, shutdown, and recycled-handle reinitialization reset rather than cache state. Optional async adds queued IO but does not change the synchronous owner. |
| Scene interaction | App-composed `SceneInteractionModule` owns selection, stable lookup/binding, pick readback/refinement, and gizmo state. Persistent mesh primitive-view authoring remains ECS component state. | Global module object with one exact validated active-world binding and interaction epoch. Replacement/switch/retirement/shutdown/reinitialize clear without resurrection; pointer-free snapshots are copied and world-tagged. |
| Camera | App-composed `CameraModule` owns camera-controller/viewport state and active camera selection, publishes the exact registry, and contributes the typed viewport-input hook. | Global viewport owner with registry state bound to exactly one `WorldHandle`; reset clears slots/poses/transitions/seed even for equal handle bits, active change rebinds empty, destruction/shutdown invalidates, and away/back never resurrects. Reference-scene entity creation, owning-world retention, and optional initial seed handoff belong to app initial-world bootstrap. |
| Editor UI | ImGui adapter/overlay/host, window contribution state, and production of the single capture snapshot | Global and optional. `EditorUiModule` owns UI lifetime, publishes the Engine-free host, and writes the frame-loop-owned capture value through the borrowed `UiEndCapture` hook context after the paired begin/build/end bracket; it does not own camera, selection, scene, config, asset, or method state. |
| Config control | `EngineConfigControl` and the app-section registry used by boot and live apply | Global owner. The app supplies section codecs before boot; preview remains side-effect-free and apply uses the existing validated commit path. |
| App session/lifecycle | The app root explicitly composes modules; Sandbox owns `SandboxSession` for editor/default-policy/reference-content state rather than an Engine-owned application callback object. | Global app-owned session bound to the current Engine boot. Initialization borrows exact config/world/service capabilities after kernel boot; two-stage shutdown quiesces the runtime first, tears app state down while those borrows remain live, then reverses modules/subsystems. No state is cached across a new Engine boot. |

Clustering remains the existing app-optional composition proof. Its owner is
global, while job cancellation, commit targets, and label results are scoped
by `WorldHandle`. A future clustering integration is grouped or split only by
ADR-0026.

No WorldSwitch child is seeded until its trigger occurs. Render extraction and
the render-world pool stay kernel/frame-loop substrate. Initial reference
content is app bootstrap, not another runtime owner.

### 5. Measurable convergence target

`ARCH-014` remains the umbrella, but its scorecard measures outcomes rather
than wrapper counts:

- `Runtime.Engine.cppm` contains only the exact imports required by its final
  public surface and no unused plain imports. The present candidate is
  `Core.Config.Engine`, `Core.FrameGraph`, `RHI.Device`, `Platform.Window`,
  `Graphics.Renderer`, `Runtime.CommandBus`, `Runtime.KernelEvents`,
  `Runtime.JobService`, `Runtime.Module`, `Runtime.ServiceRegistry`,
  `Runtime.WorldHandle`, and `Runtime.WorldRegistry`. This named set of 12 is
  evidence to verify and shrink as the API lands, not an “at most 12” budget
  that permits unrelated imports.
- Domain imports are zero by the exact substrate-allowlist complement.
  `Extrinsic.Runtime.WorldHandle` is added to the substrate set.
- Domain re-exports are zero. The two current re-exports remain separately
  exact-ratcheted and may stay only if their types are classified as kernel
  substrate.
- Domain-facade getters are zero. A domain-facade getter is any public
  `Engine::GetX()` whose returned reference, pointer, view, or value belongs to
  a responsibility outside the accepted kernel substrate. The final checker
  maintains an exact allowlist of permitted kernel getter names and owning
  types; the measured domain set is the complement. Test-only suffixes,
  aliases, prefixes, and forwarding return types do not exempt a getter.
- No `Engine&` occurs in a handler, setup, or composed-responsibility surface.
- `IApplication` and its simulation/variable-frame callbacks were removed by
  `RUNTIME-184`; application
  behavior reaches the frame through behavior-carrying composition, not an
  unrestricted Engine callback.
- No direct EnTT dispatcher use occurs in composed responsibility code.
- Every durable responsibility row records global versus `WorldHandle` scope
  and has contract coverage for its stated switch/destruction behavior.
- D10 is complete when the closed recipe-data invariant is validated; it does
  not wait for an extension registry.
- D11 is complete when the single-producer capture-snapshot contract is
  validated; it does not wait for a priority chain.
- The experiment lane is conditional and does not block convergence while no
  production experiment consumer exists.
- `IRuntimeModule`, `EngineSetup`, `ServiceRegistry`, or
  `RuntimeModuleSchedule` counts are never scorecard outcomes. Only live
  behavior, ownership, interface coupling, and validated lifecycle count.

The exact ratchet continues to fail on stale snapshots as well as growth.
Every reduction updates the snapshot in the same change, so the budget never
becomes room for later regrowth.

After `RUNTIME-180`, the historical exact intermediate snapshot was
35 plain imports, 13 domain imports, two re-exports, and 25 public getter
names. `RUNTIME-172` reduces the current exact snapshot to 33 plain imports,
11 domain imports, two re-exports, and 22 public getter names. `RUNTIME-188`
then reduces the snapshot to 26 plain imports, 4 domain imports,
2 re-exports, and 15 public getter names. `RUNTIME-183` reduces the current
snapshot to 22 plain imports, zero domain imports, 2 re-exports, and 10 public
getter names. These remain migration evidence, not the final allowed-surface
budget.

## Consequences

- Positive: `ARCH-014` can converge without manufacturing unused registries,
  filters, builders, or one-wrapper-per-noun classes.
- Positive: the domain-free Engine, explicit app composition, state-scope,
  and no-`Engine&` outcomes remain stricter than a file- or class-count proxy.
- Positive: `REVIEW-003` audits the accepted result instead of owning a
  circular prerequisite decision.
- Positive: the existing typed composition seam remains available where its
  deletion would recreate domain Engine glue, while its unused methods receive
  no blanket protection.
- Trade-off: the six implementation hypotheses may split after their child
  inventories apply ADR-0026. Task links, not a permanent module taxonomy,
  carry that migration detail.
- Trade-off: the first real extension pass, competing capture producer, or
  scheduled responsibility must state a focused contract before generalized
  machinery returns.
- Trade-off: an experiment app initially writes explicit composition code.
  A convenience builder and template arrive only after they can remove
  demonstrated repetition.
- No engine/runtime behavior code, module surface, build graph, method,
  benchmark, or performance claim changes directly through this ADR. The
  structural convergence policy and its repository-snapshot fixture change
  only for the factual `WorldHandle` classifier correction.

## Alternatives Considered

- **Require one `IRuntimeModule` per scorecard noun — rejected.** Logical
  ownership and C++ wrapper count are different questions. Mechanical wrappers
  can leave state and facades inside Engine while appearing complete.
- **Delete every composition seam because ADR intake had one production module
  — rejected.** Deleting the only type-erased app-to-runtime seam recreates the
  same lifetime callbacks, adds concrete domain knowledge to Engine, or leaks
  `Engine&`. The correct action is to narrow it and remove unproven surface.
- **Keep the complete two-phase service and schedule framework unchanged —
  rejected.** At intake there was no production `Require`, non-no-op
  `OnResolve`, registered sim system, or registered frame hook; the later
  production audit retained only mechanisms subsequently earned by real
  consumers.
- **Move domain owners behind private Engine classes — rejected.** This
  reduces file size but not ownership or coupling and preserves domain getters.
- **Predeclare the six hypotheses as permanent module families — rejected.**
  ADR-0026 requires present lifecycle, state, commit, and consumer cohesion;
  the rows are migration hypotheses only.
- **Freeze an open extension graph, priority capture chain, or experiment
  builder now — rejected.** Each has zero or one producer and no demonstrated
  arbitration or extension requirement.
- **Treat output shape or a domain noun as the composition boundary —
  rejected.** Names and result shapes are audit clues, not ownership evidence.

## Validation

This ADR is an architecture and structural-policy slice. It is validated by:

1. the live-source counts recorded in Context and a source search confirming
   one production `IRuntimeModule`, zero production schedule records, zero
   production `Require`, two production `Find` types, one capture producer,
   zero extension registrations, and zero `InlineModule` consumers;
2. strict documentation links, documentation sync, task policy, task-state
   links, and generated session-brief freshness;
3. the strict kernel-convergence checker remaining exact and green with
   `WorldHandle` classified as substrate and the corrected 42/20 snapshot; and
4. architecture review confirming that this decision adds no dependency edge,
   owner, interface, backend, control path, or compatibility exception.

Implementation children must additionally prove:

- focused contract/integration behavior for every extracted responsibility;
- state reset, rebind, cancellation, and teardown behavior at the declared
  world/global scope;
- app composition without a concrete domain import in Engine or an `Engine&`
  responsibility surface;
- removal or behavior-backed retention of each conditional mechanism in the
  verdict table; and
- the final exact Engine-surface ratchet, strict layering, canonical build,
  and default CPU-supported correctness gate.
