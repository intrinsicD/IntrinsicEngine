# Sandbox editor feature boundaries

RUNTIME-202 retired the all-feature Sandbox runtime facade and its shared
Sandbox config/default-policy surface. Runtime now publishes focused workspace,
job, scene, geometry, visualization, render-recipe, and feature-config modules.
The Sandbox app owns window/menu/ImGui state, config-registration and default
aggregation, plus the copied `SandboxEditorContext` and `SandboxEditorFrame`
records in the app-private `Sandbox.PanelSupport.hpp`. Window registration
and lifecycle stay in `Extrinsic.Sandbox.Editor.Shell`; all three panel-family
interfaces expose registration only and use `Sandbox.EditorFwd.hpp`. Drawing
implementations and their integration tests include the shared support header
after their explicit runtime imports. These private definitions use C++ linkage
so named module implementation units can share them without serializing an
all-panel module interface. Standard/GLM includes stay in each unit's global
module fragment. The shell and panel registration interfaces do not include
these complete views.

Prepared-frame command/query handles carry the workspace attachment epoch.
Retaining one beyond detach is observable as unbound, and every operation
fails closed before reaching the copied service pointers; callback surfaces
retain their operation-specific expired-attachment diagnostics. Mutating
feature contexts receive only an epoch-guarded cache-invalidation callback,
not the workspace cache object, so scene/geometry/visualization operations
preserve selected-model cache behavior without widening their public owner
boundaries.

`Runtime.EditorFeatures.Internal.hpp` holds private workspace bindings and context
adapters. It includes `Runtime.EditorFeatureCommands.Internal.hpp` for import/file
prerequisites, diagnostics and render-hint comparisons, and
`Runtime.EditorFeatureProperties.Internal.hpp` for property catalogs and model
statistics. Action implementations include these declarations directly, without
the workspace storage. Scene actions need only the command helpers; visualization
actions also use the property helpers. Their definitions remain compiled once in
`Runtime.EditorFeatureContextAdapters.cpp`. Geometry operations use the smaller
`Runtime.EditorGeometryHelpers.hpp` for entity signatures and undo helpers.
These headers contain no module imports; each implementation imports the types
it uses. Helpers retain C++ linkage across their owning operation units.
`EditorCompilationLocality.Actions` excludes unrelated processing services,
render-recipe editing, renderer and workspace snapshots from both action units.
Scene primitive-view history and visualization render-hint history retain separate
state because only visualization history owns surface visualization settings. The feature config codecs and
workspace attachment retain private module interfaces. Production app sources
may not import `Extrinsic.Runtime.Private.*` or include runtime-private headers.

Physical implementation ownership follows the feature split. Geometry operation
bodies share source-snapshot, stored-topology-fingerprint and publication helpers
through the private `Runtime.GeometryProcessingOperations.MeshSupport.hpp` and its
one ordinary compiled owner `MeshSupport.cpp`, which imports no family module and
no broad processing module. The family-neutral parts of that owner — the queued
job envelope, the active-job lookup and message, selected-model cache
invalidation, finite-position collection and the unpublished-job reason — are
declared in `PointFields.hpp` instead, which `MeshSupport.hpp` includes. That
keeps point-set families off the by-value halfedge-mesh and mesh-soup snapshots
in the mesh header. Mesh topology, mesh fields, registration and
UV/parameterization each own a public family module. Workspace model assembly lives in
`Runtime.EditorWorkspaceSnapshots.Models.cpp`. The workspace session stores its
attachment epoch, retained results, caches and subscriptions behind its private
`Impl`, defined in `Runtime.EditorWorkspaceSession.cpp`. Its small attachment
interface exposes lifecycle and prepared-frame visitation, not storage layouts.
Feature command handles construct their expired context directly, retaining the
same guarded callbacks and clearing the same borrowed live state.

`runtime.editor-prepared-frame-locality` keeps workspace snapshot dependencies
out of context-only composition. `EditorSelectedModelCache`,
`EditorWorkspaceSnapshotRequest`, `EditorWorkspaceSnapshot` and
`EditorWorkspaceSnapshotContext` keep their sole definitions in
`Runtime.EditorWorkspaceSnapshots.cppm`; the private attachment interface and
`Runtime.EditorFeatures.Internal.hpp` borrow them by pointer/reference through matching
C++-linkage declarations instead of importing the owner. Cache clear/statistics
methods compile in `Runtime.EditorWorkspaceSnapshots.Public.cpp` with matching
C++ linkage; the interface retains their declarations and unchanged storage.
The attachment interface names the
payload kind from its canonical owner `Extrinsic.Asset.ImportRouter`, not through
the scene-operation alias. Private `PrepareFrame` requires all four arguments.
The epoch guards, `IsAttached` and the visitor keep the
prepared-frame lifetime seam: prepared references stay valid only for the visitor
invocation. Public `PrepareEditorWorkspaceSnapshotFrame` defaults are unchanged.
A unit that reads a snapshot member or value imports the owning module; the
sixteen operation units that compose from context alone no longer do. Units
needing `EditorWorkspaceSnapshotStats` take it from `Runtime.EditorCommon`.

