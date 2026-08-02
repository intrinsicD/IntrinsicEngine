# Runtime

`src/runtime` is the composition root for the engine. It owns
subsystem instantiation order, frame-phase orchestration, and deterministic
startup/shutdown.

## Public module surface

The retired Sandbox facade export ledger and current owner map are recorded in
[Sandbox editor feature boundaries](../../docs/architecture/sandbox-editor-feature-boundaries.md).

| Module | Responsibility |
|---|---|
| `Extrinsic.Runtime.CommandBus` | Domain-free kernel command bus from ADR-0024 D5. Commands are plain-data payloads with correlation ids, thread-safe enqueue from any phase/thread, and exactly one Engine-owned pre-simulation drain point. Missing handlers and failed handlers are reported loudly; handlers receive `CommandContext` narrow capabilities rather than `Engine&`. |
| `Extrinsic.Runtime.KernelEvents` | Domain-free queued kernel event bus from ADR-0024 D7. Publish is always deferred and worker-safe, listeners run only during Engine-owned main-thread pumps, and listener-published cascades are deferred to the next pump. |
| `Extrinsic.Runtime.ServiceRegistry` | Two-phase runtime-module service registry from ADR-0024 D3/D13. Synchronous infrastructure is provided during module registration, required/optionally found during resolution, and boot fails closed with diagnostics naming the requester and missing service. `Withdraw<T>(expected)` is an owner-only, exact-instance lifetime operation: it is phase-independent for registration rollback and locked shutdown, and a missing or mismatched rollback entry returns an error without recording a boot error. Providers withdraw borrowed instances before destroying them so modules that stop later cannot discover stale pointers. |
| `Extrinsic.Runtime.Module` | Runtime module composition contract from ADR-0024 D1/D3/D12/D13, pruned by `RUNTIME-185`. Exports `IRuntimeModule`, `EngineSetup`, five generic frame phases, the narrow `RuntimeViewportInputHookContext`/callback, and shutdown announce context; modules receive command/event/job/world/service plus startup recipe/state capabilities and never an `Engine&`. Frame and viewport registrars are registration-phase-only. The viewport-input context borrows the completed editor-capture snapshot, active world, platform input, viewport, config, frame delta, and mutable `RenderFrameInput` only at the stable pre-gizmo insertion point; it does not widen `RuntimeFrameHookContext` or add another generic phase. There are ten production implementors (nine runtime-owned plus Sandbox's optional frame-pacing capture), seven generic hook registrations, two viewport hooks, and no module sim-system seam. Engine privately retains and sorts those hook records by phase/module/registration sequence for frame hooks and module/registration sequence for viewport hooks; no standalone schedule BMI or service exists. |
| `Extrinsic.Runtime.ClusteringModule` | Sole typed runtime owner for K-Means (`ARCH-012`, `RUNTIME-196`). It exports one `RunKMeans` request, typed `KMeansRunCompleted`/`ClusterLabelsChanged` events, `ClusteringService`, and `ClusteringModule`. The request carries canonical input/output `GeometryPropertyRef` identities, parameters, selected entity, and CPU-reference or Vulkan-compute backend data. The module snapshots active-world mesh/graph/point-cloud positions plus the exact optional label/color/scalar outputs, routes CPU work through world-scoped `JobService`, and owns one private Vulkan state plus one `JobService` GPU participant using shared `Graphics::GpuTransfer` readback. Both paths rejoin one main-thread stale/cancellation gate and atomically commit the output cohort. When the optional document owner is composed, `RUNTIME-201` routes that commit through its generation-validated `EditorCommandHistory` transaction; standalone clustering remains operational without undo. Both paths publish the same completion/change events and report requested/actual backend plus fallback diagnostics truthfully. UI, config, agent/CLI, smoke, and benchmark callers use this service; backend recorder/cache/readback details are a non-exported module partition. `Extrinsic.Runtime.Engine` does not import or name clustering. |
| `Extrinsic.Runtime.WorldHandle` | Opaque runtime world identity shared by kernel seams. `DefaultWorldHandle` is reserved as the boot-world identity so frame-0 work and compatibility jobs are always scoped. |
| `Extrinsic.Runtime.JobService` | The one domain-free snapshot-in/result-out runtime work service from ADR-0024 D8 and `RUNTIME-194`. CPU jobs submit immutable work to the shared `Core::Tasks` scheduler with dependencies, priority/kind/cost metadata, `WorldHandle` scope, cooperative cancellation, progress, optional readiness parking, and fail-closed `ValidateBeforeApply`. Only the Engine-owned pre-pump-B completion gate applies results, with an eight-result per-frame budget; `SnapshotAll()` supplies generic observation without feature identity leaking into the service. Unpublished terminal work reconciles exactly once through `FinalizeUnpublishedOnMainThread`. The Engine kernel owns the service object, while app-composed `AsyncWorkModule` publishes that exact borrowed object and owns survivor cancellation/withdrawal at module shutdown. The service also owns the `GpuQueue` participant registry used by render-thread GPU work: participants record commands inside the renderer's open frame command context, drain transfer/readback completions during Maintenance, and release resources only after Engine detaches its private renderer hook token and performs the required device-idle coordination. |
| `Extrinsic.Runtime.WorldRegistry` | Runtime-kernel world lifetime mechanism from ADR-0024 D2/D4/D7. Owns `ECS::Scene::Registry` instances behind `WorldHandle`s, creates the boot world, tracks one active world, defers active-world changes and destroys to Maintenance, publishes `WorldWillBeDestroyed` / `ActiveWorldChanged`, and cancels world-scoped jobs before two-phase teardown. Destruction takes precedence over activation: destroy-pending/announced worlds reject activation, and queued activation is revalidated as `Live` at Maintenance. It does not own preview/readiness/switch UX policy. |
| `Extrinsic.Runtime.EngineConfigBoot` | Free-standing boot-time config helper from `RUNTIME-146`, extended by `CORE-009`. Exports `CreateReferenceEngineConfig()`, registry-aware overloads, `EngineConfigBoot*` records, and `ResolveEngineConfigForBoot(...)`, preserving sandbox startup precedence (`--engine-config` → `INTRINSIC_ENGINE_CONFIG` → `config/engine.json` → reference defaults) without importing the full `Engine` interface. |
| `Extrinsic.Runtime.EngineConfigControl` | App-composed live config-control module from `RUNTIME-181`, building on the `RUNTIME-149`/`CORE-009` facade. The `final IRuntimeModule` owns the application-section registry, exposes it before boot, and publishes/withdraws its exact instance through `ServiceRegistry`. It exports render-recipe preview/load/activate/apply/clear APIs and state, engine-config hot-subset preview/load/apply APIs and state, deterministic changed-section reporting, and the DTOs used by editor/agent callers. During registration it copies the Engine's already-applied startup recipe state, retargets the narrow borrowed activation capability to its persistent state, and fully binds before publication. It mutates only the borrowed Engine-owned `EngineConfig`, rejects boot-only differences, synchronously commits the default recipe path, default-off GPU-profiling bit, and registered application sections, dispatches section callbacks only after commit, and clears all live bindings on shutdown so stale references fail closed. |
| `Extrinsic.Runtime.RenderRecipeActivation` | Plain shared recipe-activation kernel and free functions from `RUNTIME-181`. The capability contains only a borrowed active config/state plus framebuffer-extent and frame-recipe-override callbacks; it owns no window or renderer. `Engine::Initialize()` uses it for unconditional reset and optional boot-file activation even when live control is omitted, while `EngineConfigControl` uses the same preview/load/apply/clear functions for synchronous UI and agent control. |
| `Extrinsic.Runtime.ClusteringConfig` | Clustering-owned schema, canonical JSON codec/validator, lookup/update helper, and registration factory for `sandbox.clustering`. `ClusteringConfig` maps exactly to the canonical `RunKMeans` request, so config files, agent/CLI hot apply, and UI use one validated shape. |
| `Extrinsic.Runtime.ProgressivePoissonConfig` | Progressive-Poisson-owned schema, codec/validator, lookup/update helper, and registration factory for `sandbox.progressive_poisson`. |
| `Extrinsic.Runtime.ParameterizationConfig` | Parameterization-owned schema, codec/validator, lookup/update helper, and registration factory for `sandbox.parameterization`. Core sees only generic section records; `app/Sandbox` owns registration/default aggregation for all three feature modules before boot. |
| `Extrinsic.Runtime.InputActions` | Runtime-owned input-action registry from `RUNTIME-155`, finalized by `RUNTIME-186`. Exports the action binding/handle/context/service/descriptor API plus `RuntimeInputActionRegistry`, which owns handle allocation, registration/unregistration state, key-edge trigger checks, ImGui keyboard-capture suppression, callback failure logging, and per-frame dispatch after the pre-render transform flush. Its generic service aggregate carries config, scene, and mutable render input; camera- and selection-aware actions capture their exact optional services when app policy registers them. Engine publishes the registry as a built-in service and dispatches it, but no longer re-exports its records or forwards registration/unregistration. App owners import this module and retain their exact registry handles directly. |
| `Extrinsic.Runtime.AsyncWorkModule` | App-composed `JobService` lifecycle owner finalized by `RUNTIME-194`. Registration publishes the Engine kernel's exact borrowed `JobService` through `ServiceRegistry`; it creates no worker pool, scheduler, registry, frame hook, or feature facade. Shutdown first withdraws that exact service so later modules cannot discover a stale publication, then requests cancellation for every surviving job; Engine owns scheduler quiescence after reverse module teardown. World-scoped cancellation remains in `WorldRegistry`. Sandbox and tests that need app-side job discovery compose the module explicitly; omitting it leaves the kernel service and bounded Engine drain operational but publishes no editor/module lookup surface. `Runtime.Engine` never imports or names the concrete module. |
| `Extrinsic.Runtime.Engine` | Domain-free composition root and frame-loop owner. Its interface owns no application callback object and no asset/import/residency/bake, scene-document/history, scene-interaction, camera, async, config-control, editor-UI, reference-scene, selection, lookup, readback, gizmo, or mesh primitive-view capability. Optional owners publish exact services and contribute typed hooks; Engine retains their records only inside `Engine::Impl`, clears them on every boot, sorts deterministically, and dispatches them at the established frame/viewport phases. Engine also directly composes the three ECS-owned transform/bounds/render-sync systems for fixed-step and gated pre-render execution, and owns the renderer hook token that delegates GPU participant recording to `JobService` before detaching it ahead of participant shutdown. None of those one-owner roles has a standalone public helper module. Before module registration Engine publishes the exact live `RHI::IDevice`, `Platform::IWindow`, `Graphics::IRenderer`, `RenderExtractionCache`, and `RuntimeInputActionRegistry` built-ins; `AsyncWorkModule` separately publishes the kernel-owned `JobService`. Reinitialization republishes fresh borrowed instances. `BeginShutdown()` publishes and pumps the announcement after command discard, detaches the GPU command hook, drains GPU participants, stops the loop, and waits for device idle while worlds/services remain live so concrete app state can detach. `Shutdown()` invokes that boundary when needed, then performs ordinary reverse module and subsystem teardown. Render-extraction cache/pool/frame-index ownership remains in the Engine-private `RenderExtractionService`; callers read extraction or visualization state from the published cache rather than Engine forwarders. `RUNTIME-187` puts every private field behind `Engine::Impl`. The exact final surface is `12/0/0/5`: twelve declaration-required plain imports, zero domain imports, zero re-exports, and only `GetDevice`, `GetEngineConfig`, `GetLastFramePacingDiagnostics`, `GetRenderer`, and `GetWindow`, each ratcheted with its return/owning type and owning import. |
| `Extrinsic.Runtime.FramePacingDiagnostics` | Runtime-owned frame-pacing diagnostics module from `RUNTIME-158`. Exports `RuntimeFramePacingDiagnostics` plus the pure helper that copies renderer `RenderGraphFrameStats` compile/execute timings into the current frame sample. `EditorUiModule` privately copies ImGui adapter timings/counters into the borrowed sample, so this generic module no longer imports the adapter. `Engine::RunFrame()` writes phase-boundary timings and publishes the last sample through `Engine::GetLastFramePacingDiagnostics()`. |
| `Extrinsic.Runtime.SceneDocumentModule` | Optional app-composed document owner from `RUNTIME-172`. The concrete `final IRuntimeModule` publishes itself and its exact owned `EditorCommandHistory`, binds document path/event/sequence/history to one validated active `{WorldHandle, Scene::Registry*, binding epoch}`, and resets that complete durable state instead of retaining a per-world map. It preserves synchronous save/load/new/close, and queues snapshot saves and temporary-registry loads on the kernel-owned `JobService` (`RUNTIME-194` Slice B3; the optional `StreamingExecutor` dependency is retired). Every completion captures weak operation state plus module generation, binding epoch, world, and registry identity. That captured binding is re-checked through `JobDesc::ValidateBeforeApply` immediately before the drain would commit, so a stale result is discarded rather than applied; a queued operation that terminates without publishing — cancelled, discarded as stale, or dropped — reconciles through `JobDesc::FinalizeUnpublishedOnMainThread`, which publishes the terminal failure event exactly once when the captured binding still holds. New/load/close snapshot strong-handle participants in deterministic name/registration order, run every `BeforeReplace` callback while the outgoing registry is live, replace, run every `AfterReplace` callback against the rebound registry, then reset history. Parse failure invokes no participant and mutates no document state. Shutdown invalidates generation/epoch and cancels owned tasks before reverse module teardown; omission leaves Engine and the active world operational while document/history services are unavailable. |
| `Extrinsic.Runtime.SceneInteractionModule` | Optional app-composed one-world interaction owner from `RUNTIME-188`, with its one-consumer frame helpers internalized by `RUNTIME-205`. Its PImpl directly owns `SelectionController`, `StableEntityLookup` plus its scene binding, `GizmoInteraction`, packet/scratch storage, bounded pick-correlation records, and the refined-primitive cache; it publishes only the exact module and exact controller. Every input, `BeforeExtraction`, and `Maintenance` hook validates `{WorldHandle, Registry*, interaction epoch}` directly. The typed viewport hook runs after camera/capture, borrows the exact document `EditorCommandHistory` when composed, and disables transform dragging when that history is unavailable. `BeforeExtraction` drains one pick and submits a copied world-tagged selection/hover/gizmo snapshot after input actions and transform flush, and `Maintenance` drains completed readbacks. A strong scene-document participant clears the complete cohort while the outgoing registry is live and rebuilds lookup on New/Load/Close; active-world mismatch, retirement, announcement, and recycled-handle reinitialize use the same reset. Pick sequences remain monotonic while zero, unknown, wrong-world, and wrong-epoch results fail closed. Announcement unregisters the document participant and detaches borrows before ordinary exact withdrawal. Omission leaves document, camera, generic input, component-driven primitive views, rendering, and Engine operational with empty interaction snapshots. |
| `Extrinsic.Runtime.AssetWorkflowModule` | Optional app-composed global asset and import owner (`RUNTIME-183`, consolidated by `RUNTIME-200`). It is the sole published import service and exports one `AssetImportRecipe` plus typed copied results for the ordered route → decode → CPU materialize → ECS author → postprocess → GPU residency → complete stages. Its PImpl keeps the private executor alive across Engine reinitialize; each boot recreates `AssetService`, `GpuAssetCache`, the cache listener, and private geometry/model/texture decode, materialization, and residency helpers. It publishes exactly `AssetService`, `AssetWorkflowModule`, `GpuAssetCache`, and `Core::IAssetFrameHooks`. Queued work runs through world-scoped `JobService`; only the bounded main-thread gate may publish assets, author ECS, bind residency, select/focus, or dirty document history, after validating the copied `{WorldHandle, Registry*, binding generation, cancellation generation}` identity. Direct imports emit the same seven-stage trace. When `TextureBakeModule` is composed, the workflow borrows its canonical `TextureBakeService` and reconciles ready `PropertyTextureBakeOutputs` into `GeometryPresentationRuntimeState` plus one atomic material snapshot per entity, preserving unrelated channels. Resolution requires the exact `SceneDocumentModule` and `EditorCommandHistory`, with optional selection and camera services, and registers one strong document-replacement participant. Shutdown announcement cancels imports and detaches provider borrows before application/provider teardown. Omission leaves generic Engine/render/world/transfer/async and render-extraction geometry retirement operational, with asset services absent and platform drops ignored. |
| `Extrinsic.Runtime.TextureBakeModule` | Optional app-composed GPU property-texture owner (`RUNTIME-190`, unified by `RUNTIME-191`). It exports one canonical `PropertyTextureBakeRequest`, result/status, representation, output-record vocabulary, and exact `TextureBakeService`; the request describes only world/entity identity, canonical source and texcoord `GeometryPropertyRef`s, expected generations, storage/encoding/range, extent/padding, and stable output identity. Material, visualization, presentation, normal-space, and consumer meaning are absent. Requests require existing finite, count-matched UV and typed mesh vertex, face, or edge properties; edge values use the nearest triangle-edge rule. Raw scalar/vector storage and explicit RGBA, label, scalar-colormap, and normal encodings all enter one `Graphics.PropertyTextureBake` recorder and one bounded JobService GPU participant with canonical byte identity, live-residency revalidation, cache generations, ready-frame publication, dilation, stale rejection, and frame-safe retirement. A record is identified by entity plus output name: rebaking reloads the same generated asset, renaming preserves the old output, and removal destroys the owned asset. Callers own source preparation and completed-output material/presentation processing. Document replacement and shutdown synchronously detach or destroy outgoing ownership. Missing and non-operational GPUs fail closed with no CPU fallback or specialized normal path. |
| `Extrinsic.Runtime.AssetIngestStateMachine` | Runtime-owned ingest request/result state machine (`RUNTIME-101`). Exports request sources for manual import, dropped files, and reimport; phases from `Queued` through route resolution, decode scheduling/execution, main-thread apply, `Complete`, `Failed`, and `Cancelled`; and a diagnostic taxonomy for missing path/file, route failures, invalid reimport target, duplicate active request, decode failure, materialization failure, cancellation, stale completion, invalid transition, and unknown handles. The state machine is backend-neutral and owns no decoders, ECS mutation, `AssetService`, graphics, RHI, or worker threads. `AssetWorkflowModule::ImportAssetFromPath(...)`, `QueueAssetImport(...)`, `QueueGeometryImport(...)`, `QueueModelTextureImport(...)`, `ReimportAsset(...)`, and dropped imports submit records through this contract; deferred file reads and decodes run as `Runtime.JobService` jobs and complete/fail only from its main-thread completion drain, with unpublished termination reconciling the visible queue record through `JobDesc::FinalizeUnpublishedOnMainThread`. Direct and queued reimport preserve the requested existing asset identity, reload the same `AssetId` transactionally, and let the workflow's private residency/materialization state consume `Reloaded`/`Ready` events without reviving ECS `AssetSourceRef` coupling; standalone geometry scene entities remain authoring snapshots and are not duplicated. |
| `Extrinsic.Runtime.GeometryPresentation` | Sole neutral geometry-presentation contract after `RUNTIME-193`. `GeometryPresentationRecipe` contains authored shape/lane/presentation/slot choices, stable asset ids, canonical `GeometryPropertyRef` identities, uniform defaults, generated-output names, texture colormap/normal-space interpretation, and generated-output policy. `GeometryPresentationRuntimeState` separately carries runtime-only readiness, generated assets, diagnostics, and exact recipe/source/output generations. The free `BuildGeometryPresentationSnapshot(...)` projection returns copied effective state with explicit uniform/previous-output fallback and no ECS entity, borrowed property view, job token, graphics/RHI handle, or live service pointer. Scene documents persist only the recipe, accept the retired `progressiveRenderData` wire key on read, and initialize a fresh runtime sidecar. Render extraction, asset/model handoff, caller-owned texture-bake reconciliation, and Sandbox models/commands all use this one recipe/state/snapshot path for mesh, graph, point-cloud, composition, and procedural geometry. |
| `Extrinsic.Runtime.RenderArtifactPublication` | Runtime-owned render artifact publication contract (`RUNTIME-127`). Exports an artifact registry keyed by renderer id, snapshot id, view/output recipe id, source revisions, and output purpose; lifecycle kinds for transient frames, cached frames, saved files, preview-only outputs, dataset/batch outputs, readback/metric outputs, and candidate project results; UI-facing states for unpublished, stale, canceled, failed, superseded, published, and applied artifacts; explicit provenance-carrying publish/apply/undo commands; and an audit log. Registration never mutates project data. Applying a candidate artifact authorizes a project mutation for the caller-owned command path and records undo/audit metadata, but the registry itself does not import UI, renderer backends, ECS mutation callbacks, or project persistence. |
| `Extrinsic.Runtime.GeometryAvailability` | Runtime-owned geometry availability resolver (`RUNTIME-117`). Exports CPU source/provenance queries, property-domain support, element counts, and `Surface`/`Edges`/`Points` render-lane readiness from ECS `GeometrySources` plus promoted `RenderSurface`, `RenderEdges`, and `RenderPoints` components. Runtime extraction, progressive property resolution, and focused editor-operation preflight consume this resolver so mesh vertices, graph nodes, and point-cloud points can satisfy point-lane consumers without using exact `GeometrySources::ActiveDomain()` as the common capability gate. It is additionally the single owner of the canonical geometry-property vocabulary (`RUNTIME-192`): `GeometryPropertyRef` (element domain + name + value kind and nothing else, so it is safe inside a desired-state authoring recipe), the pointer-free `GeometryPropertyCatalogSnapshot` (deterministically ordered by domain then name, carrying source identity and generations so callers revalidate by comparing generations rather than dereferencing), `GeometryPropertyValueKindFilter` (`std::optional<Geometry::PropertyValueKind>`, where `std::nullopt` means unconstrained), and the shared name/domain/value-kind/count/finite-value resolution queries. Every runtime feature that names a geometry property -- bake, presentation, visualization, selected analysis, vertex-channel binding -- resolves through this module. |
| `Extrinsic.Runtime.SceneSerialization` | Backend-neutral scene document seam (`RUNTIME-098`, hardened by `RUNTIME-100`, `RUNTIME-193`, and `HARDEN-087`). Exports JSON save/load helpers over `ECS::Scene::Registry` plus `Core::IO::IIOBackend`, result/stat records, and fail-closed diagnostics. Document version 2 persists metadata names, durable stable ids, local transforms, hierarchy parent links, selectable tags, render geometry hints, visualization configs and lane overrides, authored `GeometryPresentationRecipe` values, and mesh/graph/point-cloud `GeometrySources` property data for sandbox-authored entities, including graph `h:connectivity` halfedges and mesh-domain `v:position`, `v:normal`, and `v:texcoord` where present. Version 1 is rejected rather than converted by synthesizing graph topology. It accepts the legacy `progressiveRenderData` key on read but always writes `geometryPresentation`. Unsupported persistence families are counted deterministically in `SceneSerializationStats` (`Unsupported*Entities`) instead of being silently treated as supported. It deliberately omits `GeometryPresentationRuntimeState`, renderer/RHI caches, GPU handles, dirty-tracker UX, file dialogs, transient job/readiness/diagnostic/generated-output observations, borrowed property views, arbitrary legacy asset source reimport, transient per-entity visualization recipes, and arbitrary component persistence. |
| `Extrinsic.Runtime.EditorCommandHistory` | Runtime/editor-owned undo/redo and document dirty-state seam (`RUNTIME-102`, unified by retired `RUNTIME-201`). Exports `EditorCommandHistory`, deterministic result/status/snapshot DTOs, generic command records, the retained single-selection compatibility adapter, compound commands with rollback, and a hierarchy delete/orphan planning helper. Undoable entity edits keep typed state capture/apply policy with their transform, visualization/presentation, render-hint, geometry, method, or gizmo owner and enter history through the runtime-internal generation-validated mutation transaction; the retired public transform/visualization/primitive-view adapter DTOs no longer make this module import their component types. Delete planning consumes the guarded ECS descendant-preorder query; hierarchy corruption returns `CommandFailed` with empty delete/orphan lists before any command or entity mutation can be published. The history stores labels, capacity-bounds undo/redo stacks, active scene path, revision/saved-revision dirty tracking, and fail-closed stale/missing dependency statuses. ECS remains data-authoritative; the service lives in runtime because editor command policy, sidecars, dirty-state UX, and recursive hierarchy policy are above ECS. |
| `Extrinsic.Runtime.EditorWindowRegistry` | Generic editor-window contribution contract from `UI-034`. Contributors register a stable id, display title, structured menu path, draw callback, initial open state, and optional open-state observer. Duplicate/invalid registrations fail closed; handles support unregister and visibility changes; callbacks may unregister themselves during dispatch. `DrawOpenWindows()` invokes only open windows and invokes none while global visibility is disabled. The data-only `EditorUiVisibilityCommand` (`Toggle`/`Show`/`Hide`) preserves each window's open state across global hide/show. |
| `Extrinsic.Runtime.EditorPropertyWidgets` | Generic property-inspection model and draw wrapper from `UI-034`. `BuildEditorScalarPropertyPlotModel(...)` enumerates numeric scalar properties from a `Geometry::ConstPropertySet`, excludes vector properties, selects deterministically, copies finite values into a data-only plot model, and reports filtered non-finite samples plus the finite range. `DrawEditorScalarPropertyPlotWidget(...)` renders the selector, bin control, and histogram while keeping ImGui/ImPlot types private to the implementation unit. ImPlot 1.0 is manifest-managed and linked **PRIVATE** to runtime; its context is created, rebuilt, and destroyed with the existing ImGui adapter context. |
| `Extrinsic.Runtime.EditorUiHost` | Engine-free editor capability published by `EditorUiModule`. It owns the existing `EditorWindowRegistry`, global visibility state, copied adapter diagnostics, and mutation-safe parameterless frame contributions; it stores no `Engine&`, adapter, overlay, or capture reference. Consumers register/unregister windows and contributions and may issue visibility commands. A move-only owner control is claimed exactly once before publication and is retained only by the module, so service consumers cannot invoke contributions out of bracket or forge operational/diagnostic state. |
| `Extrinsic.Runtime.EditorUiModule` | Optional app-composed PImpl owner from `RUNTIME-182`. Fresh registration claims the host owner control, publishes the exact `EditorUiHost`, and registers `UiBegin`, `UiBuild`, and `UiEndCapture` hooks. Resolution requires only the exact live `Platform::IWindow`, `Graphics::IRenderer`, and `RuntimeInputActionRegistry` built-ins, then initializes one adapter/overlay and the unsuppressed global `G` visibility action. `UiBegin` opens the ImGui frame, `UiBuild` invokes host contributions and any other deterministically ordered module hooks, and `UiEndCapture` closes the adapter before copying capture plus ImGui pacing/diagnostics. Reverse shutdown unregisters the action, detaches the overlay while renderer/window remain live, withdraws the host, and destroys all boot state. Omission is fail-closed and leaves capture/pacing unclaimed. |
| `Extrinsic.Runtime.PhysicsBridge` | Runtime-owned ECS-to-physics bridge added by `PHYSICS-001`. Exports `PhysicsBridgeFixedStepConfig`, `PhysicsBridgeDiagnostics`, and `PhysicsBridge`, which owns an `Extrinsic.Physics.World`, a `StableId -> BodyHandle` sidecar, descriptor synchronization from ECS collider/rigid-body authoring, fixed-step accumulator stepping, and dynamic-body transform writeback with `Transform::IsDirtyTag` / `Transform::WorldUpdatedTag` stamping. The bridge keeps handles out of ECS, skips static/kinematic writeback with diagnostics, and deliberately does not route contact events until `PHYSICS-002` exposes contact records. |
| `Extrinsic.Runtime.CameraControllers` | Runtime-owned camera controller behavior and exact published registry surface. Exports `CameraFocusTarget`, `ICameraController`, `OrbitCameraController`, `FlyCameraController`, `FreeLookCameraController`, `TopDownCameraController`, `CreateCameraController()`, `CameraControllerSlot`, and `CameraControllerRegistry`. `ICameraController::Focus(...)` performs one-shot centering/framing of imported or selected geometry without making UI own camera state. Controllers consume `Extrinsic.Platform.Input::Context`, use `Core::Extent2D` for viewport dimensions, and produce immutable `Graphics::CameraViewInput` for renderer extraction. `TopDownCameraController` seeds from the input view focus point, not the input position XZ, so the default reference triangle remains centered when starting in or switching to top-down mode. The registry is bound to exactly one valid `WorldHandle`: `ResetForWorld` always clears slots, poses, transitions, and seed even for equal handle bits; `SetWorldSeed` rejects invalid/unbound/wrong-world writes; an invalid reset is the shutdown state; and away/back never resurrects state. GRAPHICS-040A keeps the base `CameraViewInput` ABI stable; graphics-side temporal jitter is selected through `BuildTemporalCameraViewSnapshot(...)`, which accepts the rendered-frame index explicitly, while GRAPHICS-040C maps the renderer AA selector to TAA/external reconstruction without adding runtime camera authority. |
| `Extrinsic.Runtime.CameraModule` | Optional app-composed camera owner from `RUNTIME-180`. On registration it binds the active world, publishes the exact `CameraControllerRegistry`, subscribes to `ActiveWorldChanged` and `WorldWillBeDestroyed`, and contributes one typed viewport-input hook. The hook rechecks the active handle before reading config or seed, lazily creates the configured main controller, suppresses motion while editor capture owns viewport input, writes the immutable camera view, and consumes the one-shot transition. Shutdown unsubscribes, withdraws the exact borrowed registry, and resets it invalid. Omitting the module publishes no service and produces no fallback camera, while generic input actions, import selection, editor non-camera behavior, and app-owned reference content remain operational. |
| `Extrinsic.Runtime.MeshSurfaceTopology` | Truthful topology-only surface for canonical mesh fan triangulation and triangle-to-face mapping. It validates bounded halfedge rings, skips deleted face slots whose rings no longer claim them, and fails closed on malformed or mixed-owner topology. Extraction, selection refinement, UV diagnostics, and GPU acceptance use this one triangle order without exposing upload buffers or residency state. |
| `Extrinsic.Runtime.MeshPrimitiveView` | Data-only mesh edge/vertex-view settings and render-mode values retained for editor/session consumers. Upload-plan construction is private to `ExtrinsicRuntime`; no public primitive-view packer or lifecycle owner remains. |
| `Extrinsic.Runtime.PrimitiveSelectionRefinement` | Runtime-owned pure CPU refinement that validates a graphics `EncodedSelectionId` hint against authoritative mesh, graph, or point-cloud `GeometrySources`, maps it to a face/edge/vertex/point result, and can use a captured pick ray/depth context for the fail-closed CPU fallback. `SceneInteractionModule` directly owns the production correlation records and refined-result cache, captures world/epoch-qualified contexts, drains completed readbacks, and exposes the newest editor-facing refined result; graphics produces only the encoded hint and never owns the cache or live ECS interaction state. |
| `Extrinsic.Runtime.ReferenceScene` | Plain app-invoked reference-content seam (GRAPHICS-029A/B, simplified by `RUNTIME-180`). Exports only the data records `ReferenceSceneEntity` / `ReferenceScenePopulation` plus `BootstrapReferenceScene(selector, scene)` and `TeardownReferenceScene(scene, population) noexcept`. The triangle implementation is private: bootstrap creates one ordinary visible/selectable mesh-domain `ReferenceTriangle` with durable `StableId`, `RenderSurface`, white `VisualizationConfig`, and an optional camera seed. Sandbox owns the exactly-once initial-world policy and retains `{WorldHandle, population}` so teardown mutates only the original live world; a retired original world is a safe no-op. The content path does not require `CameraModule`. |
| `Extrinsic.Runtime.RenderExtraction` | Runtime-owned ECS-to-graphics extraction and snapshot handoff. Its exported class holds one opaque implementation object; persistent sidecars, scratch buffers, copied visualization recipe state, and the one graphics residency coordinator have exactly one definition in the non-exported `:Internal` implementation partition (`Runtime.RenderExtraction.Internal.cpp`), outside the primary module interface. Ordinary primary-module implementation units split base extraction/submission (`Runtime.RenderExtraction.cpp`), private typed plan construction and unified residency submission (`Runtime.RenderExtraction.Geometry.cpp`), and visualization recipe encoding (`Runtime.RenderExtraction.Recipes.cpp`) without adding a public subsystem seam. Extraction uses `Extrinsic.Runtime.GeometryAvailability` for `GeometrySources` lane eligibility, builds owning `Graphics::GeometryUploadPlan` values through private plan builders, and drives one `TickGeometryResidency` maintenance hook. The cache reuses its per-frame live-renderable-key scratch set across `ExtractAndSubmit()` calls before retiring missing sidecars, avoiding fresh steady-state set allocation while preserving the same renderer-visible output. `RenderExtractionCache::FindGpuRenderableAvailability(...)` exposes a read-only `GpuRenderableAvailabilityView` keyed by stable entity id, with independent surface, edge, and point lane residency plus canonical named-buffer facts; ECS remains CPU authoring state and stores no GPU handles or renderer sidecars. |
| `Extrinsic.Runtime.RenderWorldPool` | Runtime-owned multi-buffer slot-lifecycle pool for pipelined frames (`GRAPHICS-036A`, first implementation child of the retired `GRAPHICS-036` planning slice; the planning slice named it `GRAPHICS-036-Impl-A`). Exports `RenderWorldPoolDiagnostics` (the three `GRAPHICS-036` decision-7 counters: `PipelineStallCount`, `ExtractionSkipCount`, `LastConsumedFrameAge`) and the `RenderWorldPool` value type. Implements the producer/consumer slot state machine the planning slice calls "atomic swap primitives + reclamation queue": the producer (extraction) calls `AcquireBack(frameIndex)` for a free slot, writes the snapshot, and `PublishFront(slot)` (release store of a single `std::atomic` front index plus a monotonic publish-sequence bump); the consumer (renderer) calls `AcquireFront(frameIndex)` (acquire load, per-slot atomic refcount increment) and `ReleaseFront(slot)`. Buffer count defaults to 3 (triple-buffer with reclamation, decision 1), clamps to `[1, 4]`, and collapses to in-place synchronous reuse at 1. Reclamation (decision 4) returns a slot to the free list only once its refcount is zero and it is no longer the published front, drained at the start of each `AcquireBack`. Back-pressure (decision 5): producer-faster overwrites the still-unpublished back slot (`ExtractionSkipCount`); consumer-faster reuses the current front when no new publish-sequence is observed (`PipelineStallCount`), so a synchronous pool that re-publishes the same slot index every frame is never mistaken for a stall. When the producer outruns the consumer so far that every slot is a published front still held in flight (no free slot and no unpublished back), `AcquireBack` fails closed — it returns `kInvalidSlot` (still counting `ExtractionSkipCount`) so the extraction is skipped and the previous front stays current, rather than overwrite storage an in-flight frame still references. The module imports nothing from graphics/ECS/platform — it manages only slot indices and atomics, introducing no new dependency edge. `GRAPHICS-036D` extends the CPU contract to the pipelined integration path: the renderer retains per-slot snapshot storage keyed by the pool slot, and `RenderConfig::SynchronousExtraction = false` consumes `AcquirePreviousFront` to prove render-N-1 without stalls/skips while synchronous mode remains the default. `GRAPHICS-036B` surfaces the pool's three counters read-only on `RuntimeRenderExtractionStats` (`RenderWorldPipelineStallCount`, `RenderWorldExtractionSkipCount`, `RenderWorldFrameAgeFrames`) via the pure `MirrorRenderWorldPoolDiagnostics(pool, stats)` free function in `Extrinsic.Runtime.RenderExtraction`. |
| `Extrinsic.Runtime.SelectionController` | Runtime/editor selection authority (`RUNTIME-089`), published exactly by `SceneInteractionModule` in production. It coalesces hover/click requests, assigns monotonically increasing sequences, tracks bounded in-flight intent, applies Replace/Add/Toggle semantics, mirrors selected/hovered ECS tags, and maintains copied render-id buffers. Sequence-aware hit/no-hit overloads return false without mutation for unknown or evicted records; standalone no-sequence convenience calls retain their direct-drive behavior. Context-capacity eviction explicitly discards the matching controller record. The controller resolves render ids through the module-owned `StableEntityLookup`, while standalone use can retain the validated decode fallback. `ClearSceneState()` removes tags, pending/in-flight state, and world-bound snapshots without resetting the sequence counter. |
| `Extrinsic.Runtime.StableEntityLookup` | Runtime-owned scene-local lookup sidecar (`RUNTIME-092`, event-driven wiring from `RUNTIME-145`), owned in production by `SceneInteractionModule`. It maps durable ECS `StableId` values to live entities and separately decodes/validates transient render ids, with deterministic duplicate winners and stale/missing diagnostics. `StableEntityLookupSceneBinding` maintains construct/update/destroy hooks for the one bound registry. The interaction module disconnects and clears it before replacement or rebind, rebuilds it afterward, and exposes stable-id resolution plus read-only diagnostics without publishing the raw mutable binding. |
| `Extrinsic.Runtime.VisualizationRecipes` | Runtime-owned, data-driven translation from canonical geometry properties to data-only `Extrinsic.Graphics.VisualizationPackets`. Exports a closed `VisualizationRecipe` variant for scalar, color, label, vector-field, isoline, Htex-preview, and fragment-bake metadata; `EncodeVisualizationRecipe(...)` resolves `GeometryPropertyRef` values and returns an owning `VisualizationEncodingBatch` plus deterministic `VisualizationEncodingDiagnostics`. Missing BDAs emit copied property-buffer upload descriptors for common graphics residency. Encoding is side-effect free; `ScheduleVisualizationHtexRecreate(...)` is a separate typed `JobService` operation. `RenderExtractionCache` stores copied per-entity recipes, projects `VisualizationConfig` and ready `GeometryPresentationRecipe` property slots into the same encoder, scopes upload keys by stable entity id, and exposes recipe-prefixed extraction counters. No adapter object, registry, opaque key, borrowed property view, or material-source overloading remains. |
| `Extrinsic.Runtime.ImGuiAdapter` | Runtime-side Dear ImGui platform/renderer adapter (`RUNTIME-090`, `RUNTIME-159`, `UI-034`). It owns paired ImGui 1.92.8 and ImPlot 1.0 contexts, translates drained platform events, opens a frame through `BeginFrame()`, invokes the configured visible contribution through `BuildEditorFrame()`, and copies `ImDrawData` into `Graphics::ImGuiOverlaySystem` during `EndFrame()`. `EndFrame()` records the data-only `EditorInputCaptureSnapshot` defined by `Runtime.Module` from `WantCaptureKeyboard`, `WantCaptureMouse`, and active-widget state before rendering; `EditorUiModule` copies it into the frame-owned value after end. `SetEditorVisible(false)` clears stored capture immediately and suppresses contribution work while preserving adapter lifecycle. The adapter remains backend-agnostic and exposes diagnostics without exporting ImGui headers; `imgui_core_lib` and `implot::implot` are linked **PRIVATE** to runtime. ImGui dynamic texture requests remain disabled because the promoted renderer consumes the copied legacy CPU font atlas. |
| `Extrinsic.Runtime.EditorWorkspaceSnapshots` | Presentation-free workspace snapshot surface. Public `EditorWorkspaceAttachment` carries only the opaque attachment lifecycle; `PrepareEditorWorkspaceSnapshotFrame(...)` prepares copied `EditorWorkspaceSnapshot` data and snapshot queries, while the four feature operation modules prepare their own callback-scoped command/query handles. App-private `SandboxPreparedFrame` composes those five records and decides panel/window composition. Each handle carries the attachment epoch, reports unbound after detach, and fails closed before reaching copied service pointers; operation-specific callback diagnostics remain available. Feature mutation contexts receive only an epoch-guarded selected-model-cache invalidation callback, not the workspace cache object. Workspace model assembly and the bounded private attachment/job-result session compile separately; the private binding/context adapters do not implement feature operations or cross the runtime boundary. The module owns no Sandbox names, menus, widgets, or ImGui state. |
| `Extrinsic.Runtime.EditorJobProjection` | Read-only job identity, dependency, progress, and queue projections over the canonical `JobService`; submission identity remains with the editor workspace session. |
| `Extrinsic.Runtime.SceneEditingOperations` | Typed selection, import, scene-file, transform, camera, primitive-view, and document operations plus their copied scene snapshots. Validation and mutation stay in runtime owners. |
| `Extrinsic.Runtime.GeometryProcessingOperations` | Typed clustering, texture/UV, parameterization, Progressive Poisson, normals, denoise, curvature, remesh, subdivide, simplify, outlier-removal, and ICP registration operations/results. ICP trajectory collection is private to this operation path and calls `Geometry.Registration::AlignICP`; there is no standalone runtime registration wrapper. |
| `Extrinsic.Runtime.VisualizationEditingOperations` | Typed property, binding, geometry-presentation, spatial-debug, visualization-config, and visualization-recipe snapshots/operations. |
| `Extrinsic.Runtime.RenderRecipeEditingOperations` | Typed render-graph, recipe draft/apply, profiling, and artifact publication snapshots/operations. |
| `Extrinsic.Runtime.GizmoInteraction` | Runtime/editor transform-gizmo interaction (`RUNTIME-084`, history convergence in `RUNTIME-201`). It performs screen-space handle hit testing and axis-constrained translate/rotate/scale preview edits, stamps transform dirtiness, and coalesces every moved entity from one drag into one generation-validated `EditorCommandHistory` transaction. Undo/redo revalidates the exact expected batch before restoring it atomically; the retired `GizmoUndoStack` has no replacement stack. In production `SceneInteractionModule` directly owns the interaction plus its packet builder and reusable selected-entity scratch, and graphics receives only frozen copied `TransformGizmoRenderPacket` values in the interaction render snapshot. |

### Asset Import Apply Scheduling

Runtime import materialization may synchronously complete the specific
`AssetService` CPU load/reload it just issued, then flush only that asset's
event so model-scene and texture handoffs observe the result in the same
facade call. Import apply code reachable from `Engine::RunFrame()` must not use
`Core::Tasks::Scheduler::WaitForAll()` as a frame-path fence; that scheduler
wait is reserved for shutdown and explicit non-frame joins. Unrelated scheduler
work must be allowed to continue while the current import result is
materialized or reported.

Dropped geometry, model-scene, and texture imports now share the same
`JobService` shape: route and ingest records are created on the frame
thread, file read/decode runs on the worker lane, and only decoded CPU payloads
cross back into the bounded main-thread apply drain for `AssetService`, ECS,
handoff, selection, and document-history mutation. Sandbox editor geometry
commands use the published `AssetWorkflowModule::QueueGeometryImport(...)` while
model-scene and texture commands use `QueueModelTextureImport(...)`; every
supported File / Import command returns `Pending` with an ingest handle to the
ImGui callback and publishes completion through the existing runtime import
event plus queue snapshot. Sandbox editor
scene save commands use
the exact composed `SceneDocumentModule::QueueSceneSaveToPath(...)` service,
snapshot persisted ECS state on the frame thread, serialize/write that snapshot
on the worker lane, and mark the editor document saved only from the
main-thread completion callback. Sandbox editor scene open commands use
`SceneDocumentModule::QueueSceneLoadFromPath(...)`, parse into a temporary
registry on the worker lane, and publish completion through
`SceneDocumentModule::GetLastSceneFileEvent()` after the main-thread scene
replacement lifecycle succeeds or fails closed.
Both queued import and queued scene-document apply callbacks freeze the active
`{WorldHandle, Scene::Registry*}` pair plus its binding epoch at submission.
They validate that exact world, registry, and current binding epoch immediately
before mutation; an active-world switch while worker work is in flight reports
`InvalidState` and does not redirect the result through rebound scene or
handoff members. The epoch also rejects an away-and-back switch whose final
handle and scene address happen to equal the submitted pair.
Direct `AssetWorkflowModule::ImportAssetFromPath(...)` /
`AssetWorkflowModule::ReimportAsset(...)` calls and
`SceneDocumentModule::SaveSceneToPath(...)` / `LoadSceneFromPath(...)` calls
are still synchronous APIs outside the frame-driven UI/drop routes.
`AssetWorkflowModule::SetModelTextureImportIOBackendFactoryForTest(...)`
is a contract-test seam for injecting slow or fake model/texture IO backends
into queued imports; production queued imports fall back to
`Core::IO::FileIOBackend`. The queued-geometry before-decode test hook can hold
a worker deterministically while a real Null-window Sandbox session proves
frames continue and no asset, ECS, selection, focus, or history mutation occurs
before main-thread apply; production leaves the hook empty.
Geometry decode results are held as shared immutable mesh/graph/point-cloud
payloads until main-thread apply. `AssetService` publication still takes its
own payload copy, and graph/point-cloud materialization still makes the mutable
local copy required by `PopulateFromGraph` / `PopulateFromCloud`, but the
worker-to-apply handoff and reload lambdas no longer copy the whole decoded
payload.

Successful scene-changing import completion uses
`EditorCommandHistory::MarkDirty` as a document-lifecycle transition: it
advances document revision/dirty state but deliberately creates no undo entry.
Entity creation, automatic authoring, and post-import enrichment are one import
lifecycle rather than editor-authored mutations. Deferred direct-mesh
enrichment captures an exact signature over the active mesh domain and topology
markers, every vertex/edge/halfedge/face property descriptor and value, deleted
counts, and vertex-channel binding generation and property references. Its
world-scoped `JobService` completion applies only when the entity is still live,
the asset-workflow binding epoch still names the same active world and scene,
the entity-sidecar job token still matches, and that signature is unchanged.
Apply and unpublished finalization resolve the scene through `WorldRegistry`
at callback time instead of retaining a scene reference across worker
execution. World switches, document replacement, destroyed worlds, recycled
entities, and signature mismatches therefore terminate without targeting
retired storage or writing ECS, history, or selection state. The selected-entity
processing model exposes the sidecar's pending or terminal status and nonempty
reason, and pending enrichment removes all geometry-mutating actions until the
job resolves. The staged workflow preserves this validation, lifetime, and
readiness contract.
Before geometry payload population, the workflow applies the recipe's
`ImportAuthoringRecipe`; the default recipe attaches the current selectable,
render-lane, and visualization authoring state. The named postprocess stage
prepares direct-mesh normals and UVs and routes generated property textures only
through `TextureBakeService`. Completion applies the recipe's selection and
focus choices exactly once after all created entities and their aggregate bounds
are known. Optional `SelectionController` and `CameraControllerRegistry`
services determine whether those requested effects can run; materialization
remains valid when either service is absent. Sandbox installs only its separate
camera-plus-selection `F` input action. The former import authoring,
postprocessor, and completion callback registries, the exported
`Extrinsic.Runtime.SandboxDefaultPolicies` module, and the Engine-bound
lifecycle helper are absent. With recipe authoring disabled only where the
validated payload contract permits it, geometry still materializes but receives no
render/selection/visualization defaults, no focus/selection mutation, and no
generated-normal post-process.

Model-scene materialization creates explicit ECS node entities and primitive
leaves from the selected-scene payload. Node entities retain authored local TRS
and hierarchy; repeated primitive references create distinct leaf entities with
distinct world transforms while sourcing the same decoded CPU prototype.
Synchronous and queued model-scene routes then apply the recipe authoring step
to each primitive leaf in scene order and publish one `ModelScene` completion
whose entity span excludes the structural nodes. Its finite focus target
encloses all primitive world bounds, so the sandbox defaults select the first
primitive and focus once only after every renderable/selectable leaf exists.

### Sandbox Editor Async Method Jobs

Editor buttons that run heavyweight geometry or method work submit typed
commands through `SandboxEditorContext::GeometryCommands`, then return a
pending result to the ImGui frame. Runtime snapshots the main-thread input and
queues a `JobDesc` on `Runtime.JobService` together with the editor-owned
entity/output identity used by deduplication and queue-row queries.
Workers never access live ECS or renderer state. The Engine's pre-pump-B
completion gate drains at most eight completed jobs per frame, revalidates the
selected target before mutation, and publishes results only from the
main-thread apply callback.

`RUNTIME-141` Slice A first applied this model to the CPU K-Means path;
`ARCH-012` supersedes Sandbox composition with `ClusteringModule` and
`JobService`, while the standalone command helper keeps the async/immediate
fallback for isolated tests and uncomposed callers. Slice B applies the model to
Progressive Poisson point-cloud and mesh-surface CPU sampling,
Slice C applies it to mesh denoise/remesh/simplify commands, and Slice D
applies it to ICP registration alignment while preserving the existing
immediate fallback for tests and callers without an engine job surface. Slices
E.1, E.2, E.3, E.4, and E.5 apply it to mesh curvature, mesh subdivision,
mesh/graph/point-cloud vertex-normal recompute, point-cloud outlier removal,
and selected-mesh UV regeneration. Slice F.1 starts the panel-state follow-up:
the selected-mesh UV panel reads the generic job snapshot, invalidates
the selected-analysis cache when per-entity job status changes, and keeps the
last pending/completed UV result visible through the attached editor sink. Slice
F.2 pins the frame-loop behavior with a deliberately slow job on the
Null-window engine path: the ImGui editor callback must stay bounded while the
worker job is running, and render begin/extract/prepare/execute/end must still
complete for a frame that entered rendering while the job was pending/running.
Slice F.3 closes duplicate-submit UX with a shared active-job guard: converted
editor CPU commands suppress same-entity/same-domain/same-output submissions
while an existing job is blocked, queued, running, or applying, returning the
existing pending handle instead of enqueueing duplicate work. Terminal jobs do
not block an explicit re-run. The current panels do not expose a cross-command
cancel button; cancel remains a lower-level registry command surface.

### Sandbox Editor Startup Layout

`UI-018` makes `Extrinsic.Sandbox.Editor.Shell` menu-first on startup. The
first sandbox ImGui frame draws the main menu bar only; `Sandbox Editor`,
`Scene Hierarchy`, `Inspector`, `Selection Details`, `File / Scene`,
`File / Import`, `Frame Graph`, `Render Recipes`, `Camera / Render`,
`Geometry Visualization`, and all registered PointCloud/Graph/Mesh windows stay
closed until toggled from the menu. The open/closed bits live in the app-owned
`EditorShell` and do not change runtime panel models or command routing.

`UI-034` provides the generic contribution and input contract. App windows
register through `EditorShell::RegisterEditorWindow(...)` with stable ids and
structured menu paths, then remove the returned handle through
`UnregisterEditorWindow(...)`; closed windows receive no draw callback, and
global hide preserves their individual open states. All Sandbox windows are
app-owned registry contributions. Mesh / Appearance forwards the workspace's
callback-scoped borrowed selected-mesh vertex-property view to the
runtime-owned generic scalar-property widget and never retains the view.
Callbacks receive the app-owned, frame-local `SandboxEditorContext` without
`Engine&`. The shell copies the `EditorWorkspaceSnapshot`, feature result
snapshots, and focused scene, geometry, visualization, render-recipe, and
workspace-query handles; the private runtime attachment binding never crosses
into app code. Runtime retains validation, jobs, history, stale-result checks,
and mutations behind those feature-owned modules. Their implementation bodies
compile separately from the app shell.

`EditorUiModule` routes the unsuppressed `G` input action through the same
`EditorUiVisibilityCommand` path used by programmatic callers. The frame loop
owns one `EditorInputCaptureSnapshot` for the frame and lends it to
`UiBegin`/`UiBuild`/`UiEndCapture` and later hooks by reference. At the end of
each visible editor frame, `ImGuiAdapter` records keyboard capture, mouse
capture, and active-widget state; the module copies that completed result only
after `EndFrame()`. Camera, gizmo, picking, input actions, and later hooks read
the same value. Hiding the editor clears adapter and published host capture
diagnostics immediately, so the viewport does not inherit stale capture from
the prior frame. Direct ImGui capture reads remain confined to
`Runtime.ImGuiAdapter.cpp`.

The delivered RUNTIME-138 selected-entity baseline is visibility-gated: the
attached `EditorShell` derives an
`EditorWorkspaceSnapshotRequest` from currently open panel windows, so closed
Scene Hierarchy, Inspector, Selection Details, and Geometry Visualization panels
skip their selected-entity model sections. Cheap document/import/menu/status
models are still built so command/status continuity is preserved. Open domain
windows build their `EditorDomainWindowModel` only after `ImGui::Begin()`
confirms the window is visible, and all sections for the same domain share one
per-frame domain-window model cache. `EditorWorkspaceSnapshot::ModelBuildStats`
reports per-frame model-build and cache-hit counters plus nanosecond timing
diagnostics for selected panel, inspector, selected-analysis, property-catalog,
vertex-channel validation, UV diagnostics, texture-bake, visualization, and
domain-window model construction. It also reports full vertex-channel resolver
scan counts and scratch allocation bytes, UV texcoord finite-check element
counts, and texture-bake source-row enumeration counts for selected
property-catalog builds.
`RuntimeRenderExtractionStats` also exposes scalar visualization-recipe value
scan counts for scalar/isoline finite and range validation. The
selected-entity model cache now stores immutable selected-analysis submodels
(property catalog, geometry presentation, bound render state, and
texture-bake/UV controls) in separate inspector and per-domain-window entries,
plus visualization models across steady selected frames. Cache keys cover stable selection ids,
`SelectionController::SelectionGeneration()`,
`SceneInteractionModule::LastRefinedPrimitiveGeneration()` for primitive-sensitive
analysis, the selected geometry domain/count shape, vertex-channel binding
generation, a metadata signature over selected geometry source/property
descriptors, selected render-lane hint signature, geometry-presentation recipe
generation and visible inspector/domain-window consumer for selected
analysis, command-history revision, viewport,
visualization target, visualization command availability, and an effective
visualization-config/spatial-debug signature, plus the runtime-owned
visualization recipe revision for visualization model entries;
editor commands that mutate those selected inputs explicitly invalidate the cache.
The app-composed `AsyncWorkModule` publishes the kernel-owned `JobService` for
module/editor discovery and owns survivor cancellation at shutdown. Engine
always performs the count-limited completion drain, so a completed-work burst
cannot consume the whole frame by default even when the module is omitted.
RUNTIME-138 retired the broader async selected-analysis umbrella after the
bounded UI-030 capture did not establish editor callbacks as a dominant cost.
If a named feature later measures a material full-buffer derivation, that
feature owns the smallest generation-keyed JobService operation and its exact
staleness/diagnostic tests; no global selected-analysis service is planned.

`GRAPHICS-114` updates the runtime ImGui producer side so `ImGuiAdapter` keeps a
font-atlas cache and revision. `EndFrame()` copies atlas bytes into
`ImGuiOverlayFrame` only when the atlas payload changes; unchanged frames submit
metadata-only atlas records for the graphics overlay system to retain. Adapter
diagnostics report atlas copy/reuse counts and the last copied byte count.
The adapter converts each `ImDrawCmd::ClipRect` from Dear ImGui display space
into a finite framebuffer-relative scissor, transforms copied vertex positions
into the same framebuffer space, and omits empty or non-finite commands. It also
advertises `ImGuiBackendFlags_RendererHasVtxOffset`; large 16-bit-index draw
lists can therefore split into commands whose base vertex is preserved through
the pointer-free overlay records instead of aborting at 65,535 vertices.

### Render Artifact Publication

`RUNTIME-127` makes renderer outputs observable as runtime artifacts before any
project data changes. `Extrinsic.Runtime.RenderArtifactPublication` stores copied
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

### Sandbox Editor Render Recipe Editing

`UI-023` adds the `Render Recipes` sandbox editor panel. The frame model copies
the current renderer descriptor, declared recipe slots, active view/output
recipe, binding intents, validation diagnostics, draft state, activation
revision, and render-artifact rows into data-only editor DTOs. Fixed renderer
core slots are shown as non-editable; declared extension slots and optional
binding overrides are the only rows marked editable.

Draft updates, validation, preview, activation, cancellation, artifact publish,
and artifact apply use `ApplyEditorRenderRecipeCommand(...)`. Validation
and preview call the resolved `EngineConfigControl` service callback
(`PreviewRenderRecipeConfigDocument(...)`) without mutating graphics state.
Activation calls `ApplyRenderRecipeConfigPreview(...)` on that same service,
the path available to agent/CLI callers, which stores the active config on
runtime and installs a `Graphics::FrameRecipeOverride` on the renderer; the
editor keeps only widget/draft-buffer state plus a presentation cache for its
panel model. When the app omits the module, the session exposes null state,
empty callbacks, and false recipe/config command-availability flags.
Artifact publish/apply routes through `RenderArtifactRegistry`; the registry
authorizes project mutation for accepted candidate outputs but performs no ECS,
renderer, RHI, file IO, or scene persistence mutation itself. Draft states are
explicit across inactive, debounced, validated, rejected, previewed, activated,
and canceled outcomes, so stale or invalid recipes fail closed in the UI model.

### Sandbox Editor Vertex Normals

`UI-022` adds normal-recompute editor commands at
`Mesh > Processing > Vertices > Normals`,
`Graph > Processing > Vertices > Normals`, and
`PointCloud > Processing > Vertices > Normals`. The focused geometry-operation
surface exports per-domain command/result pairs:
`EditorMeshVertexNormalsCommand`,
`EditorGraphVertexNormalsCommand`, and
`EditorPointCloudVertexNormalsCommand`, with matching
`ApplyEditor*VertexNormalsCommand(...)` operations. The commands validate a
live selected `GeometrySources` entity, snapshot the domain-owned source data
when a `JobService` command surface is available, call the domain-owned geometry modules
from `GEOM-026` (`Geometry.HalfedgeMesh.Vertices.Normals`,
`Geometry.Graph.Vertex.Normals`, or `Geometry.PointCloud.Normals`) on the CPU
worker lane, and publish count-matched `glm::vec3` normals to canonical
`v:normal` only from the stale-checked main-thread apply. Tests and non-engine
callers without an injected job surface keep the immediate compatibility path.
Sync and queued completions enter one owner-local `RUNTIME-201` mutation
transaction. It validates geometry metadata, the exact domain source-property
snapshot excluding the owned output, and the exact optional current
`v:normal`; topology/attribute/output edits therefore stale-discard queued
work or reject undo/redo without moving history. Undo restores the prior
normal values or removes a newly introduced property, redo restores the
generated values, and every successful transition stamps only
`DirtyVertexNormals`. It does not call renderer/RHI upload APIs or stamp broad
`GpuDirty`. Mesh, graph, and point-cloud residency extraction consume that
dirty tag and perform deferred normal-channel reupload on the next extraction
opportunity. If a direct mesh import's deferred materialization applies after
an edit, runtime preserves count-matched current `v:normal` values so
editor-authored normals remain the CPU authority.

### Sandbox Editor Mesh Denoise

`UI-024` adds a mesh-only denoise editor command at
`Mesh > Processing > Denoise`. The focused geometry-operation surface exports
`EditorMeshDenoiseCommand`,
`EditorMeshDenoiseResult`, and
`ApplyEditorMeshDenoiseCommand(...)`. Runtime validates the selected
mesh `GeometrySources`, converts the current CPU data to a scratch halfedge
mesh, calls the geometry-owned `Geometry.Smoothing::DenoiseBilateral` kernel
from `GEOM-042`, and publishes count-matched finite positions back to canonical
`v:position` only after the geometry result succeeds. The UI exposes the
full-bilateral stage, normal/vertex iteration counts, auto-or-explicit spatial
and range sigma values, and boundary preservation, with a single `Denoise`
action. `EditorGeometryProcessingContext::MeshDenoiseKernelAvailable` is the
runtime-owned capability input used to produce deterministic unavailable-kernel
diagnostics in headless/editor contract tests; app code reaches it only through
the prepared geometry command handle.

Successful publication is undoable through the shared editor mutation
transaction: undo restores the exact prior `v:position` array and redo
reapplies the denoised positions only while the entity, geometry metadata, and
live position snapshot still match the expected state. An intervening property
or position mutation fails closed without moving history. The owner stamp marks
`DirtyVertexPositions` and `DirtyVertexAttributes` for deferred mesh
extraction/reupload and does not call renderer/RHI upload APIs or stamp broad
`GpuDirty`. Runtime owns the ECS composition and history seam; geometry owns
the denoising algorithm.

### Sandbox Editor Point-Cloud Outlier Removal

`UI-027` adds a point-cloud-only outlier-removal editor command at
`PointCloud > Processing > Remove Outliers`. The focused geometry-operation
surface exports `EditorPointCloudOutlierMethod` (statistical or radius),
`EditorPointCloudOutlierRemovalCommand`,
`EditorPointCloudOutlierRemovalResult`, and
`ApplyEditorPointCloudOutlierRemovalCommand(...)`. Runtime validates the
selected point-cloud `GeometrySources`, snapshots the full original point source
for undo plus a live-only worker cloud, and queues the GEOM-016 removal through
`JobService` when an engine job surface is available. The worker calls
`Geometry.PointCloud::RemoveStatisticalOutliers` /
`RemoveRadiusOutliers` on the copied cloud after garbage-collection to live
points first (so the operators — which iterate every slot — see only live points
and report live-relative counts, never resurrecting dead slots). The
main-thread apply revalidates the selected entity's point-source metadata and
full point-property/deleted-slot snapshot before publishing; the same typed
state and validation enter the shared editor mutation transaction for exact
undo/redo. An intervening position or attribute edit discards queued output or
rejects history without mutation. Full GPU/position/attribute/normal dirty tags
are stamped only after replacement publication; tests and non-engine callers
without an injected job surface keep the immediate compatibility path. The
window exposes a method toggle plus the per-method parameters: statistical
removal takes `KNeighbors`
(1–512) and a
`StdDevMultiplier` (0–100, higher keeps more points); radius removal takes a
positive `SearchRadius` and a `MinNeighbors` (0–512) threshold. It surfaces the
`OutlierRemovalResult` diagnostics (kept/rejected/non-finite counts plus the
statistical mean/std-dev/threshold) and fails closed with
`InvalidProcessingParameters` / `UnsupportedGeometryDomain` / `MissingScene`
when the inputs or selection are invalid.

Unlike vertex-normal and denoise publication, outlier removal changes the point
count, so the bounded main-thread apply rebuilds the entity's point
`GeometrySources` via `GeometrySources::PopulateFromCloud`. The published cloud
is the full-property scratch cloud compacted to the kept points (the rejected
slots are deleted and garbage-collected), so every surviving per-point attribute
— normals, K-Means labels, visualization scalars — is preserved on the kept
points rather than dropped to position-only. The publication is undoable through
`EditorCommandHistory::Execute`: undo republishes the original cloud (restored
exactly, including any prior deleted slots) and redo reapplies the kept cloud.
Because the count changed,
the commit stamps coarse `GpuDirty` plus `DirtyVertexPositions` /
`DirtyVertexAttributes` / `DirtyVertexNormals` so point-cloud extraction performs
a full deferred repack/reupload on the next extraction opportunity; the command
does not call renderer/RHI upload APIs. Runtime owns the ECS composition and
history seam; `GEOM-016` owns the removal algorithm and its diagnostics.

### Sandbox Editor Progressive Poisson Sampling

`RUNTIME-134` Slices A-D.1 add a progressive Poisson sampling playground at
`PointCloud > Processing > Progressive Poisson Sampling` and
`Mesh > Processing > Progressive Poisson Sampling`. The
focused geometry-operation surface exports
`EditorProgressivePoissonChannel`,
`EditorProgressivePoissonConfig`,
`EditorProgressivePoissonCommand`,
`EditorProgressivePoissonResult`, and
`ApplyEditorProgressivePoissonCommand(...)`. Runtime validates selected
point-cloud `GeometrySources`, or reconstructs a selected editable mesh and
samples its triangle surface through `Geometry.PointCloud.SurfaceSampling`
before calling the METHOD-012 CPU reference backend. The command publishes
deterministic per-point float properties:

- `p:poisson_level`
- `p:poisson_phase`
- `p:poisson_splat_radius`
- `p:poisson_prefix_visible`

The UI exposes the reference sampler knobs (`dimension`, `grid_width`,
`max_levels`, `hash_load_factor`, `radius_alpha`, grid-origin randomization and
seed, shuffle toggle and seed), plus a prefix count, color-channel selector, and
CPU/Vulkan-compute backend selector.
Mesh selections also expose surface sample count, surface seed, minimum triangle
area, and vertex-normal interpolation controls. Successful mesh runs publish the
sampled cloud back onto the selected entity via `GeometrySources::PopulateFromCloud`,
remove the stale surface render hint, enable point rendering, and report the
surface-sampling diagnostics in `EditorProgressivePoissonResult`.
Successful point-cloud and mesh runs also report requested method backend,
actual method backend, CPU fallback reason when present, and accepted-point
counts per progressive level, so the UI can show backend identity and
level-distribution readouts without querying method internals.

The command routes `VisualizationConfig` to the selected scalar channel so
existing point colormap extraction handles the display. Prefix count `0` shows
every accepted point; positive values clamp to the accepted count and drive
`p:poisson_prefix_visible`. The published `p:poisson_phase` is a deterministic
display bucket derived from level-local rank modulo the 2D/3D phase count because
the CPU reference backend does not yet export internal phase assignments as a
stable method result.

Slice C routes the same knobs through the engine config-control facade as
the registered `sandbox.progressive_poisson` application section: widget edits
use `SetProgressivePoissonPlaygroundConfig(...)`, serialize the candidate
`EngineConfig`, preview it with the resolved
`EngineConfigControl::PreviewEngineConfigControlDocument(...)` service,
hot-apply it with
`EngineConfigControl::ApplyEngineConfigHotSubset(...)`, and then schedule
a debounced rerun when `auto_run_on_edit` is enabled. The explicit Run button
uses the same config path before invoking the sampler command. The command only
composes runtime-owned ECS state and the public method/surface-sampling APIs; it
does not add sampler logic to UI code or call renderer/RHI upload APIs directly.
METHOD-013 extends the command/config seam with a backend request
(`CpuReference` or `VulkanCompute`) and reports requested backend, actual backend,
and CPU fallback reason. Slice B adds
`Extrinsic.Runtime.ProgressivePoissonGpuBackend`, which pins the Vulkan
storage-buffer layout, BDA push/state records, shader asset paths, and per-level
build/accept plus GRAPHICS-108 compaction dispatch plans. Slice C.2 adds the
runtime-owned executable-resource seam: SoA position uploads, initial remaining
keys, output-count initialization, pass recording, and production result buffers
for `order`/`level_offsets`/`splat_radii`. Slice D.1 adds parsed readback payloads
and CPU-reference parity diagnostics for `order`, `level_offsets`, `splat_radii`,
and per-level Poisson guarantees. RUNTIME-195 removes the three duplicate
host-visible readback targets, their pre-copies, and the blocking result
`IDevice::ReadBuffer` calls: one copied `Graphics::GpuTransfer` batch now drains
the three production buffers, while the method adapter owns typed validation.
Its actual-Vulkan smoke uses a CPU-reference-shaped seeded payload and therefore
proves transport/parser operation only. Public Sandbox execution still reports
CPU fallback for `gpu_vulkan_compute` requests until METHOD-014 proves compute
parity, but users can select either backend and inspect the requested-vs-actual
backend readout.

### Sandbox Editor Mesh Curvature

`UI-026` adds a mesh-only curvature analysis editor command at
`Mesh > Processing > Curvature`. The focused geometry-operation surface exports
`EditorMeshCurvatureCommand`,
`EditorMeshCurvatureResult`, and
`ApplyEditorMeshCurvatureCommand(...)`. Runtime validates the selected
mesh `GeometrySources`, converts the current CPU data to a scratch halfedge
mesh, calls the geometry-owned `Geometry::Curvature::ComputeCurvature` backend
from `GEOM-040`, and publishes count-matched finite vertex properties only
after the geometry result succeeds.

Successful scalar publication writes canonical `v:mean_curvature` and
`v:gaussian_curvature` `double` properties. When principal directions are
requested and available, the command also writes `v:principal_dir1` and
`v:principal_dir2` `glm::vec3` properties; when the directions lane is disabled
or unavailable, the command succeeds with scalars only and reports a
deterministic diagnostic. The UI exposes an output selector, a principal
directions toggle that is inert when directions are unavailable, and a single
`Compute` action. Successful commits are undoable through the shared editor
mutation transaction, which validates geometry metadata, the exact source
positions, and the four owned curvature-property snapshots before apply, undo,
or redo. An intervening geometry or curvature-property edit fails closed
without moving history. Successful publication stamps
`DirtyVertexAttributes` and does not call renderer/RHI upload APIs or stamp
broad `GpuDirty`.

Published curvature properties use the ordinary closed
`Extrinsic.Runtime.VisualizationRecipes` alternatives. Scalar curvature maps
through `ScalarVisualizationRecipe`; principal directions may be authored as
separate `VectorFieldVisualizationRecipe` values over the canonical direction
properties. Missing, wrong-typed, count-mismatched, or non-finite sources fail
closed with deterministic recipe diagnostics; no curvature-specific wrapper
or fallback dispatch path exists.

### Sandbox Editor Mesh Remesh And Subdivide

`UI-025` adds mesh topology replacement commands at
`Mesh > Processing > Remesh` and `Mesh > Processing > Subdivide`. The focused
geometry-operation surface exports `EditorMeshRemeshCommand`,
`EditorMeshRemeshResult`, `ApplyEditorMeshRemeshCommand(...)`,
`EditorMeshSubdivideCommand`, `EditorMeshSubdivideResult`, and
`ApplyEditorMeshSubdivideCommand(...)`. Runtime validates a live selected
mesh `GeometrySources` entity, builds a scratch halfedge mesh, calls the
geometry-owned `GEOM-043`/`GEOM-044` kernels, and publishes the resulting
topology back through `GeometrySourcesPopulate` only after the geometry result
succeeds.

The remesh window exposes uniform/adaptive mode, target edge length, iteration
count, project-to-surface, and mean-curvature versus error-bounded Taubin sizing
selection. Uniform mode calls `Geometry.Remeshing`; adaptive mode calls
`Geometry.HalfedgeMesh.AdaptiveRemeshing`, mapping the editor target length to a
bounded adaptive sizing range. The subdivide window exposes Loop, Catmull-Clark,
and Sqrt(3) operators, iteration count, and Loop feature-edge preservation.
Each backing kernel and option has an explicit `SandboxEditorContext` feature
gate, so unavailable operators return deterministic diagnostics without
mutating `GeometrySources`.

Successful remesh and subdivide commits are undoable through the shared editor
mutation transaction: undo restores the exact prior mesh snapshot and redo
reapplies the generated mesh only while geometry metadata and the complete
canonical position/connectivity state still match. In-place topology changes
also stale-discard queued output before publication. Publication stamps
`DirtyVertexPositions`, `DirtyVertexAttributes`, `DirtyEdgeTopology`, and
`DirtyFaceTopology`, and does not call renderer/RHI upload APIs or stamp broad
`GpuDirty`; mesh extraction repackages/reuploads on the next deferred
extraction opportunity.

### Sandbox Editor Mesh Simplify

`UI-028` adds a mesh decimation command at `Mesh > Processing > Simplify`. The
focused geometry-operation surface exports `EditorMeshSimplifyMetric`,
`EditorMeshSimplifyCommand`, `EditorMeshSimplifyResult`, and
`ApplyEditorMeshSimplifyCommand(...)`. Runtime validates a live selected
mesh `GeometrySources` entity, builds a scratch halfedge mesh, calls the
geometry-owned `GEOM-014` `Geometry::Simplification::Simplify` kernel in place,
and republishes the collapsed topology through the shared mesh
topology-replacement seam. The window exposes the error metric (Classical QEM /
feature-aware FA-QEM, the GEOM-014 default), a target face count, an optional
per-collapse max-error cap (`0` = unlimited), boundary preservation, and the
FA-QEM feature weights (feature angle, normal/boundary/curvature weights, sharp-
feature and UV-seam pinning), and reads out the `Result` diagnostics: input →
output vertex/face counts, collapse count, max collapse error, topology/quality
rejections, and pinned sharp-feature/UV-seam counts. Commits use the same
generation-validated topology transaction as remesh/subdivide and stamp the same
`DirtyVertexPositions`/`DirtyVertexAttributes`/`DirtyEdgeTopology`/
`DirtyFaceTopology` tags as remesh/subdivide, without renderer/RHI upload calls
or broad `GpuDirty`. The runtime-owned
`EditorGeometryProcessingContext::MeshSimplifyKernelAvailable` capability gates
the executor so an unavailable kernel returns deterministic diagnostics without
mutating `GeometrySources`; it is not app-owned Sandbox state.

### Sandbox Editor Mesh Parameterization

The selected-mesh parameterization command dispatches the configured LSCM,
harmonic-cotangent, Tutte-uniform, or BFF CPU strategy, validates finite
count-matched output, and publishes canonical `v:texcoord` values through the
shared editor mutation transaction. Each initial apply, undo, and redo
revalidates geometry metadata plus the exact semantic triangle topology,
finite `v:position` values, and current optional UV property consumed by the
solver. Undo restores the prior UV values or removes a newly introduced
property; redo restores the generated values. An intervening position,
topology, or UV edit returns `StaleEntity` without changing geometry or the
history cursor. Successful transitions stamp `DirtyVertexTexcoords` and
`DirtyVertexAttributes` only after publication, while the transaction retains
no selected-model cache or other session-owned state.

### Sandbox Editor ICP Registration

`UI-029` adds an `ICP Registration` panel reachable from the `View` menu.
`ARCH-006` Slice 3 preserves that path through the app-owned stable registration
`view.registration` rather than a fixed runtime window-kind slot. The Sandbox
geometry-operation surface exports `EditorICPVariant`,
`EditorRegistrationCommand`, `EditorRegistrationResult`, and
`ApplyEditorRegistrationCommand(...)`. The command reads the source and
target point positions from two selected point-cloud entities, requires both to
resolve to `GeometrySources` `Domain::PointCloud`, invokes
`Geometry::Registration::AlignICP` through the operation's private trajectory
collector, selects the requested preview pose, and drives the source entity's
`Transform::Component` through the same internal
generation-validated transaction used by direct transform edits. Each history
transition revalidates the captured world/registry/entity identity and exact
expected transform before atomically replacing the component and stamping
`Transform::IsDirtyTag`; an intervening transform edit fails closed instead of
being overwritten by undo/redo. The panel takes the source/target from the
current multi-selection (with a swap toggle), exposes the `ICPVariant`, max
iterations, max correspondence distance (`0` = unlimited), and inlier ratio,
and provides a trajectory-step slider over `[0, IterationCount()]` that scrubs
the previewed pose. The command owns no geometry, renderer, or asset state; it
only reads point positions, calls the runtime controller, and edits the source
`Transform`.

### Sandbox Editor Appearance / Properties Reorganization

`UI-031` reorganizes the per-domain windows so concern ownership is clear. The
former `Render` window is renamed `Appearance`
(`Mesh/Graph/PointCloud / Appearance`, opened from the `Appearance` menu item)
and now co-locates render hints, visualization controls (including uniform/lane
color and visualization-property presets), bound render-state inspection,
property/attribute assignment (`DrawPropertyBindingTargets` +
`DrawVertexChannelBindingTargets`), and texture baking
(`DrawTextureBakeControls`). The `Properties` window is now a pure property
explorer — it lists every property and its value preview
(`DrawPropertyCatalogRows`) plus diagnostics only, keeping
internal/connectivity/generated rows visible, and no longer hosts render-hint,
visualization, binding, or texture-bake controls. Processing menu leaves open
focused method windows such as `Mesh / Processing / Denoise`,
`Mesh / Processing / Simplify`, `PointCloud / Processing / Remove Outliers`,
and `Graph / Processing / Vertices / Normals`; the old omnibus per-domain
`Processing` window is no longer the primary execution surface. Delivered
selected-model caching remains, while any future async derivation requires a
measured feature-local need rather than the retired RUNTIME-138 umbrella.

`UI-033` makes the `Appearance` windows compositional over render lanes rather
than exact provenance-domain gates. `PointCloud / Appearance` is the point/vertex
lane surface and can show controls for selected point clouds, graphs, or meshes
when the points lane is available; `Graph / Appearance` owns edge/connectivity
appearance; `Mesh / Appearance` owns surface/face appearance. The exact-domain
gate remains in the raw `Properties` window and the `Processing` windows so
method execution cannot silently run on a richer or incompatible domain.

### Sandbox Editor Vertex Channel Bindings

`RUNTIME-123` provides normal/color vertex-channel binding controls through
`Extrinsic.Runtime.VisualizationEditingOperations`, with their copied model
assembly included in `EditorWorkspaceSnapshots`, for mesh, graph, and
point-cloud entities. The
property catalog exposes one target each for `VertexChannel::Normal` and
`VertexChannel::Color`, lists only the selected entity's structural vertex
domain (mesh vertices, graph nodes, or point-cloud points), and evaluates each
candidate through `VertexAttributeBinding`. Normals accept count-matched
`glm::vec3`; colors accept count-matched `glm::vec3` or `glm::vec4` and pack
through `ResolveColorChannelPackedUnorm8`. Resolver status, source/fallback
counts, and non-finite repair counts remain visible in the data-only model.

`ApplyEditorVertexChannelBindingCommand(...)` mutates only the runtime
ECS descriptor `VertexChannelBindingSet`. Under `RUNTIME-201`, the complete
optional descriptor enters the shared editor mutation transaction: undo/redo
restore the exact binding set, each transition rejects an intervening binding
or generation edit, and the selected normal/color dirty domain is stamped only
after successful publication. It does not allocate renderer resources, call
RHI upload APIs, or persist material/asset authoring state. Runtime render
extraction reads the component and passes it to
`PackMesh`/`PackGraph`/`PackCloud`; graphics receives only the resulting
channel byte spans through public `GpuWorld` upload descriptors.

### Visualization UI Controls

`UI-019` keeps mesh, graph, and point-cloud visualization color editing in
`Extrinsic.Runtime.VisualizationEditingOperations`; app-owned domain visualization windows and the
top-level `Geometry Visualization` panel route the existing uniform-color
source through `ApplyEditorVisualizationConfigCommand(...)`; when
`VisualizationConfig::ColorSource::UniformColor` is active they expose an
ImGui color edit widget for the config's `glm::vec4 Color`. The UI does not
own renderer state, property-buffer residency, or graphics resource uploads.

`UI-020` adds lane-targeted visualization edits for the same command/model
surface. The top-level `Geometry Visualization` panel still edits the selected
entity's default `VisualizationConfig`; Mesh, Graph, and PointCloud domain
visualization windows target surface, edge, and point render lanes respectively
and store optional `VisualizationLaneOverrides` entries. Editability is based
on the source rows a lane needs (`Vertices`/graph nodes for points, `Edges` for
lines, faces plus vertices for mesh surfaces) rather than only on the mutually
exclusive `GeometrySources::ActiveDomain()`, so a mesh or graph can give its
rendered vertices/nodes a uniform point-lane color independently of edge or
surface color.

Under `RUNTIME-201`, default and lane-targeted visualization changes share one
owner-local mutation transaction. Each history transition revalidates the exact
stored optional config for the stable `{world, entity, lane}` identity before
replacing it; an intervening edit leaves ECS state and the undo/redo cursor
unchanged. `EditorCommandHistory` stores only the generic record and no longer
exports a visualization-component adapter.

`UI-021` makes `Extrinsic.Runtime.GeometryAvailability` the shared availability
policy for those editor models and commands. Domain windows, visualization
targets, property catalogs, primitive-view toggles, render hints, K-Means
source choices, and mesh UV/bake diagnostics ask for the source data or render
lane they need while preserving mesh, graph, or point-cloud provenance labels.

### Geometry Presentation Editor Inspector

`UI-015`, migrated by `RUNTIME-193`, extends
`Extrinsic.Runtime.VisualizationEditingOperations` with data-only
geometry-presentation operations; presentation-free copied inspector models
are assembled in `EditorWorkspaceSnapshots`, and the app owns the ImGui rows.
The selected-entity model reports
composition, mesh, graph, and point-cloud shapes; lane/presentation slots;
uniform default colors; compatible-first property choices with incompatible
entries disabled and explained; readiness and diagnostics; per-entity
derived-job rows from an injected `DerivedJobQueueSnapshot`; and aggregate child
summaries for composition entities. Slot default and source-property commands
route through the shared generation-validated editor transaction when history
is available. Apply, undo, and redo require the expected
`GeometryPresentationRuntimeState::RecipeGeneration` and stamp a fresh
monotonic generation, so an intervening bake/output or authoring mutation
cannot be overwritten through an ABA generation restore. They mutate only the
authored `GeometryPresentationRecipe`; readiness, diagnostics, and generated
outputs remain in `GeometryPresentationRuntimeState`. UI code does not run
geometry algorithms, texture bakes, asset IO, worker jobs, or graphics uploads,
and it does not
implicitly copy transient selection/highlight overlays into authored
properties.

### Geometry Property And Bake Inspector

`UI-016`, `UI-017`, and `UI-014` use
`Extrinsic.Runtime.VisualizationEditingOperations` plus copied
`EditorWorkspaceSnapshots` models for framework24-style property and
render-state inspection without importing
framework24 ownership patterns. The selected-entity property catalog enumerates
mesh vertex/edge/halfedge/face, graph node/edge, and point-cloud point
properties, including canonical topology/internal rows that visualization
presets intentionally filter out. Supported scalar, label, `glm::vec2`,
`glm::vec3`, and `glm::vec4` rows carry value kind, component count, element
count, descriptor identity, compatibility data, and selected-value previews;
unknown or unsupported property storage remains visible with deterministic
disabled reasons.

The bound-state model reports render lanes, geometry-presentation slots,
source kind, property/default/texture backing, readiness, generated-output
state, retained previous output, property-catalog correlation, and derived-job
progress/diagnostics for mesh, graph, point-cloud, and composition selections.
Rows are data-only snapshots; they do not store raw property pointers, renderer
handles, GPU buffer addresses, or live `AssetService` state.

The UV/texture-bake panel reports selected-mesh `v:texcoord` availability,
count matching, finite-value diagnostics, checker-preview availability, the
promoted fast-staged backend, and mesh UV regeneration command availability. The
UV regeneration command triangulates the selected mesh `GeometrySources`, then
queues `Geometry.UvAtlas` through the runtime `JobService` when that job
surface is available. The worker runs from copied mesh soup, property, authored
UV, and topology snapshots. The main-thread apply phase and every undo/redo
transition enter the shared generation-validated editor mutation transaction
and revalidate exact live positions, edge/halfedge/face connectivity, and known
vertex/face property values. Publication copies remapped properties back to the
regenerated halfedge mesh, repopulates `GeometrySources`, then stamps geometry
and full-GPU dirty tags. Intervening topology or authored-property edits discard
queued output or reject history without mutation. The panel reports the
matching `uv_regeneration` job status from
the runtime queue snapshot and stores the immediate `Pending` result until the
main-thread apply sink publishes the completed result. Callers without a job
surface still use the same worker/commit path synchronously for compatibility.
Texture baking consumes the property catalog, lists mesh vertex/edge/face
bakeable sources separately from internal, connectivity, unsupported, graph,
and point-cloud rows, and calls the exact `TextureBakeService` published by
`Extrinsic.Runtime.TextureBakeModule`; UI code never runs a baker, mutates
`AssetService`, or touches graphics/RHI residency directly. The controls expose
the UV property, output identity/name and extent, raw-float versus encoded-RGBA
storage, encoder, encoding colormap, and every compatible caller-owned
presentation target. Raw scalar textures retain their numeric values and select the
albedo/scalar colormap at render time, so changing Viridis/Inferno does not
rebake. A baked texture can feed multiple compatible targets, while one
physical material channel has only one entity-wide owner. Catalog rows report
pending/ready/failed state and support target edits, rename, and remove;
removal destroys the generated asset and restores affected presentation slots
to property-buffer rendering. Reusing the same entity/output identity rebakes
into the same asset, whereas renaming first preserves the old asset and lets the
default identity produce a new one. Object/world normals must exist as explicit
properties before selection. Null, lost, stale-property/UV/topology, or
incompatible residency paths fail closed without selecting the CPU
fallback.

### Point And Primitive View Payloads

BUG-028 added flat-circle, surface-aligned-circle, and impostor-sphere point
rendering for mesh vertices. RUNTIME-106 moves the authoritative control surface
to the promoted render components: `RenderPoints::Type` selects flat, sphere, or
surfel rendering, and a uniform float `RenderPoints::SizeSource` is forwarded to
the retained point-sidecar `GpuEntityConfig::Point.PointSize` every frame,
including clean reuse frames. The same component vocabulary applies to mesh vertices,
graph nodes, and point-cloud points. Older primitive-view editor/engine command
surfaces are compatibility shims that translate to `RenderEdges` /
`RenderPoints`; extraction no longer consumes `MeshPrimitiveViewSettings` as a
toggle source.

Under `RUNTIME-201`, both the generic render-hint command and the mesh
primitive-view compatibility command use one owner-local mutation transaction.
The transaction snapshots the complete optional `RenderSurface`,
`RenderEdges`, and `RenderPoints` cohort and revalidates it before initial
apply, undo, or redo. An intervening component edit returns `StaleEntity`
without overwriting another lane or moving the history cursor. The former
public primitive-view history adapter is deleted; only the feature owner
translates primitive-view input into canonical render components.

Edge view sidecars prefer authored mesh `Edges` rows, but BUG-028 also derives a
unique wireframe line list from valid halfedge/face surface topology when a mesh
has no explicit edge rows. Graphics still consumes only the immutable retained
line renderable; topology traversal remains in runtime.

The shared retained point vertex format is still 20 bytes (`pos.xyz, uv`), but
the UV fields are neutral zeroes and are not used for normals. The promoted
`assets/shaders/forward/point.vert/frag` pair currently supports mode 0 flat
circles, mode 1 depth-corrected impostor spheres, and mode 2 surfel shading with
a neutral fallback normal. A future dedicated normal-buffer residency slice must
restore true normal-aligned surfel ellipses without reusing texture coordinates.

`Extrinsic.Runtime.EngineConfigBoot` exports `CreateReferenceEngineConfig()` so
reference applications can request the standard runtime configuration without
importing the full `Engine` interface. Applications may pass the returned config
to `Engine`; runtime remains responsible for interpreting subsystem
configuration and composition. Its registry overload populates the canonical
defaults from a caller-owned `EngineConfigSectionRegistry`.
`ResolveEngineConfigForBoot(...)` is the sandbox
boot helper layered on top of that reference value: it checks
`--engine-config`, `INTRINSIC_ENGINE_CONFIG`, and an existing
`config/engine.json` path, then uses the core-owned
`Extrinsic.Core.Config.EngineLoad` diagnostics lane to preview the file before
constructing `Engine`. Invalid or unreadable explicit files keep the reference
config and preserve diagnostics in `EngineConfigBootResult`; runtime does not
mutate a live engine from this path. Sandbox constructs
`EngineConfigControl` before this call, resolves through that exact object's
owned registry, and moves the same control into `Engine::AddModule(...)`.
`CreateReferenceEngineConfig()` flips
`EngineConfig::ReferenceScene::Enabled = true` and
`Selector = ReferenceSceneSelector::Triangle`; this is boot-time application
policy interpreted by the Sandbox app, not by generic `Engine`. The
default-constructed `EngineConfig{}` keeps `Enabled = false` so existing
CPU/null tests do not regress. `CreateReferenceEngineConfig()` also sets
`Render.EnablePromotedVulkanDevice = true` so the reference sandbox requests the
promoted Vulkan backend by default (GRAPHICS-080). Whether the runtime actually
binds Vulkan or falls back to Null is governed by the GRAPHICS-033 truth table
in `src/graphics/vulkan/README.md` — when the promoted backend was not compiled
in, the host lacks Vulkan support, or the operational gate is not yet
satisfied, the runtime resolves to Null, emits the
`VulkanRequestedButNotOperational` breadcrumb, and continues. The
default-constructed `EngineConfig{}` keeps
`Render.EnablePromotedVulkanDevice = false` so unit/contract suites that drive
`Engine::Initialize()` against the Null device do not regress and do not emit
the breadcrumb.

Apps that compose live control resolve `EngineConfigControl` through
`Engine::Services()`, documented in
[`docs/architecture/runtime-config-control.md`](../../docs/architecture/runtime-config-control.md).
Recipe preview/activation uses
`EngineConfigControl::PreviewRenderRecipeConfigDocument(...)`,
`LoadRenderRecipeConfigPreviewFile(...)`,
`ActivateRenderRecipeConfigDocument(...)`, and
`ApplyRenderRecipeConfigPreview(...)`. Engine-config preview uses
`PreviewEngineConfigControlDocument(...)` /
`LoadEngineConfigControlFile(...)`; hot apply is intentionally limited to
`render.default_recipe_config_path`, `render.enable_gpu_profiling`, and
registered `app.sections` records through
`ApplyEngineConfigHotSubset(...)`. The profiling bit defaults off and changes
only per-frame recording enablement; hot toggles do not construct, destroy, or
replace the device-owned profiler. Changed section names are lexical; callbacks
fire once after commit and never during preview, rejection, or no-change. All
other engine-config differences are reported as boot-only rejections and do
not mutate the live engine; the active config authority remains the
`Engine`-owned `EngineConfig` value borrowed by the subsystem.

Runtime consumes `Extrinsic.Core.FrameLoop` for reusable platform/render/
maintenance/shutdown phase contracts. The contract lives in `core` because it has
no higher-layer imports; `Runtime.Engine` supplies runtime-specific hook
implementations during composition.

`Engine::RunFrame()` keeps per-frame lifecycle state in an internal
`RuntimeFrameContext` record: clamped prior completed-frame delta, fixed-step
interpolation alpha, monotonic render frame index, `RenderFrameInput`,
extraction stats, and the acquired `RenderWorldPool` front slot. This keeps the
stage data explicit without exporting a runtime API or reviving legacy
`Runtime.FrameLoop`, `Runtime.RenderOrchestrator`, or
`Runtime.ResourceMaintenance` modules.
After the `UiEndCapture` hook returns, `RunFrame()` reads the committed
`Render.EnableGpuProfiling` value exactly once into that immutable
`RenderFrameInput`. Editor/config applies at the hook therefore affect the
same acquired frame; extraction and rendering never reread mutable config.
Single-use frame-hook adapters, fixed-step/camera/input helpers, pick-context
capture, and pick-readback refinement live as private `Runtime.Engine.cpp`
helpers so `RunFrame` stays an ordered phase list while preserving the same
runtime-owned composition points.

## Derived overlay producer decision

`RUNTIME-104` classifies legacy `Graphics.OverlayEntityFactory` behavior for
current promoted workflows and deliberately adds no `DerivedOverlayProducer`
module. Cross-domain mesh, graph, and point-cloud child overlays are represented
by ordinary promoted `GeometrySources` entities when a runtime/editor command
imports or authors data; mesh edge/vertex overlays are component-driven
`RenderEdges` / `RenderPoints` lanes implemented as runtime-owned primitive-view
sidecars over the parent mesh. Current vector-field and isoline workflows use
`Extrinsic.Runtime.VisualizationRecipes` to emit data-only visualization packets
(`VectorFieldOverlayPacket` and `IsolineOverlayPacket`) into
`RuntimeRenderSnapshotBatch`; that path creates no child ECS entity, stores no
graphics/RHI handle in ECS, and leaves backend command-shape proof retired by
`GRAPHICS-085`.

The retired overlay snapshot sketches in
`docs/migration/nonlegacy-parity-matrix.md` are historical planning notes, not
current API debt. Reopen a value-gated runtime/editor task only if a concrete
current workflow cannot be represented by ordinary geometry entities,
component-driven primitive-view sidecars, transient debug packets, or existing
visualization packet spans.

## Scene lifecycle and persistence boundary

`RUNTIME-100` defines the promoted scene-manager replacement as a runtime-owned
scene replacement boundary, not a broad legacy serializer clone.
`SceneDocumentModule` parses a load into a temporary registry first; only a
successful parse snapshots the registered replacement participants and reaches
the live scene. It runs all deterministic `BeforeReplace` callbacks while the
outgoing registry is valid, replaces the contents, runs all `AfterReplace`
callbacks against the same rebound registry, and resets its exact owned
`EditorCommandHistory` after the complete transition. New/close use the same
contract. The module itself owns no selection, lookup, readback, extraction,
asset, bake, or GPU object.

`SceneInteractionModule`, `AssetWorkflowModule`, and `TextureBakeModule` own
their respective interaction, asset-handoff, and generated-texture replacement
participants. The document module allocates participant generations across its
full module lifetime rather than per boot, so recycling a slot index after
reinitialize cannot make a stale participant handle valid again:

- `SelectionController::ClearSceneState(...)` drops selected/hovered ECS tags,
  selected-id snapshots, pending picks, and in-flight pick correlation without
  resetting its monotonic issue sequence.
- `SceneInteractionModule` cancels a gizmo drag while the outgoing registry is
  live, restores the authored transform, and clears selected scratch and
  packets while retaining app-global tuning/mode. Durable gizmo undo belongs
  to the document `EditorCommandHistory`, which the document resets after the
  complete replacement transition.
- `SceneInteractionModule` also drops all world/epoch pick-correlation contexts
  and advances the refined-result generation. The stable lookup binding
  disconnects and clears; `AfterReplace` rebuilds it against the same rebound
  registry and publishes an empty interaction snapshot.
- `RenderExtractionCache::ClearSceneState(...)` frees scene-owned renderable
  instances/geometry, hard-shuts the unified geometry residency owner, clears mesh
  edge/vertex sidecars, copied visualization recipes, and transient batches,
  then submits an empty snapshot.
- `PhysicsBridge::Clear()` is the physics-side reset contract: it clears the
  physics world, `StableId -> BodyHandle` sidecar, and fixed-step accumulator.
  `Engine` does not yet own a bridge instance, so engine-integrated physics reset
  remains a wiring decision for the task that installs physics into the frame
  loop.

Active-world Maintenance is deliberately distinct.
`AssetWorkflowModule` validates its exact
`{WorldHandle, Registry*, binding epoch}` before every tick, direct import, and
model-scene callback; a mismatch destroys and rebinds its asset/bake handoffs
without synthesizing a document new/load/close transition. The generic
render-extraction lane independently retires its outgoing sidecars.
`SceneInteractionModule` validates the active handle/registry before every
input, extraction, lookup, and readback action, so it resets/rebinds before
delayed world events can expose stale state and never resurrects interaction
on away/back. `SceneDocumentModule` independently validates before document
operations and resets path/event sequence/history on mismatch.

Persistence support is intentionally narrow and explicit:

| Family | Status | Reason |
|---|---|---|
| Metadata names, stable ids, local transforms, hierarchy links, selectable tags | Supported | Current sandbox/editor scene identity and hierarchy need round-trip coverage. |
| Render surface/edge/point hints, `VisualizationConfig`, and `VisualizationLaneOverrides` | Supported | These are CPU descriptors consumed by runtime extraction; no live GPU handles are serialized. |
| Mesh/graph/point-cloud `GeometrySources` property data | Supported | These are the current sandbox-authored geometry authorities. |
| Lights and shadow-caster tags | Deferred | Runtime extraction consumes light descriptors, but the current scene file scope does not yet define light/shadow authoring UX or compatibility policy. |
| Collider and rigid-body descriptors | Deferred | ECS authoring descriptors exist and `PhysicsBridge::Clear()` resets live sidecars, but engine-owned physics lifecycle wiring is not installed. |
| `AssetInstance::Source` and legacy scene-file asset-source reimport | Retired from scene JSON | Reimport is an explicit runtime `AssetId` operation through `AssetService` path metadata and the ingest state machine; scene files do not persist live asset-source coupling or resurrect ECS `AssetSourceRef` semantics. |
| Transient per-entity `VisualizationRecipe` values | Deliberately not serialized | Runtime/session sidecar data is reconstructed from durable `VisualizationConfig` and `GeometryPresentationRecipe` intent; no registry or external borrowed buffer lifetime is persisted. |
| Camera/editor document state | Retired from scene JSON | Camera controllers and `EditorCommandHistory` are runtime/editor state, not ECS scene contents. |
| Renderer/RHI caches, GPU handles, material bindless slots | Retired from scene JSON | Graphics resources are rebuilt from runtime snapshots and asset ids; scene files never store live backend state. |

Unsupported ECS families encountered during save are counted in
`SceneSerializationStats` and emitted in the JSON `stats` object. They are not
materialized on load, which keeps unsupported persistence fail-closed and
diagnosable rather than silently pretending parity with legacy component dumps.

## Engine initialisation ordering

`Engine::Initialize()` runs the following ordered steps once per engine
lifetime (re-`Initialize` after `Shutdown` repeats the same order against
freshly-constructed subsystems):

1. `Core::Tasks::Scheduler::Initialize` — CPU fiber scheduler must be live
   before any task-graph or streaming dispatch runs.
2. Platform window, runtime device bootstrap, renderer construction +
   `Initialize`. `Extrinsic.Runtime.DeviceBootstrap` owns the device-selection
   truth table, concrete backend factory call, and GPU asset fallback-texture
   descriptor bytes; `Engine` remains the composition caller and still fills
   the platform-window-backed `RHI::DeviceCreateDesc`. Immediately after
   `IDevice::Initialize`, the runtime evaluates
   `ShouldEmitVulkanRequestedButNotOperationalBreadcrumb(...)` from the
   bootstrap module against the resolved `RenderConfig` and
   `IDevice::IsOperational()`. When the runtime
   requested the promoted Vulkan device (`Backend == Vulkan` &&
   `EnablePromotedVulkanDevice`) but the resolved device is non-operational,
   `Core::Log::Warn` emits one
   `[Runtime] VulkanRequestedButNotOperational status={...} reason={...}`
   breadcrumb per `Engine::Initialize()` and calls
   `Backends::Vulkan::RecordVulkanOperationalFallback(...)` to advance the
   `VulkanOperationalDiagnosticsSnapshot` counters (GRAPHICS-033B). When the
   Vulkan backend is not compiled in, the status/reason text is
   `NotCompiled`/`None` and no Vulkan counters are touched (the diagnostics
   surface does not exist in that build). Runtime never aborts solely
   because requested Vulkan falls back to Null — see the truth table in
   `src/graphics/vulkan/README.md`.
   Immediately after renderer initialization, runtime attempts the boot-only
   render recipe file from
   `Core::Config::RenderConfig::DefaultRecipeConfigPath` when that path is
   non-empty. Usable `RenderRecipeConfig` previews flow through the shared
   `Runtime.RenderRecipeActivation` free functions also used by the optional
   live-control module; missing or invalid startup files clear the active
   override and leave the derived default frame recipe in place. When
   `EngineConfigControl` is composed, registration copies the startup result
   into `GetRenderRecipeState().LastApply` without loading the file again.
   Later live engine-config hot
   applies validate the referenced recipe before mutating
   `DefaultRecipeConfigPath`, so an invalid hot file preserves the current
   active recipe override.
   `RUNTIME-203` deliberately retains both public modules: device bootstrap is
   shared by Engine backend creation and AssetWorkflow fallback setup, while
   recipe activation is shared by Engine boot, module/config control, and
   render-recipe editing. Their multiple production consumers make them
   current contracts rather than one-owner composition helpers.
3. CPU `FrameGraph`.
4. Boot `WorldRegistry` and its initial ECS registry.
5. Runtime-module registration. Engine first publishes its exact borrowed
   kernel/device/window/renderer/extraction/input services, then invokes every
   name-sorted module's `OnRegister`. When composed, `AssetWorkflowModule`
   validates the required built-ins before creating the per-boot
   `AssetService` and `GpuAssetCache`, initializes the fallback texture, installs
   the asset listener, and publishes the four exact asset capabilities. A
   non-operational device skips fallback upload without disabling CPU asset
   authority.
6. Runtime-module resolution/finalization. `AssetWorkflowModule` requires the
   already-published `SceneDocumentModule` and exact `EditorCommandHistory`,
   optionally resolves streaming and selection, registers its document
   replacement participant, borrows `TextureBakeService` when present, and
   binds the model texture/scene handoffs to the exact active
   `{WorldHandle, Registry*, epoch}`. `TextureBakeModule` then requires the same
   document/history services, registers its replacement participant plus its
   single property-texture GPU participant, and
   finalizes its already-published service. Other optional modules resolve
   their own declared services, and the complete schedule is validated and
   locked.
7. The app root initializes its concrete state after `Engine::Initialize()`.
   Sandbox initializes `SandboxSession`, optionally bootstraps reference content
   in the initial world exactly once, retains that owning handle for teardown,
   and hands the population seed to the camera registry when present.

## Canonical frame loop phases (`Engine::RunFrame`)

`Engine::Run()` owns the outer loop state: if the loop exits because
`IWindow::ShouldClose()` is already true before a frame starts or becomes true
after a frame, it normalizes that path through `RequestExit()` so
`Engine::IsRunning()` is false when `Run()` returns. A configured platform
window that initializes already closed logs a deterministic runtime warning
instead of falling back to Null silently; tests that need headless frame-loop
execution should request `Core::Config::WindowBackend::Null` explicitly.

1. Platform events / resize handling. Minimized windows wait on platform events,
   resample the frame clock, and return before ImGui or render-frame work begins;
   resize requests idle the device, resize device/renderer resources, acknowledge
   the request, and continue through the normal frame.
2. Kernel command drain. `Engine::Commands()` drains once after platform input
   and before simulation; this is the only command execution window for the
   frame.
3. Kernel event pump A. `Engine::Events()` pumps once after the command drain
   so command-published events are visible before simulation; listener-published
   cascades defer to the next pump.
4. Fixed-step simulation. Each substep appends the promoted
   `TransformHierarchy` / `BoundsPropagation` / `RenderSync` bundle, then runs
   `Compile` → `Execute` → `ResetForReplay` on the CPU `FrameGraph`. Exact
   repeated descriptors reuse the compiled topology with current callbacks.
   Dirty world
   matrices and bounds are recomputed before the next substep or render
   extraction (`RUNTIME-091`, `CORE-008`).
5. Job completion gate. `Engine::Jobs()` drains finished worker results on the
   main thread, suppresses cancelled/world-scoped completions, and publishes
   completion events only for survivors.
6. Kernel event pump B. Post-simulation and job-completion events are delivered
   before UI, variable tick, and extraction.
7. Variable-frame module hooks: `UiBegin`, `UiBuild`, then `UiEndCapture`.
8. Render input and interaction snapshot preparation. After `UiEndCapture`,
   typed viewport hooks run in module-name order: `Runtime.CameraModule`
   populates the camera before `Runtime.SceneInteractionModule` gates gizmo and
   selection input on completed capture. The runtime then checks for pending
   `Transform::IsDirtyTag` /
   `WorldUpdatedTag` work and runs the pre-render transform flush
   (the Engine-private BUG-024 lane) only when needed:
   `TransformHierarchy` → `BoundsPropagation` → `RenderSync` execute directly
   (outside the fixed-step FrameGraph) so post-fixed-step local-transform
   mutations — Sandbox Editor inspector edits applied in the ImGui editor hook,
   other module-frame-hook mutations, and module-owned gizmo drags — refresh
   `Transform::WorldMatrix`, world bounds, and `DirtyTags::DirtyTransform`
   before transform-gizmo packets are built and before render extraction
   observes the scene. Idle frames skip the redundant sweep. The runtime then
   dispatches registered input actions. Sandbox registers the `F` key-edge
   descriptor only when both exact optional `CameraControllerRegistry` and
   `SelectionController` services exist. Its callback calls
   `FocusCameraOnSelection(...)` after the bounds flush, suppresses itself while
   ImGui owns the keyboard, and refreshes `RenderFrameInput::Camera` after a
   successful focus so the snapped view reaches extraction the same frame.
   Finally the `BeforeExtraction` module hook drains the coalesced pick into
   `RenderFrameInput`, builds gizmo packets, and submits one copied
   `RuntimeSceneInteractionRenderSnapshot`. This keeps input actions and
   transform flush ahead of packet construction without adding another generic
   frame phase.
9. Renderer begin frame. Runtime acquires the render frame through
   `IRenderer::BeginFrame()`. GRAPHICS-040A keeps camera extraction unchanged;
   temporal jitter remains graphics-side, and GRAPHICS-040C maps the renderer's
   AA selector to TAA/external reconstruction, motion-vector recipe activation,
   and retained reconstruction history without adding runtime ownership.
10. Runtime render extraction: the `RenderWorldPool` producer acquires a back
   slot (`AcquireBack(frameIndex)`), then ECS queries, runtime sidecars,
   dirty-domain interpretation, and deletion cleanup consume the last submitted
   world-tagged interaction value. `RenderExtractionCache` copied the selected
   render IDs, hover identity, and gizmo packets at submission; extraction
   validates the world and supplies empty interaction data on omission or
   mismatch. Neither `ExtractAndSubmit` nor renderer snapshot batches retain a
   controller, module pointer, or refinement payload. The producer then
   publishes the
   slot (`PublishFront`). In synchronous mode the consumer acquires the current
   front (`AcquireFront(frameIndex)`, frame age 0); when
   `RenderConfig::SynchronousExtraction = false`, the consumer acquires the
   previous front (`AcquirePreviousFront(frameIndex)`) so render-N consumes the
   distinct renderer-owned snapshot storage written by extraction N-1.
   Pool/extraction counters remain on the `RenderExtractionCache` snapshot
   published during composition; Engine has no statistics forwarding getter.
11. Renderer render-world extraction.
12. Render prepare.
13. Render execute.
14. End frame + present.
15. Maintenance: transfer retirement, streaming drain/apply/pump, asset
    residency (`AssetService::Tick`, `GpuAssetCache::Tick`, and pending
    model-scene material binding re-resolution), one
    `RenderExtractionCache::TickGeometryResidency` call for every runtime-authored
    geometry lane, and `JobService` terminal-record reaping. The module
    `Maintenance` hook then drains **all** completed pick readbacks
    (`SelectionSystem::PopPickResult()` FIFO). It accepts only a known nonzero
    sequence issued for the current world and interaction epoch, applies the
    exact controller intent, and refines the primitive with the issuing-frame
    context. Unknown, evicted, old-world, and old-epoch results cannot mutate
    selection or refined output. The exact `SceneInteractionModule` exposes the
    latest refined result; graphics only produces the hint.
    After the readback drain the Engine-private `RenderExtractionService` releases the
    `RenderWorldPool` snapshot reference acquired in phase 6 at frame retire;
    this is the current front in synchronous mode and the previous front in
    pipelined mode. In the default synchronous mode the single logical slot is
    reclaimable next frame.
16. Frame clock finalize.

`Engine::RunFrame()` consumes `Extrinsic.Core.FrameClock` by sampling the stored
prior completed-frame duration for simulation, runtime hooks, camera controls,
and Dear ImGui timers. `EndFrame()` records the current frame for consumption by
the next frame, while post-sleep resampling keeps deliberate minimized/idle
sleep out of that record. Runtime owns the phase orchestration, not the reusable
clock value type. Before any frame has completed, the clock query
deterministically returns zero; the ImGui adapter retains its existing
non-positive-delta fallback for that first frame.

`UI-030` adds `Engine::GetLastFramePacingDiagnostics()` as a copied runtime
frame-pacing sample for the most recent `RunFrame()` attempt; `RUNTIME-158`
keeps the exported record and counter-mirroring policy in
`Extrinsic.Runtime.FramePacingDiagnostics`. The sample reports
whether platform polling continued, whether the renderer began/completed the
frame, the frame index, total frame microseconds, and per-phase CPU timings for
platform begin, resize/operational transitions, fixed-step work, ImGui begin/end,
editor callback and draw-data copy, pre-render setup/transform flush, selection
pick drain/readback drain, render contract phases, render-graph compile/execute,
present, maintenance, and render-world pool release. It also mirrors the ImGui
adapter's most recent draw-list/vertex/index/command counts, font-atlas
copy/reuse counters, user-texture flag, and copied font/vertex/index/command
payload byte counts so selected-editor captures can separate editor CPU time
from ImGui producer-copy volume. Renderer-owned render-graph timing and ImGui
adapter diagnostics are copied for observability only; they are not branching
contracts for runtime behavior.

Shutdown is also delegated through `Extrinsic.Core.FrameLoop`. After command
discard, Engine publishes and pumps the shutdown announcement so AssetWorkflow
cancels imports and detaches provider borrows while TextureBake rejects new
requests and detaches scene targets. The generic GPU-participant bridge then
drains retained bake work and performs any required device-idle wait before the
texture module retires cache generations, generated assets, scratch textures,
buffers, and pipeline leases. Application shutdown follows while the persistent
`AssetWorkflowModule` and `RuntimeInputActionRegistry` remain live, allowing
Sandbox to unregister its focus-action handle. Ordinary reverse module
teardown then shuts down AsyncWork and the app-composed domain modules before
world, frame graph, render-extraction/renderer, device, window, scheduler, and
initialized-state cleanup.

### Pipelined render-world pool (`GRAPHICS-036C`/`GRAPHICS-036D`)

The Engine-private `RenderExtractionService` owns the runtime-side
`Extrinsic.Runtime.RenderWorldPool`, configured during `Engine::Initialize()`
from `Core::Config::RenderConfig::SynchronousExtraction` (default `true`): one
logical buffer in the synchronous baseline, or the triple-buffered default when
pipelined extraction is requested. `RunFrame` still drives the phase order, but
it reaches the slot lifecycle through the service/cache/pool boundary:
`AcquireBack`/`PublishFront` around `ExtractAndSubmit`, `AcquireFront` for
synchronous consume, `AcquirePreviousFront` for the render-N-1 consume path, and
service-mediated `ReleaseFront` at frame retire. The pool's
`PipelineStallCount`/`ExtractionSkipCount`/`LastConsumedFrameAge` counters are
mirrored onto the service-owned last extraction stats each frame.

The renderer retains one stable `RuntimeRenderSnapshotBatch` copy per pool slot.
`SubmitRuntimeSnapshots(..., storageSlot)` clears and rewrites only the target
slot, while `ExtractRenderWorld(input, storageSlot)` exposes spans from the
slot acquired for rendering. `BeginFrame()` resets transient frame state and the
default read slot but does **not** clear retained runtime snapshot storage,
which prevents extraction-N from overwriting the data render-N is consuming from
N-1. The `integration;runtime;graphics` CPU/null proof for `GRAPHICS-036D`
exercises five frames with `RenderConfig::SynchronousExtraction = false` and
asserts that render-N sees snapshot fields from N-1, with no stalls/skips and
frame age 1 after the bootstrap frame.

The synchronous default remains behavior-preserving (no stalls/skips, frame age
0); this slice does not flip production default behavior. The pool never imports
graphics/ECS/platform — it manages only slot indices and atomics; the consumer
(`graphics/renderer`) still sees only the `const` snapshot through the
`SubmitRuntimeSnapshots`/`ExtractRenderWorld` seam, so no new dependency edge is
introduced.

## Job service integration

`RUNTIME-194` consolidated runtime work onto one surface. Asset import/decode,
queued scene document IO, deferred mesh post-processing, visualization/Htex
baking, GPU result write-back, and the model-scene progressive enrichment chain
now submit to the kernel-owned `Extrinsic.Runtime.JobService`. The superseded
executor and derived-job registry are retired. `AsyncWorkModule` publishes the
one borrowed service and owns its shutdown survivor sweep. Queue visibility is
served by the generic, read-only `JobService::SnapshotAll()`; job identity
(which entity, which output) stays with the submitting consumer rather than the
execution service.

The Sandbox editor migration window is closed. `EditorJobCommandSurface`
contains one `JobService` submit path plus active-output and per-entity row
queries. The private `EditorWorkspaceSession` keeps editor job identity — entity id,
`EditorJobScope`, output semantic, output name — in its
`JobToken -> identity` table, prunes it against `JobService::SnapshotAll()` each
frame, and joins it only when either query runs. The editor no longer owns a raw
queue snapshot or a registry adapter. `EditorJobScope` is the
editor-local successor to `DerivedJobScope` and must not be promoted to a
general vocabulary. Focused geometry operations submit through this single
surface; the workspace session owns only the token-to-identity projection.
`AssetWorkflowModule::GetAssetImportQueueSnapshot()` computes import-row
cancellation from per-token `JobService::GetState` queries; a decode is
cancellable until its result reaches the main-thread apply gate
(`JobState::AwaitingGate`), and world retirement requests cancellation that
terminalizes the visible queue record once the decode worker returns and its
unpublished finalizer runs.
`Engine` no longer exposes a frame-recorded streaming `TaskGraph` bridge.
All Sandbox K-Means requests route through
`Extrinsic.Runtime.ClusteringService::RunKMeans`. The clustering module copies
the selected geometry/property snapshot, schedules CPU-reference work as a
world-scoped job or accepts Vulkan work into its one private `JobService`
`GpuQueue` participant, and rejoins both paths at the same stale/cancellation
gate. The Engine bridge invokes the private participant inside the renderer's
open frame command context, shared transfer readback completes without an
extra swapchain present, and the module commits labels/colors and publishes
`KMeansRunCompleted` plus `ClusterLabelsChanged`. The editor workspace session
and focused geometry-operation path do not own a second queue or
backend-specific result path.
`Extrinsic.Runtime.AssetIngestStateMachine` is the promoted ingest-state
contract for manual, dropped-file, and reimport requests. `AssetWorkflowModule`
submits ingest records before route/decode/apply work, completes deferred
geometry imports from the `JobService` completion drain, and routes reimport
through same-`AssetId` `AssetService` reloads without reintroducing ECS
asset-source coupling.

`ASSETIO-005` adds the runtime-owned AssetIO queue snapshot on top of that
state machine. `Runtime.AssetIngestStateMachine` exports queue DTOs with stable
operation handles, source paths, payload kinds, asset ids when known,
enqueue/start/finish timestamps, coarse stages (`Queued`, route/decode,
main-thread apply, GPU upload, terminal states), determinate vs indeterminate
progress, and terminal diagnostics.
`AssetWorkflowModule::GetAssetImportQueueSnapshot()` polls those
records for editor/UI consumers, marks only active deferred `JobService`
imports as cancellable, and exposes
`AssetWorkflowModule::CancelAssetImport(...)` /
`AssetWorkflowModule::ClearCompletedAssetImports()` without moving asset, ECS,
graphics, or UI ownership below runtime. Sandbox manual File / Import commands
and promoted dropped geometry/model/texture requests remain queued across the
worker/apply boundary; explicit direct imports and reimports still use the same
state-machine records but reach a terminal row inside their synchronous call.
`Engine::BeginShutdown()` discards pending commands, marks the initialization
state false, and publishes/pumps `RuntimeShutdownAnnounced` before concrete app
state, GPU-participant, module-provider, renderer, or device teardown. The
`AssetWorkflowModule` listener calls
`AssetWorkflowModule::CancelActiveAssetImportsForShutdown()`, advances the
binding epoch, releases its document participant, and detaches every provider
borrow. Cancellation reuses the executor plus ingest-state path for pending,
ready, running, and decoded work waiting for main-thread apply, so those records
become `Cancelled` and their main-thread materialization cannot run after
Sandbox teardown. Interactive queue cancellation keeps its
existing pre-apply boundary; the additional decoded-work allowance is
shutdown-only.

Shutdown order requirement:

1. command discard and shutdown-announcement pump; asset imports cancel and all
   scene/document/provider borrows detach in the listener
2. Engine detaches its private renderer command hook, then shuts down
   `JobService` GPU participants with the required device-idle coordination
   while asset cache/bake state remains live
3. app-owned teardown while runtime services/worlds remain live; Sandbox
   unregisters its focus action, detaches editor state, tears down its initial
   reference population, and may release a blocked decoder
4. reverse name-sorted module shutdown:
   `AsyncWorkModule::OnShutdown()` runs before
   `AssetWorkflowModule::OnShutdown()`. Import decode no longer runs on the
   executor (`RUNTIME-194` Slice B4) — the shutdown announcement in step 1
   already cancelled active imports on `JobService` — so this ordering now
   covers only the derived-job lane. Async
   first joins and drains executor work, then
   drains derived completions/readbacks, applies readbacks already ready at the
   shutdown boundary, and cancels every surviving derived record. It then
   snapshots `JobService` and requests cancellation for every remaining
   non-terminal job; final scheduler teardown joins any worker still observing
   that request. Once AsyncWork returns, later readiness polling cannot run a
   derived main-thread callback; AssetWorkflow then withdraws exact services
   and destroys per-boot
   assets/cache/handoffs. `TextureBakeModule` withdraws its service and releases
   its already-idle GPU-owned bake state in its own reverse-order slot
5. world, frame graph, renderer, device, window, and finally task-scheduler
   teardown. Calling `Engine::Shutdown()` without a separate app-owned teardown
   invokes `BeginShutdown()` automatically.

## Stable entity lookup ownership and policy

`Extrinsic.Runtime.StableEntityLookup` is the runtime-owned home for any
`StableId -> entt::entity` resolution. `HARDEN-068` Decision 3 deliberately kept
ECS owning only the `StableId` value type, sentinel, validity check, equality,
ordering, and hasher, and deferred every lookup map to a runtime consumer so the
`ecs -> core` boundary and the "no registry-wide lookup field" rule stay intact.
Graphics never resolves stable ids; it only consumes the render id mirrored
into snapshots. The render id is `static_cast<std::uint32_t>(entt::entity) + 1`
(`StableEntityLookup::ToRenderId`, BUG-026): render id `0` is reserved for the
GPU picking background sentinel (the selection-ID targets clear to 0), so the
first entity of a fresh registry — whose raw handle casts to 0 — stays
pickable, and `entt::null` wraps to the same `0` "no entity" value.

Ownership rules:

- ECS holds **no** lookup map. The sidecar lives only in `runtime`.
- Graphics holds **no** stable-id resolution. It reports render ids; runtime
  resolves them.
- The render/extraction stable id is a reversible encoding of the live handle
  (index + version), so render-id resolution decodes and validates against the
  registry rather than storing a parallel container. The `StableId` map is the
  only materialised state, because a durable id is independent of the volatile
  handle.

Duplicate / stale policy:

- **Duplicate `StableId`:** keep a single deterministic winner — the live entity
  with the smallest `ToRenderId` (entt value) wins, independent of insertion or
  registry-iteration order, and each duplicate occurrence bumps
  `DuplicateStableIds`. Losing duplicates are not retained; a `Rebuild`
  re-derives winners, and `Forget` of a winner drops the mapping until the next
  `Rebuild`/`Track`. The sentinel `kInvalidStableId` is never tracked.
- **Stale handle:** a winner whose entity is destroyed without a matching
  `Forget` is detected lazily on resolve (rejected, erased, counted as
  `StaleEntityResolves` + `StaleEntriesPruned`) or in bulk by `PruneStale`.
- **Missing id:** a resolve for an untracked or sentinel id returns `nullopt`
  and bumps `MissingStableIdLookups`.
- **Reassignment:** when an entity's `StableId` component is replaced and the
  sidecar is updated incrementally with `Track` (hot-reload / undo / editor
  reassignment, rather than a full `Rebuild`), `Track` first drops the entity's
  prior winner entry so the old durable id stops resolving to it and a later
  `Forget` does not leak a stale entry.

Frame wiring (`RUNTIME-092`, optimized by `RUNTIME-145`, owned by
`RUNTIME-188`): `SceneInteractionModule` owns a `StableEntityLookup` and
`StableEntityLookupSceneBinding`, attaches the lookup to its published
`SelectionController`, and subscribes to the bound registry's `StableId`
construct/update/destroy events. Construct/update calls `Track(scene, entity)`
and destroy calls `Forget(entity)`, so
`ResolveByStableId`/`ResolveSelected` observe the current entity set without a
per-frame full map rebuild. Whole-scene replacement is the exception: the
module's strong document participant disconnects the hooks before
`SceneDocumentModule` clears/swaps the raw registry, then reconnects and calls
`Rebuild(scene)` against the rebound registry. New/close reconnect on an empty
registry; Load rebuilds durable ids without document ownership of the lookup.
Active-world mismatch performs the same reset/rebind before any public resolve
and does not retain a per-world map.
The controller's render-id resolution seam (`ConsumeHit`,
`SetSelectedByStableEntityId`) routes through the attached lookup's
`ResolveByRenderId` — which decodes the handle *and* validates it against the
live registry — so a pick readback naming a recycled/destroyed slot is rejected
by the single runtime-owned authority (counted as `StaleEntityHits` on the
controller and `StaleEntityResolves` on the lookup) instead of mis-resolving to
the recycled occupant. The seam is opt-in: with no lookup attached (the
controller's standalone unit/contract use) resolution falls back to the bare
`ToEntityHandle` decode plus the controller's own validity check, so existing
direct-drive callers are unaffected. The controller does not own the lookup's
lifetime.

Sandbox default (`RUNTIME-092` Slice B decision): reference-scene entities remain
transient and receive no generated `StableId`; durable ids are emplaced only by
serializer / undo / prefab / editor consumers, matching the `StableId` contract
that transient entities skip the 16-byte cost. The render-id resolution path the
sandbox selection uses needs no `StableId` (it decodes + validates the live
handle), so leaving reference-scene entities transient keeps the default path
allocation-free; durable `StableId` assignment lands with the first serializer /
editor consumer that needs cross-recycle references.

## Dependency direction

`Runtime` depends on `Core`, `Assets`, `ECS`, `Platform`, and `Graphics`.
It orchestrates but does not own GPU barrier/resource policy or render-pass
branching logic.

`Extrinsic.Runtime.RenderExtraction` is the only promoted runtime owner for live
ECS render queries. It stores entity-to-graphics sidecars outside canonical ECS
components, allocates/frees `GpuWorld` instance handles through the renderer,
builds transform/light/visualization/gizmo snapshot records, and submits those
records to graphics through `IRenderer::SubmitRuntimeSnapshots()`. It also owns
the `GRAPHICS-023C` asset-generation observation bridge: when a renderable has
`AssetInstance::Source`, extraction can compare the normalized `Assets::AssetId`
with a supplied `Graphics::GpuAssetCache` view and `GpuSceneSlot` metadata to
report pending/up-to-date/rebind-required states without performing the later
GPU geometry or material rebind. The Engine-private `RenderExtractionService`
owns the live cache instance, pool, last stats, and frame-index counter that
`Engine` composes into the frame loop; it does not change the extraction
algorithms or renderer-owned resource policy.

`RUNTIME-183` removes the former Engine-private asset-residency declaration and
its include-only header. The cache/listener and import implementation are folded
into `AssetWorkflowModule`'s private PImpl; `RUNTIME-190` independently composes
texture-bake lifetime in `TextureBakeModule`. The Engine interface and
implementation therefore carry no asset-domain owner, import, bake facade, or
transition.
`RUNTIME-182` deletes the former private
`ImGuiEditorBridge`; the app-composed `EditorUiModule` now owns that optional
lifetime without an Engine member or facade. `RenderExtractionService` stays
by value because the Engine API independently requires its public extraction
and render-world-pool types. During module-service registration, Engine
provides that service's existing `RenderExtractionCache` instance as a borrowed
`ServiceRegistry` entry. Sandbox UV-view composition resolves the stable cache
once when it builds an attached editor context and queries geometry/material
availability directly; copied command surfaces remain bounded by the existing
attachment-epoch guard, and a missing cache fails closed to the CPU-layout
fallback. No service BMI, wrapper, or additional Engine facade is introduced.

For direct mesh imports that stay on the runtime-authored `GeometrySources`
residency lane, extraction also accepts data-only
`Graphics::MaterialTextureAssetBindings` keyed by stable render id. The default
recipe postprocess stage resolves finite UVs/normals on the worker lane, publishes
the mesh and a property-buffer presentation target on main-thread apply, and
submits the same canonical `PropertyTextureBakeRequest` used by model-scene and
editor callers. If promoted Vulkan is still crossing its cold-start gate, one
bounded world-scoped `JobService` continuation parks the caller request until
the service becomes operational; it never blocks mesh publication or selects a
CPU texture path.

`TextureBakeModule` allocates each output through the live `AssetService`,
using deterministic metadata paths with collision probes rather than
fabricating an `AssetId`. Before recording and completion it revalidates exact
property, UV, topology, world, entity, output, cache-generation, and live
`GpuGeometryResidencyView` identity. Commands record through its single
`JobService` GPU participant inside the renderer's open frame command context;
the bake never begins or presents a second frame. Padded work uses retained,
extent-keyed dilation scratch under fixed entry/byte limits and serializes one
submission per frame.

Until exact readiness, callers retain authored texture, property-buffer, or
vertex-normal fallback. `AssetWorkflowModule` observes the output catalog and
applies one atomic material/presentation snapshot per entity, merging only the
declared target channel and preserving unrelated channels. Stale, failed, or
removed outputs clear only their prior generated target. Scene replacement and
shutdown keep recorded work visible to the generic device-idle boundary before
frame-safe retirement. No selected-mesh, CPU mesh-attribute, or specialized
object-normal bake service remains.

The authored geometry-presentation recipe and its runtime status sidecar are
consumed during the same extraction pass as data-only descriptors.
`BuildGeometryPresentationSnapshot(...)` resolves copied slot state against the
current `GeometrySources` view and folds counters onto
`RuntimeRenderExtractionStats`: entity/slot counts, default slots, pending slots,
ready texture slots, ready property buffers, unsupported slots, diagnostics, and
previous-output retention. Ready authored/generated mesh surface texture slots
are exposed as material texture binding requests keyed by stable render id;
pending, failed, unsupported, or previous-output-retained slots report
diagnostics without blocking geometry residency. Graph and point-cloud
properties remain direct property-buffer candidates in the snapshot model, with
separate graph vertex/edge domains and point-cloud color/scalar/size/normal
domains; unsupported mesh surface direct property buffers fail closed until a
future renderer-owned data path exists.

`GRAPHICS-034` records the future asset-backed mesh residency bridge that
extends that observation-only seam. Runtime will normalize
`ECS::Components::AssetInstance::Source` through a named
`Runtime::NormalizeAssetInstanceId(...)` helper, store residency in a separate
`Runtime::AssetGeometryCache` keyed by `Assets::AssetId`, and prefer an attached
asset source over procedural / `GeometrySources` residency for the same
renderable. The cache state machine is `NotRequested -> CpuPending ->
GpuUploading -> Ready/Failed`; `Ready` entries bind shared `GpuGeometryHandle`
values through `GpuWorld::SetInstanceGeometry`, generation rebinds acknowledge
GRAPHICS-023D only after a successful replacement bind, and refcount-zero
entries retire through a future asset-geometry maintenance tick adjacent to the
existing asset and unified geometry maintenance hooks. Failed assets use a
visible missing-mesh placeholder plus the GRAPHICS-031 default debug material
once the implementation child lands; until then the bridge is not implemented.

### Unified geometry upload and residency (`RUNTIME-197`)

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

RUNTIME-139 removed the dormant alternate-layout hint and plan. Runtime plan
builders now copy channel bytes, update class/mask, bounds, and identity
directly into the sole implemented uniform-SoA residency contract. The live
`GpuGeometryResidencyView` reports exact channel descriptors rather than a
fixed storage-lane field.

`FindRenderableSidecarForTest(stableId)` exposes handles and boolean residency
facts for procedural, mesh, graph, point-cloud, mesh-edge, and mesh-vertex lanes
without exposing coordinator keys, plan-builder scratch, reference counts, or
retire queues. Payload/layout parity is asserted through
`GpuWorld::TryGetGeometryResidencyView(...)` fingerprints and counts; lifecycle
contracts target `GeometryResidencyCoordinator` directly.

`ImGuiAdapterDiagnostics` exposes the GRAPHICS-114 overlay-transport byte
counters alongside the existing draw-list counts: `LastFrameFontAtlasCopyBytes`
is non-zero only when the atlas bytes are recopied into an overlay frame, while
`LastFrameVertexCopyBytes`, `LastFrameIndexCopyBytes`,
`LastFrameCommandCopyBytes`, and `LastFrameOverlayCopyBytes` report the POD
vertex/index/command payload copied for the current submitted frame. These
counters are runtime-side observability only; graphics still owns retained atlas
resources and upload buffers. UI-030 extends the same diagnostics with
microsecond timings for `BeginFrame`, the editor callback, `ImGui::Render()`,
draw-data/font/list copying, and total `EndFrame()` so runtime frame-pacing
samples can separate editor CPU work from ImGui producer-copy cost.

Runtime owns camera motion, input-to-pick-request translation, gizmo hit testing,
and transform application. Graphics receives only immutable `CameraViewInput`,
`PickPixelRequest`, and transform-gizmo render packets during extraction.

## Camera controller baseline

`EngineConfig::Camera` selects the app-composed `CameraModule` main camera
controller. The default is enabled `Orbit`; `Fly`, `FreeLook`, and `TopDown`
are also implemented and constructible through `CreateCameraController()`.
The module publishes the exact `CameraControllerRegistry` and registers one
typed viewport-input hook. Engine dispatches that hook after `UiEndCapture`
and `RenderFrameInput` initialization. Deterministic module-name ordering runs
`Runtime.CameraModule` before `Runtime.SceneInteractionModule`, so the latter
receives the populated camera and completed capture before gizmo/pick input.
The camera hook lazily registers the selected controller
in `CameraControllerSlot::Main`, uses the current world's optional seed, calls
`controller->Update(input, dt)` when viewport input is not captured, and
copies `controller->GetView(viewport)` into `RenderFrameInput::Camera`. If no
seed exists, the controller falls back to the deterministic default
perspective camera at `(0, 0, 4)` looking down `-Z`. If `CameraModule` is
omitted, no registry or fallback camera is produced.

The default editor controls are deliberately simple and backend-neutral:
right- or middle-mouse drag rotates orbit/fly/free-look cameras, `WASD` moves or
pans according to the active controller, `Shift` accelerates movement, and mouse
wheel zooms orbit/top-down cameras. The sandbox `Camera / Render` window mirrors
these bindings so controller replacement buttons are not the only visible UI.
The sandbox default `F` focus-on-selection binding is app-owned and constructed
with the generic camera feature factory only when Sandbox resolves both the
exact camera registry and exact selection controller; generic input actions
remain operational when either optional service is omitted.
Viewport left-click selection is routed by `SceneInteractionModule` from the
runtime input context into its published
`SelectionController::RequestClickPick(...)`; it is suppressed while ImGui or
an active gizmo owns the mouse. The production input context is updated by the
active `Platform::IWindow` backend before the typed viewport hooks run.

Camera-controller registration and replacement are one-shot camera-transition
events. `CameraControllerRegistry` marks the slot as pending, and
`CameraModule` consumes that bit into
`CameraViewInput::ExplicitCameraTransition` on the first extracted frame after
the change. Graphics uses the immutable flag, plus its own consecutive-camera
delta thresholds, to skip stale previous-frame HZB occlusion without reading
live runtime or ECS state.

The registry is bound to exactly one valid `WorldHandle`. Every
`ResetForWorld` clears all controller slots, poses, pending transitions, and
the seed before binding the supplied handle, including when its bits match the
previous handle. Active-world change and destruction events reset that state,
and the hook repeats the handle comparison before any config/seed/controller
read so delayed event delivery cannot expose stale state. Sandbox passes the
plain reference-population seed to the initial owning world when both content
and the camera registry are present. After lazy controller construction,
controller state is authoritative and graphics receives only immutable
`CameraViewInput`.

Known gaps relative to legacy and planned camera work are tracked in
`tasks/archive/RUNTIME-081A-camera-legacy-gap-analysis.md`: editor-specific camera
shortcuts and any policy that renders multiple camera outputs in one frame remain
outside this runtime-controller surface. Transform-gizmo hit testing,
translate/rotate/scale drag application, and the generation-validated
`EditorCommandHistory` batch commit live in
`Extrinsic.Runtime.GizmoInteraction`. `SceneInteractionModule` directly owns
the default input binding, selected-entity scratch, selection-click interlock,
and extraction packet submission. Graphics consumes only copied
`TransformGizmoRenderPacket` spans.

## Geometry-property vocabularies (RUNTIME-192)

`Extrinsic.Runtime.GeometryAvailability` owns the one way to *name* a geometry
property. The progressive, editor-visualization, editor-catalog, and bake
modules previously each mirrored a subset of it, which forced conversion
switches and let identical property identity drift between consumers; those
duplicates are retired and
`RuntimeEngineLayering.NoDuplicateGeometryPropertyVocabularyRemains` keeps them
from reappearing.

Vocabularies that stay deliberately separate, because they answer different
questions:

| Vocabulary | Question it answers |
| --- | --- |
| `Runtime::GeometryElementDomain` | Which element domain does this property live on? |
| `Geometry::PropertyValueKind` | What is the property's value type? |
| `GeometryPropertyValueKindFilter` | What value type is *acceptable* here? (`nullopt` = any) |
| `GeometrySources::Domain` | Where did this geometry come from (provenance)? |
| `Runtime::GeometryRenderLane` | Which render lane consumes it? |
| `Runtime::VertexChannel` | Which structural vertex stream? |
| `Graphics::MaterialChannel` | Which material slot? |

One carries a non-obvious constraint:

- **The persisted scene format uses legacy spellings.** Property value kinds
  persist as `ScalarFloat`/`ScalarDouble` (not the canonical `Float`/`Double`),
  domains persist as `GraphVertex`/`Point` (not `GraphNode`/`PointCloudPoint`),
  and a legacy `MeshSurface` domain is accepted on read and mapped to `Unknown`.
  That mapping is owned by `Runtime.SceneSerialization` and must never be
  derived from `DebugNameForGeometryPropertyValueKind`; regression tests in
  `Test.RuntimeSceneSerialization.cpp` pin the strings and prove the reader
  rejects canonical names.