The private attachment interface also owns `EditorFeatureResultBindings`, a
pointer-only record of family sinks/results, the UV surface and a family-owned
`EditorPointCloudServiceBorrowedServices` record. It contains no complete family
values and imports none of their modules. `EditorWorkspacePreparedFrame` borrows
that record, the workspace snapshot, the opaque broad editor bindings and the
processing context. The sole `EditorProcessingContext` definition stays in its
existing owner with matching C++ linkage. The session constructs it after epoch
guards and config/job callbacks are installed; the snapshot builder and eleven
processing-frame leaves and workspace query preparation reuse it. Command handles
copy it into owned storage.

The eleven leaves import the private attachment plus their own implicit family
owner, without the broad `Runtime.EditorFeatures.Internal.hpp`. Each leaf copies
only its family's results and commands. Session preparation/reset clears the
cached context and borrow records before replacing session storage. Scene-frame
preparation copies retained import/file results through its existing scene
context pointers, valid during the visit. `EditorCompilationLocality.ProcessingFrames`
checks the actual compiler dependency closure for every leaf; broad scene,
visualization, render-recipe, renderer, texture-bake and snapshot imports are
forbidden there. `.ProcessingFamilies` additionally forbids the broad geometry-
processing module in the ten family leaves.

Scene, visualization and render-recipe command preparation call the existing
`MakeEditor*Context` adapters through declarations in the private attachment
interface. Their sole context definitions remain in their respective feature
modules with matching C++ linkage. These three leaves import only their own
feature and the private attachment; the attachment never imports their complete
contexts. Scene history callbacks take the command history and epoch guard from
the scene context. Construction timing stays unchanged: visualization contexts
receive the statistics pointer installed after workspace snapshot construction.
`EditorCompilationLocality.CommandFrames` excludes unrelated processing, renderer,
method services and snapshot modules; `.SceneFrame`, `.VisualizationFrame` and
`.RenderRecipeFrame` additionally exclude the other two feature modules. Shared
conversion implementations remain in `Runtime.EditorFeatureContextAdapters.cpp`;
the broad private header retains matching declarations for its existing consumers.

Clustering and consolidation records have canonical owners in
`Runtime.ClusteringTypes` and `Runtime.PointCloudConsolidationTypes`; their
service modules re-export those contracts. `Runtime.ClusteringConfig` imports
only the clustering value contract. `Runtime.ModuleLifecycle` provides the
minimal lifecycle interface; complete setup/frame/recipe capabilities remain in
`Runtime.Module` for implementation and composition callers. The shared UV
inspection record belongs to `Runtime.EditorCommon`, so appearance models do
not import the geometry-processing operation interface.

Density, spacing, density weights, keypoints and outlier analysis share live
position/deletion capture and same-domain output preflight in
`Runtime.GeometryProcessingOperations.PointProperties.cpp`. `CapturePointInput`
resolves the domain, watches positions and deletion storage, skips deleted rows,
and preserves ascending source-row IDs without copying values during readiness
or catalog queries. `ValidatePointOutputs` checks the resolved output domain,
reserved names and existing storage against the validated typed config.

Density, spacing and density weights share scalar history publication, including
stale-storage guards, render dirty notifications and workspace invalidation on
apply, undo and redo. Density and spacing also share framed fixed-width kNN
pagination; radius pagination retains its separate support-membership contract.
Both live in `RadiusRows.hpp`, so only the units that actually page neighbours
name the spatial-index cache.
`BuildPointInputCatalog` reuses finite live-row capture for weights, keypoints,
outliers, descriptors, construction and normals, independently of method result records.
`RadiusRows.cpp` also compiles once outside either family and preserves complete
radius support and its explicit lowest-ID limit.
The private `PointFields.hpp` and `RadiusRows.hpp` declarations have C++ linkage across module
owners; the common implementation is compiled once as an ordinary translation
unit, without the broad method module or mesh-topology imports. Typed configs/results, numerical kernels, sample minima,
backend gates and job completion delivery remain with method adapters. Keypoint
mask/score publication and outlier provenance/removal transactions remain explicit.

The nine point-processing panels use `ProcessingDraftState` and `PanelSupport`
for entity/input/output selection. `DrawProcessingPointInput` accepts an optional
domain restriction where topology requires it, such as mesh face normals taking
vertex positions. The panels update coupled input/output domains explicitly.
`DrawProcessingExecution`, local to `MeshProcessingPanels.cpp`, owns the common
edit/validate/reapply/run sequence for density, spacing, keypoints, descriptors,
density weights and bilateral filtering. Outlier detection/removal, normal
estimation and construction retain their different request sequences. Their
algorithm controls and statistics remain explicit; compatible Show actions use
`ShowProcessingProperty`, while face-normal display retains its face-lane path.

The session retains separate typed result values: density/spacing live in
`EditorPointFieldResultsSnapshot`, weights/keypoints/outliers live in
`EditorPointAnalysisResultsSnapshot`, normals live in `EditorNormalResultsSnapshot`,
registration lives in `EditorRegistrationResultsSnapshot`, UV regeneration and
parameterization live in `EditorParameterizationResultsSnapshot`, descriptors
join `EditorPointAnalysisResultsSnapshot`, bilateral filtering and progressive
Poisson live in `EditorPointSetResultsSnapshot`, construction lives in
`EditorPointConstructionResultsSnapshot`, and the two service-queued runs live in
`EditorPointCloudServiceResultsSnapshot`. No record holds every method's results.
Each family prepares copied results
through its own entry point. Session composition holds their storage privately;
the general workspace interface does not import the point or normal families.
Retained frames stay independent when results are replaced or dismissed. Typed
sinks use the existing attachment epoch guard, and dismissal clears both panel
and session state. Family command handles guard borrowed services and queued
completion delivery before accessing an expired attachment. A queued job that
never reaches its publisher — cancelled, stale, or dropped — still delivers one
terminal result through its unpublished finalizer, and a publisher that already
answered the sink suppresses that finalizer so no submission delivers twice. A
request that observes an already active job for the same entity and output
registers no callback at all, and an immediate outcome on a session without a
job lane is returned rather than delivered. Apply gates test the attachment
epoch before reading the borrowed scene or spatial cache, so a detached job
fails closed instead of dereferencing freed state; finalizers read only their
own job state and publish no output, history or cache invalidation.

The UV-regeneration controls have one implementation,
`DrawSandboxUvRegenerationControls` in the app-private `Sandbox.PanelSupport.*`.
It owns the atlas parameters, the submission that pairs
`Parameterization.Commands` with the session's `ResultSinks.UvRegeneration`
terminal callback, the once-per-result atlas-extent adoption into the bake
width/height, the status readout and the dismissal that clears panel state and
the session slot together. The domain-panel and shell texture-bake panels both
call it and neither submits the command itself, so panel state lives for the
panel's lifetime rather than the per-frame `SandboxEditorContext` copy.

For canonical-helper discovery before adding a mechanism, use
[`intrinsicengine-reuse`](../../tools/agents/skills/intrinsicengine-reuse/SKILL.md).

## Processing compilation locality

`Extrinsic.Runtime.EditorProcessing` owns one shared execution context and
attachment-checked command handle, independent of method configs, results and
workspace storage. `Extrinsic.Runtime.PointFieldOperations` owns density and
spacing contracts, validated config operations and typed result sinks.
`Extrinsic.Runtime.PointAnalysisOperations` owns weights, keypoints, outliers and
FPFH descriptors: one family because all four resolve a feature scale, page
radius or kNN support through the shared row helpers, and publish named
same-domain scalars in one undoable transaction.
`Extrinsic.Runtime.PointSetOperations` owns bilateral filtering and progressive
Poisson ordering, which share the shared-context-only dependency, a CPU/GPU
backend pair with truthful requested/actual/fallback reporting and same-domain
publication without topology change. Progressive Poisson uses the canonical
`ProgressivePoissonPlaygroundConfig` directly; its serialized `double` knobs
narrow to `float` once, at the sampler and GPU parameter boundary, which is where
the previous editor-owned duplicate narrowed them.
`Extrinsic.Runtime.PointConstructionOperations` owns Hoppe surface reconstruction
and kNN graph building; it is separate because it creates an owning entity with
its own sources, assets, selection and undo rather than publishing onto the input
domain. `Extrinsic.Runtime.PointCloudServiceOperations` owns K-Means and
point-set consolidation dispatch: the editor validates, applies config and queues
a correlated run, and the owning service delivers completion through its own
subscription. Its prepared frame borrows the two service pointers beside the
generic command handle, and every entry point validates the handle's attachment
before dereferencing a service, so the generic execution context needs no
service import and a frame copied past detach reports unavailable.
`Extrinsic.Runtime.NormalOperations` owns point, graph and mesh normal contracts.
`Extrinsic.Runtime.RegistrationOperations` owns ICP alignment, its source/target
selection and its narrower input catalog. `Extrinsic.Runtime.ParameterizationOperations`
owns UV regeneration, surface parameterization and the UV view request/state.
`Extrinsic.Runtime.MeshFieldOperations` owns curvature, curvature segmentation and
geodesic distance: fields published onto the mesh that produced them, never a
topology replacement. `Extrinsic.Runtime.MeshTopologyOperations` owns denoise,
remesh, subdivide and simplify, which share one scratch-mesh source, one UV
preservation/discard contract and one replacement commit; `EditorMeshTexcoordOutcome`
belongs to that family because it reports what a replacement did to the UVs.
Host-declared mesh kernel availability is plain data on the shared
`EditorProcessingContext`, not a capability registry: the mesh families gate their
fail-closed branches on it and `EditorGeometryProcessingModel` projects the same
values into panel availability.
The UV view command surface is a parameterization-family surface: the session owns
one guarded instance, shared private bindings borrow it as an incomplete pointer,
and the family prepared frame copies it. Submitting and disabling the view take
that copied surface directly instead of a general command framework.
The configured normal operation also handles canonical `v:normal` publication;
there is no second provenance-specific normal command/job/history path.
`MeshSources.cpp` compiles shared face-ring validation and mesh normal snapshots
without any processing-family or workspace interface dependency.
The families share compiled config validation/application through
`ApplyEditorProcessingConfig` and the generic execution context, including the
selection controller used by outlier removal. Their
numerical adapters and shared capture owner must not import
`GeometryProcessingOperations`, `EditorWorkspaceSnapshots`, or private workspace
attachment composition, directly or transitively. Unrelated numerical adapters,
scene/visualization/recipe command units and workspace model builders must not
import a processing family; the families also stay independent of each other.
The context adapters are a composition unit like the session: they import the
parameterization family only to construct the one UV view surface the session
owns and guards. This is the
`runtime.processing-compilation-locality` contract.

Each family's `Runtime.*Operations.Frame.cpp` is its composition leaf: it
reads session bindings and returns copied family results plus guarded commands.
The app calls it beside the other prepared-frame functions. Only that leaf may
consume private workspace composition; the family interface and numerical/config
implementations remain independent. A family-record edit can therefore rebuild
session and panel composition without invalidating other algorithm adapters.
Private shared bindings borrow forward-declared sink/result containers; their
definitions have C++ linkage and remain in the family module. The session owns
both containers. Only the family frame leaf dereferences those pointers while
visiting the prepared frame, then copies the values into its returned record.
Do not place complete family records in shared private bindings: that would
make sibling feature implementations import the family again.

`ProcessingCompilationLocality.Family`, `.PointAnalysis`, `.PointAnalysisTests`,
`.Normals`, `.NormalTests`, `.MeshSupport`, `.Registration`, `.Parameterization`,
`.RegistrationTests`, `.MeshField`, `.MeshTopology`, `.MeshFieldTests`,
`.PointSet`, `.PointSetTests`, `.PointConstruction`, `.PointConstructionTests`,
`.PointCloudService`, `.Discovery` and `.UnrelatedAdapters` inspect actual
Clang scanner requirements and CMake module closures through
`tools/analysis/compile_hotspots.py`. Missing, ambiguous or stale source scans
fail closed. Run them after building their producers. These checks enforce
module dependencies, not timing budgets or a whole-engine impact database;
CI-014 remains the owner of generic build-impact selection. Matched content-edit
probes and their limitations are recorded by
[RUNTIME-233](../../tasks/active/RUNTIME-233-processing-compilation-locality-pilot.md)
[RUNTIME-234](../../tasks/active/RUNTIME-234-point-analysis-compilation-locality.md)
and [RUNTIME-235](../../tasks/active/RUNTIME-235-mesh-processing-compilation-locality.md).
Further families use this path when they have a coherent contract; they do not
need a context, handle or library target for each individual method.

The operator confirms no external C++ API consumers or persisted scene/config
data require compatibility. The migration therefore replaces public APIs and
engine-owned formats directly, updates in-tree consumers together, and removes
superseded representations without compatibility adapters. Current-format
round-tripping, implemented features and the Framework24 product baseline
remain required. Compilation-locality closure and product feature-completeness
closure are separate: completing the pilot cannot retire the product gate.

## Public ownership map

| Area | Current owner |
|---|---|
| Workspace attachment lifecycle | `Extrinsic.Runtime.EditorWorkspaceAttachment` |
| Copied workspace snapshots and queries | `Extrinsic.Runtime.EditorWorkspaceSnapshots` |
| Job identity/progress projection | `Extrinsic.Runtime.EditorJobProjection` |
| Selection, import, scene-file, transform, camera, primitive-view operations | `Extrinsic.Runtime.SceneEditingOperations` |
| Shared processing execution context and guarded commands | `Extrinsic.Runtime.EditorProcessing` |
| Density and spacing commands, configs and copied results | `Extrinsic.Runtime.PointFieldOperations` |
| Weights, keypoints and outlier commands, configs and copied results | `Extrinsic.Runtime.PointAnalysisOperations` |
| Normal commands, config and copied results | `Extrinsic.Runtime.NormalOperations` |
| ICP registration commands, config, input catalog and copied results | `Extrinsic.Runtime.RegistrationOperations` |
| UV regeneration, parameterization, UV view surface, view model and copied results | `Extrinsic.Runtime.ParameterizationOperations` |
| Curvature, curvature segmentation, geodesics commands, configs and copied results | `Extrinsic.Runtime.MeshFieldOperations` |
| Denoise, remesh, subdivide, simplify commands, UV outcome and copied results | `Extrinsic.Runtime.MeshTopologyOperations` |
| Bilateral filtering and progressive Poisson commands, configs and copied results | `Extrinsic.Runtime.PointSetOperations` |
| Point construction commands, config and copied results | `Extrinsic.Runtime.PointConstructionOperations` |
| K-Means and point-set consolidation dispatch, availability and copied results | `Extrinsic.Runtime.PointCloudServiceOperations` |
| Processing discovery, menus, capabilities, algorithm entries, panel availability model and primitive selection | `Extrinsic.Runtime.GeometryProcessingOperations` |
| Property, presentation, binding, spatial-debug, visualization operations | `Extrinsic.Runtime.VisualizationEditingOperations` |
| Frame-graph, recipe, profiling, artifact operations | `Extrinsic.Runtime.RenderRecipeEditingOperations` |
| Clustering config schema/codec | `Extrinsic.Runtime.ClusteringConfig` |
| Progressive-Poisson config schema/codec | `Extrinsic.Runtime.ProgressivePoissonConfig` |
| Parameterization config schema/codec | `Extrinsic.Runtime.ParameterizationConfig` |
| Point-cloud consolidation config schema/codec | `Extrinsic.Runtime.PointCloudConsolidationConfig` |
| Sandbox context/frame, windows, defaults, registration aggregation | `Extrinsic.Sandbox.Editor.Shell`, app-private `Sandbox.PanelSupport.hpp`, `Extrinsic.Sandbox.ConfigSections`, and `Extrinsic.Sandbox` |

## Workflow verification matrix

| Workflow | Owning path | Coverage |
|---|---|---|
| Workspace hierarchy, inspector, selection, cache invalidation | workspace snapshot + scene operations | `Test.SandboxEditorModels.cpp`, `Test.RuntimeReferenceScene.cpp` |
| File import, reimport, queue state, scene save/load/new/close | scene operations + `AssetWorkflowModule` / `SceneDocumentModule` | `Test.SandboxEditorSceneCommands.cpp`, `Test.AssetImportFormatCoverage.cpp` |
| Clustering submit, pending/ready/failure, stale completion | `ClusteringService` + geometry operations + job projection | `Test.SandboxEditorClusteringMethods.cpp`, `Test.SandboxEditorSessionLifecycle.cpp` |
| Denoise, curvature, remesh, subdivide, simplify, normals, outliers, registration | geometry operations | `Test.SandboxEditorMeshMethods.cpp`, `Test.SandboxEditorClusteringMethods.cpp` |
| Parameterization config, execution, undo, diagnostics, CPU/GPU view fallback | geometry operations + parameterization config | `Test.ParameterizationOperations.cpp`, `Test.SandboxParameterizationPanel.cpp` |
| Point-cloud consolidation config, async execution, stale gate, result projection, and undo | geometry operations + consolidation service/config | `Test.PointCloudConsolidationModule.cpp`, `Test.SandboxPointCloudConsolidationPanel.cpp` |
| Property catalog, bindings, texture bake, presentation, visualization recipes | visualization operations | `Test.SandboxEditorVisualization.cpp`, `Test.TextureBakeModule.cpp` |
| Frame graph, profiling, render-recipe preview/apply, artifact state | render-recipe operations + config control | `Test.RuntimeRenderRecipeActivation.cpp`, `Test.RuntimeConfigControl.cpp` |
| App-owned context/frame, window lifecycle, runtime-only imports | Sandbox shell | `Test.SandboxEditorPresentation.cpp`, `Test.SandboxDomainPanels.cpp` |
| Config file, UI, and agent/CLI parity | feature config modules + app registration + config control | `Test.SandboxConfigSections.cpp`, `Test.RuntimeConfigControl.cpp`, `Test.ParameterizationOperations.cpp`, `Test.SandboxPointCloudConsolidationPanel.cpp` |

## Exhaustive removed-type ledger

This table is the no-compatibility-bucket inventory of every type exported by
the removed `Runtime.SandboxEditorFacades` and
`Runtime.SandboxConfigSections` module interfaces.

| Removed export | Current symbol | Owner | Disposition |
|---|---|---|---|
| `SandboxEditorAssetPayloadKind` | `EditorAssetPayloadKind` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorCameraControllerKind` | `EditorCameraControllerKind` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRecipeSlotKind` | `EditorRecipeSlotKind` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderRecipeConfigState` | `EditorRenderRecipeConfigState` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderRecipeConfigDiagnosticCode` | `EditorRenderRecipeConfigDiagnosticCode` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorDiagnosticCode` | `EditorDiagnosticCode` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorCommandStatus` | `EditorCommandStatus` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorDomainWindowKind` | `EditorDomainWindowKind` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorVisualizationPropertyDomain` | `EditorVisualizationPropertyDomain` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorVisualizationPropertyPreset` | `EditorVisualizationPropertyPreset` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorVisualizationTarget` | `EditorVisualizationTarget` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGeometryProcessingDomain` | `EditorGeometryProcessingDomain` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGeometryProcessingAlgorithm` | `EditorGeometryProcessingAlgorithm` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGeometryProcessingCapabilities` | `EditorGeometryProcessingCapabilities` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGeometryProcessingEntry` | `EditorGeometryProcessingEntry` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGeometryProcessingMenuItem` | `EditorGeometryProcessingMenuItem` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorProgressivePoissonCommand` | `EditorProgressivePoissonCommand` | `Runtime.PointSetOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorProgressivePoissonResult` | `EditorProgressivePoissonResult` | `Runtime.PointSetOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorProgressivePoissonConfigStatus` | `EditorProgressivePoissonConfigStatus` | `Runtime.PointSetOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorProgressivePoissonConfigCommand` | `EditorProgressivePoissonConfigCommand` | `Runtime.PointSetOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorProgressivePoissonConfigResult` | `EditorProgressivePoissonConfigResult` | `Runtime.PointSetOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshDenoiseStage` | `EditorMeshDenoiseStage` | `Runtime.MeshTopologyOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshCurvatureOutput` | `EditorMeshCurvatureOutput` | `Runtime.MeshCurvatureConfig.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshRemeshMode` | `EditorMeshRemeshMode` | `Runtime.MeshTopologyOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshRemeshSizingLaw` | `EditorMeshRemeshSizingLaw` | `Runtime.MeshTopologyOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshSubdivideOperator` | `EditorMeshSubdivideOperator` | `Runtime.MeshTopologyOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshSimplifyMetric` | `EditorMeshSimplifyMetric` | `Runtime.MeshTopologyOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshDenoiseCommand` | `EditorMeshDenoiseCommand` | `Runtime.MeshTopologyOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshDenoiseResult` | `EditorMeshDenoiseResult` | `Runtime.MeshTopologyOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshCurvatureCommand` | `EditorMeshCurvatureCommand` | `Runtime.MeshCurvatureConfig.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshCurvatureResult` | `EditorMeshCurvatureResult` | `Runtime.MeshFieldOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshRemeshCommand` | `EditorMeshRemeshCommand` | `Runtime.MeshTopologyOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshRemeshResult` | `EditorMeshRemeshResult` | `Runtime.MeshTopologyOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshSubdivideCommand` | `EditorMeshSubdivideCommand` | `Runtime.MeshTopologyOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshSubdivideResult` | `EditorMeshSubdivideResult` | `Runtime.MeshTopologyOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshSimplifyCommand` | `EditorMeshSimplifyCommand` | `Runtime.MeshTopologyOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshSimplifyResult` | `EditorMeshSimplifyResult` | `Runtime.MeshTopologyOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorICPVariant` | `EditorICPVariant` | `Runtime.RegistrationOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRegistrationCommand` | `EditorRegistrationCommand` | `Runtime.RegistrationOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRegistrationResult` | `EditorRegistrationResult` | `Runtime.RegistrationOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshVertexNormalsCommand` | `NormalEstimationConfig` | `Runtime.NormalOperations.cppm` | configured normal operation |
| `SandboxEditorMeshVertexNormalsResult` | `EditorNormalEstimationResult` | `Runtime.NormalOperations.cppm` | configured normal operation |
| `SandboxEditorGraphVertexNormalsCommand` | `NormalEstimationConfig` | `Runtime.NormalOperations.cppm` | configured normal operation |
| `SandboxEditorGraphVertexNormalsResult` | `EditorNormalEstimationResult` | `Runtime.NormalOperations.cppm` | configured normal operation |
| `SandboxEditorPointCloudVertexNormalsCommand` | `NormalEstimationConfig` | `Runtime.NormalOperations.cppm` | configured normal operation |
| `SandboxEditorPointCloudVertexNormalsResult` | `EditorNormalEstimationResult` | `Runtime.NormalOperations.cppm` | configured normal operation |
| `SandboxEditorDiagnostic` | `EditorDiagnostic` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorEntityRow` | `EditorEntityRow` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorTransformModel` | `EditorTransformModel` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderHintModel` | `EditorRenderHintModel` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGeometryDomainModel` | `EditorGeometryDomainModel` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorGeometryPresentationPropertyOptionModel` | `EditorGeometryPresentationPropertyOptionModel` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorPropertyCatalogDomain` | `EditorPropertyCatalogDomain` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorPropertyValuePreview` | `EditorPropertyValuePreview` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorPropertyCatalogRow` | `EditorPropertyCatalogRow` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorPropertyBindingTargetModel` | `EditorPropertyBindingTargetModel` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorVertexChannelBindingOptionModel` | `EditorVertexChannelBindingOptionModel` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorVertexChannelBindingTargetModel` | `EditorVertexChannelBindingTargetModel` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorPropertyCatalogModel` | `EditorPropertyCatalogModel` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGeometryPresentationSlotModel` | `EditorGeometryPresentationSlotModel` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorJobScope` | `EditorJobScope` | `Runtime.EditorJobProjection.cppm` | feature-owned runtime contract |
| `SandboxEditorJobIdentity` | `EditorJobIdentity` | `Runtime.EditorJobProjection.cppm` | feature-owned runtime contract |
| `SandboxEditorJobDependency` | `EditorJobDependency` | `Runtime.EditorJobProjection.cppm` | feature-owned runtime contract |
| `SandboxEditorJobDomain` | `EditorJobDomain` | `Runtime.EditorJobProjection.cppm` | feature-owned runtime contract |
| `SandboxEditorJobRecord` | `EditorJobRecord` | `Runtime.EditorJobProjection.cppm` | feature-owned runtime contract |
| `SandboxEditorJobQueueSnapshot` | `EditorJobQueueSnapshot` | `Runtime.EditorJobProjection.cppm` | feature-owned runtime contract |
| `SandboxEditorJobDependencyModel` | `EditorJobDependencyModel` | `Runtime.EditorJobProjection.cppm` | feature-owned runtime contract |
| `SandboxEditorJobModel` | `EditorJobModel` | `Runtime.EditorJobProjection.cppm` | feature-owned runtime contract |
| `SandboxEditorGeometryCompositionSummary` | `EditorGeometryCompositionSummary` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorGeometryPresentationModel` | `EditorGeometryPresentationModel` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorBoundRenderStateRowKind` | `EditorBoundRenderStateRowKind` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorBoundRenderStateRow` | `EditorBoundRenderStateRow` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorBoundRenderStateModel` | `EditorBoundRenderStateModel` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorTextureBakeSourceCategory` | `EditorTextureBakeSourceCategory` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorTextureBakeSourceRow` | `EditorTextureBakeSourceRow` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorUvDiagnosticsModel` | `EditorUvDiagnosticsModel` | `Runtime.EditorCommon.cppm` | feature-owned runtime contract |
| `SandboxEditorTextureBakeTarget` | `EditorTextureBakeTarget` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorTextureBakeTargetSnapshot` | `EditorTextureBakeTargetSnapshot` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorTextureBakeTargetUpdateRequest` | `EditorTextureBakeTargetUpdateRequest` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorTextureBakeControlsModel` | `EditorTextureBakeControlsModel` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorInspectorModel` | `EditorInspectorModel` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorPrimitiveDetailModel` | `EditorPrimitiveDetailModel` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorSelectionModel` | `EditorSelectionModel` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorSceneFileOperation` | `EditorSceneFileOperation` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorSceneFileCommand` | `EditorSceneFileCommand` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorSceneFileResult` | `EditorSceneFileResult` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorSceneFileCommandSurface` | `EditorSceneFileCommandSurface` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorSceneFileModel` | `EditorSceneFileModel` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorDocumentModel` | `EditorDocumentModel` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorFileImportCommand` | `EditorFileImportCommand` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorFileImportResult` | `EditorFileImportResult` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorAssetImportCommandSurface` | `EditorAssetImportCommandSurface` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorFileImportPayloadOption` | `EditorFileImportPayloadOption` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorFileImportModel` | `EditorFileImportModel` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorAssetImportQueueCommandSurface` | `EditorAssetImportQueueCommandSurface` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorAssetImportQueueRow` | `EditorAssetImportQueueRow` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorAssetImportQueueModel` | `EditorAssetImportQueueModel` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderGraphPassModel` | `EditorRenderGraphPassModel` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGpuProfileQueueModel` | `EditorGpuProfileQueueModel` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGpuProfilePassModel` | `EditorGpuProfilePassModel` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGpuProfileModel` | `EditorGpuProfileModel` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGpuProfilingConfigStatus` | `EditorGpuProfilingConfigStatus` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGpuProfilingConfigResult` | `EditorGpuProfilingConfigResult` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderGraphModel` | `EditorRenderGraphModel` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderRecipeDraftState` | `EditorRenderRecipeDraftState` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderRecipeCommandKind` | `EditorRenderRecipeCommandKind` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderRecipeCommandStatus` | `EditorRenderRecipeCommandStatus` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderRecipeSlotModel` | `EditorRenderRecipeSlotModel` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderRecipeBindingOverrideModel` | `EditorRenderRecipeBindingOverrideModel` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderRecipeOutputModel` | `EditorRenderRecipeOutputModel` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderArtifactRow` | `EditorRenderArtifactRow` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderRecipeEditorModel` | `EditorRenderRecipeEditorModel` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderRecipeEditorState` | `EditorRenderRecipeEditorState` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderRecipeCommand` | `EditorRenderRecipeCommand` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderRecipeCommandResult` | `EditorRenderRecipeCommandResult` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorPrimitiveViewSettings` | `EditorPrimitiveViewSettings` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorPrimitiveViewCommandSurface` | `EditorPrimitiveViewCommandSurface` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorVisualizationRecipeCommandSurface` | `EditorVisualizationRecipeCommandSurface` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorJobCommandSurface` | `EditorJobCommandSurface` | `Runtime.EditorJobProjection.cppm` | feature-owned runtime contract |
| `SandboxEditorUvRegenerationCommandResult` | `EditorUvRegenerationCommandResult` | `Runtime.ParameterizationOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorParameterizationResult` | `EditorParameterizationResult` | `Runtime.ParameterizationOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorCameraRenderModel` | `EditorCameraRenderModel` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorVisualizationConfigModel` | `EditorVisualizationConfigModel` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorVisualizationPropertyInfo` | `EditorVisualizationPropertyInfo` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorVisualizationRecipeModel` | `EditorVisualizationRecipeModel` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorVisualizationModel` | `EditorVisualizationModel` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGeometryProcessingModel` | `EditorGeometryProcessingModel` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorDomainWindowModel` | `EditorDomainWindowModel` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorSelectedModelCacheSection` | `EditorSelectedModelCacheSection` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorSelectedAnalysisCacheConsumer` | `EditorSelectedAnalysisCacheConsumer` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorSelectedModelCacheKey` | `EditorSelectedModelCacheKey` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorSelectedAnalysisModel` | `EditorSelectedAnalysisModel` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorSelectedAnalysisCacheEntry` | `EditorSelectedAnalysisCacheEntry` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorVisualizationModelCacheEntry` | `EditorVisualizationModelCacheEntry` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorSelectedModelCacheStats` | `EditorSelectedModelCacheStats` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorSelectedModelCache` | `EditorSelectedModelCache` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorModelBuildRequest` | `EditorWorkspaceSnapshotRequest` | `Runtime.EditorWorkspaceSnapshots.cppm` | workspace snapshot request |
| `SandboxEditorModelBuildStats` | `EditorWorkspaceSnapshotStats` | `Runtime.EditorCommon.cppm` | workspace snapshot diagnostics |
| `SandboxEditorPanelFrame` | `SandboxEditorFrame` | `app/Sandbox/Editor/Sandbox.PanelSupport.hpp` | app-owned composition copied from `EditorWorkspaceSnapshot` |
| `SandboxEditorParameterizationUvViewStatus` | `EditorParameterizationUvViewStatus` | `Runtime.ParameterizationOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorParameterizationUvViewRequest` | `EditorParameterizationUvViewRequest` | `Runtime.ParameterizationOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorParameterizationUvViewState` | `EditorParameterizationUvViewState` | `Runtime.ParameterizationOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorParameterizationUvViewCommandSurface` | `EditorParameterizationUvViewCommandSurface` | `Runtime.ParameterizationOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorContext` | `SandboxEditorContext` | `app/Sandbox/Editor/Sandbox.PanelSupport.hpp` | app-owned composition of copied snapshots and feature-named command/query handles |
| `SandboxEditorTransformEditCommand` | `EditorTransformEditCommand` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorCameraControllerCommand` | `EditorCameraControllerCommand` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorPrimitiveViewCommand` | `EditorPrimitiveViewCommand` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderHintCommand` | `EditorRenderHintCommand` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorVisualizationConfigCommand` | `EditorVisualizationConfigCommand` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorVisualizationPropertyCommand` | `EditorVisualizationPropertyCommand` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorVisualizationRecipeCommand` | `EditorVisualizationRecipeCommand` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorVertexChannelBindingCommand` | `EditorVertexChannelBindingCommand` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGeometryPresentationSlotDefaultCommand` | `EditorGeometryPresentationSlotDefaultCommand` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGeometryPresentationSlotPropertyCommand` | `EditorGeometryPresentationSlotPropertyCommand` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorTextureBakeCommand` | `EditorTextureBakeCommand` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorTextureBakeCommandResult` | `EditorTextureBakeCommandResult` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorUvRegenerationCommand` | `EditorUvRegenerationCommand` | `Runtime.ParameterizationOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorParameterizationStrategy` | `EditorParameterizationStrategy` | `Runtime.ParameterizationOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorParameterizationCommand` | `EditorParameterizationCommand` | `Runtime.ParameterizationOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorConfiguredParameterizationCommand` | `EditorConfiguredParameterizationCommand` | `Runtime.ParameterizationOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorParameterizationConfigStatus` | `EditorParameterizationConfigStatus` | `Runtime.ParameterizationOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorParameterizationConfigCommand` | `EditorParameterizationConfigCommand` | `Runtime.ParameterizationOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorParameterizationConfigResult` | `EditorParameterizationConfigResult` | `Runtime.ParameterizationOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorParameterizationViewModel` | `EditorParameterizationViewModel` | `Runtime.ParameterizationOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorPreparedFrameView` | app-private `SandboxPreparedFrame` over five feature-owned `Editor*PreparedFrame` records | `Sandbox.EditorShell.cpp` plus the workspace/scene/geometry/visualization/render-recipe operation modules | app-private aggregate; no public all-feature prepared frame |
| `SandboxEditorPreparedFrameVisitor` | — | deleted | direct app-owned calls to the five feature preparation functions; no replacement visitor |
| `SandboxEditorSession` | `EditorWorkspaceAttachment` | `Runtime.EditorWorkspaceAttachment.cppm` | opaque attachment lifecycle only; each feature owner prepares its own surface |
| `EngineConfigSectionRegistry` | `Core::Config::EngineConfigSectionRegistry` | `runtime feature config modules` | shared Core alias; app aggregates registrations |
| `EngineConfigSectionRegistration` | `Core::Config::EngineConfigSectionRegistration` | `runtime feature config modules` | shared Core alias; feature factory returns it |
| `EngineConfigSectionChangedCallback` | `Core::Config::EngineConfigSectionChangedCallback` | `runtime feature config modules` | shared Core alias; app supplies callbacks |
| `ClusteringConfig` | `ClusteringConfig` | `Runtime.ClusteringConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ProgressivePoissonPlaygroundChannel` | `ProgressivePoissonPlaygroundChannel` | `Runtime.ProgressivePoissonConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ProgressivePoissonPlaygroundBackend` | `ProgressivePoissonPlaygroundBackend` | `Runtime.ProgressivePoissonConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ProgressivePoissonPlaygroundConfig` | `ProgressivePoissonPlaygroundConfig` | `Runtime.ProgressivePoissonConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ParameterizationStrategyKind` | `ParameterizationStrategyKind` | `Runtime.ParameterizationConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ParameterizationBoundaryPolicy` | `ParameterizationBoundaryPolicy` | `Runtime.ParameterizationConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ParameterizationBffBoundaryMode` | `ParameterizationBffBoundaryMode` | `Runtime.ParameterizationConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ParameterizationUvRenderMode` | `ParameterizationUvRenderMode` | `Runtime.ParameterizationConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ParameterizationUvBackgroundMode` | `ParameterizationUvBackgroundMode` | `Runtime.ParameterizationConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ParameterizationViewConfig` | `ParameterizationViewConfig` | `Runtime.ParameterizationConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ParameterizationUvConfig` | `ParameterizationUvConfig` | `Runtime.ParameterizationConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ParameterizationLscmConfig` | `ParameterizationLscmConfig` | `Runtime.ParameterizationConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ParameterizationHarmonicConfig` | `ParameterizationHarmonicConfig` | `Runtime.ParameterizationConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ParameterizationBffConfig` | `ParameterizationBffConfig` | `Runtime.ParameterizationConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ParameterizationConfig` | `ParameterizationConfig` | `Runtime.ParameterizationConfig.cppm` | feature-owned config contract; app aggregates registration |

Workspace snapshot construction folds the four feature contexts directly into one
private binding record. Scene owns identity; processing, visualization and recipe
contexts supply their respective fields. Any expired attachment makes the combined
record inert. Job commands fall back from processing to visualization, engine
config from processing to recipe, and invalidation from scene through processing
to visualization. Forward feature-context construction remains shared.
