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

Scene editing contexts carry asset-import command callbacks. The asset-workflow
owner resolves live asset services; editor contexts and snapshot bindings do not
carry an unused service pointer. `EditorCompilationLocality.AssetService` guards
the scene/workspace interfaces and shared test producers against that dependency.

Editor job lists and the optional UV-regeneration job own copies of the canonical
`EditorJobRecord` from `Runtime.EditorJobProjection.cppm`. Its dependencies reuse
the scheduler's value-owned `JobDependency` from `Runtime.JobService.cppm`, including
their order, tokens and reasons. Runtime and app consumers share these records;
there is no separate UI job payload or field-copy converter. Missing jobs remain
empty or absent, while present records retain their state, identity, backend
domains, dependency reasons, progress and diagnostics independently of later
queue changes.

Property-binding targets and presentation slots own vectors of the canonical
`GeometryPresentationPropertyOption` from `Runtime.GeometryPresentation.cppm`.
The enumerator's ordered records carry the property reference, count, source
generation, compatibility and disabled reason directly into both editor views.
These callers leave the optional observed source generation at its default zero.
App consumers read the value kind from `Property.ValueKind`; snapshots retain
independent strings without a second record or conversion pass. Both builders use
one private slot-selector resolver: authored domain and value kind take precedence,
otherwise provenance/lane/semantic defaults apply. An unresolved domain yields
empty options in both views.

The shell's active prepared-frame storage owns the point-cloud service frame.
`SandboxEditorContext` borrows it for the draw visit and is reset before that
storage on draw completion and detach. Its constructor requires a frame lvalue;
callers must keep that storage alive for the complete visit. The canonical frame
definition shares global C++ attachment with its family result/sink records, so
unrelated panels can forward-declare the borrow without importing config types.
A default context exposes the same empty unavailable service state. Consolidation
helper records are declared only in `Sandbox.PointCloudConsolidationPanel.hpp`,
consumed by MethodPanels and its integration test; their definitions live in the
existing MethodPanels implementation. Shared support, domain and mesh-processing
implementations do not depend on consolidation config or service operations.
The service/session owners still require complete config/result records.


Prepared-frame command/query handles carry the workspace attachment epoch.
Retaining one beyond detach is observable as unbound, and every operation
fails closed before reaching the copied service pointers; callback surfaces
retain their operation-specific expired-attachment diagnostics. Mutating
feature contexts receive only an epoch-guarded cache-invalidation callback,
not the workspace cache object, so scene/geometry/visualization operations
preserve selected-model cache behavior without widening their public owner
boundaries.

Normal config and editor results use `Geometry.NormalEstimation.Types` for the
canonical orientation/weighting enums and copied point-normal diagnostics. The
point and mesh normal algorithms re-export those types; their parameters,
result properties and functions stay with the algorithm owners. Config codecs
and normal prepared frames therefore do not import normal algorithms, owning
mesh/point-cloud containers or spatial indices. The compiler-derived
`ProcessingCompilationLocality.NormalContracts` test guards that boundary.

Density-weight config and point-analysis results use
`Geometry.PointCloud.Kernels.Types` for the kernel/mode enums, their token
spellings and copied density diagnostics. The algorithm module re-exports this
owner. Config codecs and point-analysis prepared frames do not import the kernel
algorithms or spatial-query/index modules; the execution unit imports its kernel
API explicitly. `ProcessingCompilationLocality.DensityWeightContracts` checks
these configured compiler dependencies.

Mesh-topology results use `Geometry.Smoothing.Types` for the canonical denoiser
status. The smoothing algorithm re-exports this enum; its functions and status
spelling remain with the algorithm owner. Topology contract and prepared-frame
producers do not import smoothing, the owning halfedge mesh or discrete calculus.
`ProcessingCompilationLocality.MeshTopologyContracts` guards this boundary using
configured compiler dependencies.

Parameterization results use `Geometry.Parameterization.Types` for copied quality
and rejection diagnostics and `Geometry.UvAtlas.Types` for atlas status/provenance.
The algorithm interfaces re-export their canonical types. The runtime interface
and prepared frame do not import parameterization/atlas algorithms, the owning
halfedge mesh or mesh soup; execution units import the APIs they use directly.
`ProcessingCompilationLocality.ParameterizationContracts` checks this boundary
from configured compiler dependencies.

Consolidation result records use `Geometry.PointCloud.Consolidation.Types` for
its canonical status enum and `Runtime.GeometryProperty.Types` for property
identities. The algorithm re-exports the status; parameters, projection state,
result arrays and status spelling remain with the algorithm owner.
`ProcessingCompilationLocality.ConsolidationContracts` keeps the runtime records
independent of the algorithm, point containers, spatial indexes and live geometry
availability. `ConsolidationConsumers` checks the algorithm boundary for the
service-family interface and prepared frame; their editor presentation and
private composition still consume live availability.

`Runtime.EditorFeatures.Internal.hpp` holds private workspace bindings and context
adapters. It includes `Runtime.EditorFeatureCommands.Internal.hpp` for import/file
prerequisites and diagnostics, and
`Runtime.EditorFeatureProperties.Internal.hpp` for property catalogs and model
statistics. Action implementations include these declarations directly, without
the workspace storage. Scene actions use the command helpers; visualization
actions use the property helpers. Their definitions remain compiled once in
`Runtime.EditorFeatureContextAdapters.cpp`. Stored lane overrides and effective
entity-fallback visualization lookup use this same owner for command history
and copied models, declared in `Runtime.EditorVisualizationHelpers.hpp` so
unrelated context consumers need no visualization component import. Stored lookup
preserves absence for undo; effective lookup falls back from an absent lane
override to the entity config. Geometry operations use the smaller
`Runtime.EditorGeometryHelpers.hpp` for entity signatures, stable-entity lookup
and command-status conversion. Registration and scene actions additionally
include `Runtime.EditorTransformHelpers.hpp` for transform comparison and undo
publication; unrelated geometry families need no transform component module.
UV view request tokens reuse the compiled editor signature byte mixer while
retaining their own field ordering. The curvature changed-value template and
topology positive-finite predicate live only in their consuming implementations.
These headers contain no module imports; each implementation imports the types
it uses. Helpers retain C++ linkage across their owning operation units.
`EditorCompilationLocality.Actions` excludes unrelated processing services,
render-recipe editing, renderer and workspace snapshots from both action units.
Scene primitive-view history and visualization render-hint history share owned
`EditorRenderHintComponents` snapshots and their compiled capture, comparison and
apply functions through `Runtime.EditorRenderHintHelpers.hpp`. Float width/size
sources compare by their exact bits; property-name sources compare as strings.
Visualization history composes this record with surface visualization settings
and applies those settings before the render components. Each family retains its
own transaction and guard. The narrow helper header lets visualization actions
exclude scene-editing and asset-import modules, enforced by
`EditorCompilationLocality.RenderHintActions`.
The five feature config modules own their public schemas and globally attached
codec declarations. `Runtime.FeatureConfigCodecs.Detail.cpp` directly defines
those functions as one ordinary translation unit, sharing JSON parsing without
a private forwarding module. Curvature parameter conversion retains its feature
implementation owner and is declared only in the private
`Runtime.CurvatureSegmentationParams.hpp` shared by validation and execution.
The config interface, shared codec and Sandbox config registration producers
exclude the segmentation algorithm through
`ConfigCompilationLocality.CurvatureInterface`. Consolidation token functions
retain their feature implementation owner. Seven point-processing config
implementations also reuse the shared TU's string-token property encoder,
validator and name/domain decoder through `Runtime.PointConfigJson.hpp`.
Validation distinguishes malformed references from unknown domain tokens;
five families reuse `ValidatePointConfigPropertyRefs` for their matching ordered
per-field diagnostics. Each supplied field exists in the merged defaults; the
first invalid reference wins before family-specific property relationship checks.
Other families retain their own diagnostic wording and classification.
Decoding retains the caller's expected value kind; family parsers stay feature-owned.
The numeric-kind property codec and vec3-only
encoders remain separate contracts. Visualization operation declarations and
implementations have no UV-atlas dependency, enforced by
`EditorCompilationLocality.VisualizationUvAtlas`. The workspace
attachment retains its private module interface. Production app sources
may not import `Extrinsic.Runtime.Private.*` or include runtime-private headers.

Physical implementation ownership follows the feature split. Geometry operation
bodies share source-snapshot, stored-topology-fingerprint and publication helpers
through private headers and one ordinary compiled owner `MeshSupport.cpp`, which
imports no family module and no broad processing module. `MeshSources.hpp` holds
source preparation and fingerprint declarations; geodesics and parameterization
include it without the job/publication declarations in `MeshSupport.hpp`.
`BuildHalfedgeMeshForProcessing` names the requesting operation in failures;
position-binding errors identify the bound property. Shared metadata checks retain the source snapshot and
deletion-mask ordering before topology conversion; topology previews report the
same metadata defect text as execution. The compact topology result discards
unused snapshot buffers before whole-mesh processing. The shared owner's
triangle-soup result and builder declarations live in `Runtime.GeometryProcessingOperations.MeshSoup.hpp`, included only by that
compiled owner and UV regeneration. Mesh field/topology families need no mesh-soup
module. The family-neutral parts of that owner — the queued
job envelope, the active-job lookup and message, selected-model cache
invalidation, finite-position collection, numeric position comparison, job-handle
messages and result error normalization — are declared
in `PointFields.hpp` instead, which `MeshSupport.hpp` includes. That keeps point-set
families and registration off the by-value halfedge-mesh and mesh-soup snapshots
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

The snapshot interface takes its container, string, optional and shared-pointer
names from `Extrinsic.Core.Std`. That small module
interface includes the standard headers without editor dependencies and exposes
using-declarations in `Core::Std`; it adds no declarations to `std` and defines
no replacement types. The primary snapshot interface imports it without
re-exporting those helper names. CMake publishes its module dependency metadata
because consumers need the referenced BMI. Scalar standard headers stay local.
Cache-key equality is defaulted in `Runtime.EditorWorkspaceSnapshots.Public.cpp`,
where the normal headers make standard comparison operators visible, instead of
exporting standard overloads into the runtime namespace. All snapshot records
retain their original types, aggregate initialization and value ownership.
This scoped header-ownership boundary avoids serializing merged standard-header
declarations again in the broad snapshot interface; it is not a general-purpose
container facade or a reason to migrate every interface without measurement.
The renderer uses the same declaration owner; no runtime-private module import
exception is needed for this shared lower-layer surface.

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

`EditorProcessing` borrows `RHI::IDevice` and `SpatialIndexCache` through forward
declarations with matching C++ linkage. Their sole class definitions remain in
`RHI.Device` and `Runtime.SpatialIndexCache`; the spatial cache implementation,
including its private storage, compiles at that owner. Processing-context and
snapshot interfaces need neither complete API. Consumers that call either service
import its owner. `EditorCompilationLocality.ProcessingServiceBorrows` checks the
processing, discovery and snapshot interfaces against compiler dependencies for
both owners and the LBVH implementation. Context storage, config callbacks and
prepared-frame lifetime remain unchanged.

`EditorCommandHistory` and `EditorProcessing` also borrow `SelectionController`
through a forward declaration. The sole class definition and its member
implementations retain their owner in `Runtime.SelectionController` with matching
C++ linkage; selection config, pick and primitive records remain module-attached.
History's selection command implementation imports the controller, while the
history interface and generic processing code do not. Compiler dependencies are
guarded by `EditorCompilationLocality.SelectionControllerBorrows`.

Scene-facing interfaces borrow `ECS::Scene::Registry` the same way. Its sole
definition in `ECS.Scene.Registry` has C++ linkage and still owns EnTT storage
by value. Command history, processing, selection/refinement, scene serialization,
scene/visualization editing and world management declare only the borrowed type.
`WorldRegistry` constructs and destroys its existing owning pointer out of line.
Concrete registry users import the owner; snapshot/context interfaces do not
deserialize the full registry API. `EditorCompilationLocality.SceneRegistryBorrows`
guards all ten interfaces and their workspace-snapshot consumer. This changes
compilation dependencies, not scene ownership, context lifetime or command behavior.

The session and public snapshot-query preparation share
`MakeEditorWorkspaceSnapshotContext`, compiled in the existing context-adapter
unit and declared through the private attachment interface. It combines the
prepared processing context with scene, visualization and recipe contexts and
the session cache borrow. The wrapper does not include the broad bindings header.
Query handles share an immutable context directly; their private access helper
stays in `Runtime.EditorWorkspaceSnapshots.Public.cpp`. Expired attachments yield
default models, and statistics overrides use a per-query context copy.
`EditorCompilationLocality.WorkspaceSnapshotQueries` guards the wrapper's
dependency boundary.

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

Clustering and consolidation records and borrowed service APIs have canonical
owners in `Runtime.ClusteringTypes` and `Runtime.PointCloudConsolidationTypes`.
Service member implementations compile with those Types owners; lifecycle
modules re-export the contracts and exclusively bind the services. Lifecycle
class forward declarations and definitions use matching C++ linkage so private
friendship does not pull registration, GPU or world state into the service API.
`PointCloudServiceOperations`, its prepared frame, and the editor session consume
Types directly. Within the operations family, only the implementation imports
module-owned consolidation preflight. The compiler-derived
`EditorCompilationLocality.PointCloudServices` check enforces these boundaries.
The consolidation lifecycle interface borrows the device, world registry and
spatial-index cache through declarations matching their owners' C++ linkage.
Its implementation imports those owners and the consolidation algorithm;
`EditorCompilationLocality.ConsolidationLifecycle` keeps all four out of the
interface's compiler dependency closure.
The method panel also consumes these Types and operation contracts directly.
Consolidation property-reference validation compiles beside its records in
`Runtime.PointCloudConsolidationTypes`; both the panel and lifecycle preflight
call that owner. `EditorCompilationLocality.MethodPanelServices` keeps both
lifecycle modules out of the panel's compiler dependency closure.
Consolidation draft and request validation reuse the runtime config validator;
the strategy menu supplies labels and choices, not a second validation policy.
`Runtime.ClusteringConfig` imports the Types owner without importing the lifecycle
module. `Runtime.ModuleLifecycle` provides the minimal lifecycle interface;
complete setup/frame/recipe capabilities remain in
`Runtime.Module` for implementation and composition callers. The shared UV
inspection record belongs to `Runtime.EditorCommon`, so appearance models do
not import the geometry-processing operation interface.

Bit-exact property guards share the private
`GeometryIntegration/Runtime.GeometryValueComparison.hpp` overloads across mesh
properties, Progressive Poisson, normals, parameterization, clustering and
consolidation. Matching `std::vector<T>` buffers use the same helper for length
and element comparison, including packed Booleans, signed zero and NaN payloads.
Graph/topology tags and unsupported-property policies remain local.
`SameGeometryPositions` retains numeric comparison, including its distinct
signed-zero/NaN behavior; it does not use the bit-exact helper.

Clustering and mesh processing share `CollectFiniteGeometryPositions`, declared
in the private `GeometryIntegration/Runtime.GeometryPositionCapture.hpp` and
compiled in `Runtime.GeometryProcessingOperations.PointProperties.cpp`.
It snapshots a nonempty, count-matched vec3 property in source-row order and
rejects any non-finite value. It does not interpret deletion masks. Clustering
uses the narrow declaration header without importing mesh or editor job types;
`ProcessingCompilationLocality.PositionCapture` guards the mesh-free closure of
clustering and the compiled capture owner.

Density, spacing, density weights, keypoints and outlier analysis share live
position/deletion capture and same-domain output preflight in
`Runtime.GeometryProcessingOperations.PointProperties.cpp`. `CapturePointInput`
resolves the domain, watches positions and deletion storage, skips deleted rows,
and preserves ascending source-row IDs without copying values during readiness
or catalog queries. `ResolvePointDeletionSource` supplies the shared domain,
property name and row divisor for this capture, bilateral filtering, descriptors,
construction, normals and registration. Halfedges inherit the paired edge's deletion flag;
construction reuses this capture; normal generation retains its additional topology
mask validation and ownership. Normal readiness validates mask metadata and
captures watches without copying masks or counting live faces. Execution owns
the graph edge-mask snapshot and mesh reconstruction; empty face topology keeps
the existing face-output no-op and weighted-vertex fallback behavior.
`CapturePointNormalInput` composes the same capture for positions and same-domain
normals. Bilateral filtering, descriptors and supplied-normal Hoppe construction
share its deletion mapping, compact row order and revision watches. Descriptors
reject exact zero normals; Hoppe also requires finite float squared lengths above
`1e-16`. The shared scan caches minimum/maximum squared norms, leaving those
thresholds with the consuming family. LBVH coordinate limits apply to positions,
not normal components.
ICP source, target and local-normal readiness reuse the same per-property verdicts.
Its point-to-plane preview still validates inverse-transpose normal arithmetic on
borrowed live rows: local finite/length verdicts alone cannot prove float safety
under the target transform. That exact check performs no value-vector allocation
but remains a synchronous row scan. Execution captures each operand once into
`PointInputCapture`; publication compares its property/deletion watches, compact
source-row IDs and values, alongside the existing entity/transform guards. The
shared input metadata checks actual vec3 storage size as well as domain size
before any row access.
`ValidatePointOutputs` checks the resolved output domain,
reserved names and existing storage against the validated typed config.

Normal, outlier, keypoint, descriptor, density-weight, kernel-density, spacing,
bilateral and registration previews return the shared `ActionReadiness` directly.
Their method preflight is independent of the config-command lane; config-backed panel
actions combine it with `ResolveEditorProcessingActionReadiness`, which gives
missing config commands priority. Construction retains its resolved request
because execution consumes the resolved property bindings.

Density, spacing and density weights share scalar history publication, including
stale-storage guards, render dirty notifications and workspace invalidation on
apply, undo and redo. Density, spacing and local-distance-ratio outliers share
`AppendPointKnnRows` for complete CPU LBVH rows, including self candidates and
compact indices. Each adapter chooses its width and retains its numerical kernel;
statistical outliers keep their self-excluding queries. Density and spacing also
share framed fixed-width kNN pagination; radius pagination retains its separate
support-membership contract. These declarations live in `RadiusRows.hpp` for
consumers of spatial-index neighborhoods.
`BuildPointInputCatalog` shares live-row validation with density, spacing, weights,
keypoints, outliers, descriptors, construction, normals and registration, independently
of method result records. Density, spacing and bilateral catalogs require two live samples;
registration requires three; the generic catalog requires one. Catalog admission does not depend on the requested execution backend.
Prepared editor sessions own an opaque input-readiness cache in
`Runtime.GeometryProcessingOperations.PointProperties.cpp`. Catalog,
density, spacing, density-weight, outlier, keypoint, normal, bilateral, descriptor,
construction and registration previews inspect point metadata and enqueue a missing verdict on the engine's existing command bus.
The next main-thread command drain runs the compiled canonical row scan. The
point-input portion of these previews neither copies nor scans property values. Keys include
world/epoch, scene, entity, canonical
position property and its count/revision, and deletion-source count/revision;
halfedges use the paired edge's mask. Negative verdicts are cached too. Bilateral,
descriptor and supplied-normal construction previews request both property verdicts before returning pending,
reusing catalog and other-method entries independently. Cached zero-vector and
nonfinite flags preserve role-specific normal checks without rescanning. Both
properties must be accepted in the current main-thread preview; apply captures
both again. Normal metadata and the family output rules precede these requests.
Construction validates config, property metadata and the source world transform
before requesting cached inputs. Graph construction and Hoppe with estimated
normals request only positions. Position bounds, Vulkan subnormals, supplied-normal
lengths, sample/storage budgets and backend availability follow accepted inputs.
Nonfinite positions take priority over nonfinite normals; finite-input summaries
then replace row-order-dependent reasons with deterministic family checks. Apply
retains fresh captures and source/transform revision guards.
Only the latest generation for a logical source is retained, and sources unused
in the previous prepared frame expire. Weak queued references cannot keep old
entries alive. Metadata errors remain immediate; pending input verdicts disable
actions. Catalogs omit pending entries and include membership in their generation.
The combo retains its current binding independently of catalog membership.
Prepared processing handles are main-thread borrows and reject a switched or
destroyed world before dereferencing its scene. Retained mutable property borrows
must call `MarkModified()` after later writes, as required by property coherence.
Apply paths always capture current inputs again. Explicit standalone contexts
without session state retain synchronous preflight; sessions without a command
bus report readiness unavailable. Normal output metadata is checked before
requesting the point verdict; scalar output metadata follows it, preserving each
family's diagnostic order. Method/backend/topology checks follow an accepted verdict.
Normal-method readiness still owns its separate topology/mask checks, including
copies of topology deletion masks. Execution reconstructs its full point-deletion
mask from the shared capture's live source slots. The cache definition and scanner
stay compiled
in `PointProperties.cpp`, with only an opaque pointer and copied counters in the
shared processing interface. Engine registers its existing `CommandBus` as a
built-in service for this session wiring.
`RadiusRows.cpp` also owns `AcquirePointIndex`, shared by all nine point-processing
adapters. The private helper retains the immutable lease and cache-reuse flag,
then compares captured source rows and coordinates; typed backend gates, failure
statuses and method-specific diagnostics stay with the callers. Registration's
transformed target-space acquisition stays separate.

`RadiusRows.cpp` compiles the CPU capture and both pagers once outside either family and preserves
complete radius support and its explicit lowest-ID limit. Property capture imports
only the leaf point-LBVH algorithm for coordinate validation, without the spatial
cache service or paging records; `ProcessingCompilationLocality.PropertyCapture`
guards that compiler boundary. Normal processing uses its dedicated
normal kernels without importing the unrelated point-cloud utility API.
Processing discovery includes no mesh reconstruction header and requires neither
full halfedge-mesh types, the spatial cache nor transform components; its source-
domain queries use `GeometryAvailability` and its selection operations use the
existing processing context. Compiler-closure tests enforce this boundary.

Generic point discovery, density, spacing and bilateral filtering share a compiled
metadata-only candidate catalog in `PointProperties.cpp`. It supplies vec3 entries
and source/property revisions; each consumer retains its own numerical and sample-
count admission rather than borrowing another method's eligibility.

The private `PointFields.hpp` and `RadiusRows.hpp` declarations have C++ linkage across module
owners; the common implementation is compiled once as an ordinary translation
unit, without the broad method module or mesh-topology imports. Typed configs/results, numerical kernels, sample minima,
backend gates and job completion delivery remain with method adapters. Keypoint
mask/score publication and outlier provenance/removal transactions remain explicit.
Point-method submissions reuse the compiled `FindActiveEditorJob` lookup while
retaining their active-state filters, output identities and duplicate-job messages.

The nine point-processing panels use `ProcessingDraftState` and `PanelSupport`
for entity/input/output selection. `DrawProcessingPointInput` accepts an optional
domain restriction where topology requires it, such as mesh face normals taking
vertex positions. Descriptor, bilateral and construction normal selectors reuse
this catalog presentation, including domain headings and sample counts.
Descriptor and bilateral require the position domain; construction permits any
domain while positions are unresolved. The panels update coupled input/output
domains explicitly.
K-Means and Progressive Poisson share the local `DrawPointSetPositionInput`
chooser in `Sandbox.MethodPanels.cpp`; their output-domain updates remain in
the callers. Consolidation retains its distinct selection identity and default
focus behavior.
Curvature and geodesics reuse the same draft state, keyed by observed active
config, and apply-before-execute helper. Rejected edits remain retryable, while
external config changes replace the draft. Geodesics clears mesh-local source
indices on subsequent entity changes, including deselection; initial active
sources remain available on first use. Curvature segmentation uses the same draft
and apply-before-execute owners with explicit Apply/Reload: it adopts external
updates only while clean, retains rejected drafts, and Reload discards local
edits. Its Run action uses runtime admission plus config availability and stays
visible with a disabled reason when no suitable mesh is chosen.
`DrawProcessingExecution`, local to `MeshProcessingPanels.cpp`, owns the common
edit/validate/reapply/run sequence for density, spacing, keypoints, descriptors,
density weights, bilateral filtering and normal estimation. Its Run button and
construction's resolved-request button use `ActionReadiness` from existing
`Runtime.EditorProcessing`: the live config lane takes priority, then the method
preflight supplies its reason. Config preview/apply share their availability
predicate. Historical config errors are display state; each click reapplies and
revalidates, clears a successful retry's error, and only then executes.
`DrawProcessingActionButton` reuses `DrawDisabledReasonTooltip` immediately after
the disabled item; neither helper validates geometry. Outlier detection/removal,
ICP trajectory application and construction retain their distinct request sequences. Their
algorithm controls and statistics remain explicit; compatible Show actions use
`ShowProcessingProperty`, while face-normal display retains its face-lane path.

UV regeneration's shared control builds one request for
`PreviewEditorUvRegenerationCommand` and apply. Both use the same session,
parameter, mesh-source metadata and active-job admission checks; the preview
never builds or copies a mesh. The private `ValidateMeshSoupSourceMetadata`
also gates soup construction and mesh topology/curvature/segmentation admission.
It checks actual position, halfedge-connectivity and face-representative buffer
sizes against their owning property-domain slot counts, including inactive
slots. Equal connectivity-array lengths alone do not establish this contract.
Full finite/topology validation remains at submission, so admission readiness
does not certify numerical feasibility. `ValidateMeshVertexDeletionMaskMetadata`
shares the processing builder's optional Boolean vertex-mask cardinality check
with topology, curvature and segmentation admission. Topology checks it after
positions and before soup metadata; curvature checks it after soup metadata,
and segmentation after edge metadata. Each family retains its diagnostic prefix
and priority. UV preview requests a cached face-ring verdict after metadata and
active-job admission, then checks the mask, matching the command's error order.
The four mesh-topology previews share that cached verdict after position, mask
and soup metadata admission, preserving their command builder's mask-first order.
`MeshSupport.cpp` compiles one ring walk for soup construction and scan-only
validation; the latter does not copy positions or materialize triangles. A
`MeshFaceRings` input kind reuses the existing cache, weak queued entries,
world/attachment guards and frame pruning. Its key includes the bound position
property's slot count and the four canonical face/halfedge topology properties'
counts and revisions. Point-row and mesh-ring entries never alias.
The shared scan counter includes both kinds. Mask-only edits do not rescan rings;
coordinate-only edits do not invalidate ring validity either. Deferred checks run
on the main-thread command drain, not the UI frame; standalone contexts retain
synchronous validation.
`Runtime.GeometryProcessingOperations.MeshReadiness.hpp` exposes only admission
and readiness declarations, so the cache does not import owning mesh or soup
modules. Negative ring verdicts are cached. Finite-input, conversion and UV
feasibility readiness, plus mesh-field families' connectivity verdicts, remain
open under UI-037.
Duplicate requests return the existing pending job before mesh preparation,
without adding a result callback. This command does not require config controls.

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
Poisson, registration, UV, curvature and mesh topology finalizers share the
compiled `BuildUnpublishedEditorJobFailure` status/error mapping and diagnostic
builder in `MeshSupport.cpp`, declared in the private `JobFailure.hpp`. Only
`Current` validation admits worker detail; each caller retains its own predicate
for that detail, typed result data and exactly-once delivery flag. UV keeps its
separate atlas rejection status. The point-field header does not require the
editor command-status module for these declarations.

Inspector and domain appearance panels share compiled uniform-color,
scalar color/range and bin/isoline controls in `Sandbox.PanelSupport.cpp`.
The callers own source visibility and the domain-only baked-texture restriction;
the shared controls retain full-model command submission and caller ImGui IDs.

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

Processing config interfaces import `Extrinsic.Runtime.GeometryProperty.Types`
for canonical element domains, property references and kind filters. Its kind
vocabulary comes from `Geometry.Properties.Types`; neither interface imports
property storage, live ECS sources or rendering components. `GeometryAvailability`
re-exports the vocabulary and owns source inspection, catalog snapshots and
resolution. `ProcessingCompilationLocality.ConfigPropertyTypes` checks the type
owners, fifteen config interfaces, eleven config implementations, clustering
types and the shared feature codec against those live owners using the configured
compiler graph. The curvature-segmentation implementation deliberately calls the
geometry parameter validator and retains its algorithm import.
Property identities do not replace source provenance, sampling/raster domains,
vertex streams, material slots or visualization output meanings; those vocabularies
describe different contracts and remain with their current owners.

`Extrinsic.Runtime.EditorProcessing` owns one shared execution context and
attachment-checked command handle, independent of method configs, results and
workspace storage. `Extrinsic.Runtime.PointFieldOperations` owns density and
spacing contracts, validated config operations and typed result sinks.
`Extrinsic.Runtime.PointAnalysisOperations` owns weights, keypoints, outliers and
FPFH descriptors: one family because all four resolve a feature scale, page
radius or kNN support through the shared row helpers, and publish named
same-domain scalars in one undoable transaction. The family and its shared
property/row helpers import neither `Geometry.HalfedgeMesh` nor
`Geometry.MeshSoup`; they operate on resolved property domains, including mesh
domains, without owning a full mesh representation.
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
Direct and queued commands share private typed computation functions in
`Runtime.MeshTopologyOperations.Topology.cpp`; parameter mapping, kernel dispatch,
counters and kernel failures have one implementation per operation. Source capture,
UV handling, job guards and history publication remain with their existing callers.
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

The point-field copied result contains the editor's centroid and spacing/bounds
summary values directly. Geometry's owning `CloudStatistics` stays inside the
spacing implementation; it does not enter the family interface, config facade
or prepared-frame closure. Likewise, the bilateral result owns four last-pass
diagnostics directly; both its config and the point-set result interface stay
independent of point-cloud utilities and owning point-cloud containers.
Point-analysis results likewise carry keypoint/descriptor spacing and radii as
copied scalar values; their config, result and prepared-frame producers do not
import point-cloud features, utilities or owning containers. The numerical
implementations keep their feature-scale types, while density-weight kernel
choices and diagnostics retain their separate kernel-module contract.
Geodesic status and complete owned distance results live in
`Geometry.Geodesic.Types`, shared by the algorithm and runtime reports. Mesh-field
interfaces, config adapters and prepared frames import that data-only module;
only the geodesic execution unit imports `Geometry.Geodesic`. Parameter choices
and status-string conversion remain with the algorithm. Segmentation
status and diagnostic records live in `Geometry.CurvatureSegmentation.Diagnostics`,
shared by the GMM, feature, patch and boundary algorithms and runtime reports.
Mesh-field interfaces, config adapters and prepared frames therefore stay
independent of those algorithms and the owning halfedge mesh. Algorithm parameters,
full result arrays and status-string conversion remain with their algorithm owners.
Parameterization and UV regeneration access geometry views and publish through
the existing mesh-soup owner without importing ECS geometry-population adapters.

`ProcessingCompilationLocality.Family`, `.PointFieldResults`, `.PointAnalysis`, `.PointAnalysisTests`,
`.Normals`, `.NormalTests`, `.MeshSupport`, `.Registration`, `.Parameterization`,
`.RegistrationTests`, `.MeshField`, `.MeshTopology`, `.MeshFieldTests`,
`.GeodesicResults`, `.SegmentationDiagnostics`, `.PointSet`, `.PointSetResults`, `.PointAnalysisResults`, `.PointSetTests`, `.PointConstruction`, `.PointConstructionTests`,
`.PointCloudService`, `.Discovery` and `.UnrelatedAdapters` inspect actual
Clang scanner requirements and CMake module closures through
`tools/analysis/compile_hotspots.py`. Missing, ambiguous or stale source scans
fail closed. Run them after building their producers. These checks enforce
module dependencies, not timing budgets or a whole-engine impact database;
CI-014 remains the owner of generic build-impact selection. Matched content-edit
probes and their limitations are recorded by
[RUNTIME-233](../../tasks/done/RUNTIME-233-processing-compilation-locality-pilot.md)
[RUNTIME-234](../../tasks/done/RUNTIME-234-point-analysis-compilation-locality.md)
and [RUNTIME-235](../../tasks/done/RUNTIME-235-mesh-processing-compilation-locality.md).
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
| `SandboxEditorProgressivePoissonConfigStatus` | `RuntimeEngineConfigApplyStatus` | `Runtime.EngineConfigControl.cppm` | shared config result contract |
| `SandboxEditorProgressivePoissonConfigCommand` | `ProgressivePoissonPlaygroundConfig` | `Runtime.ProgressivePoissonConfig.cppm` | typed config passed directly to shared apply |
| `SandboxEditorProgressivePoissonConfigResult` | `RuntimeEngineConfigApplyResult` | `Runtime.EngineConfigControl.cppm` | shared config result; one preview/diagnostic payload |
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
| `SandboxEditorGeometryPresentationPropertyOptionModel` | `GeometryPresentationPropertyOption` | `Runtime.GeometryPresentation.cppm` | feature-owned runtime contract |
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
| `SandboxEditorJobDependency` | `JobDependency` | `Runtime.JobService.cppm` | canonical scheduler value contract |
| `SandboxEditorJobDomain` | `EditorJobDomain` | `Runtime.EditorJobProjection.cppm` | feature-owned runtime contract |
| `SandboxEditorJobRecord` | `EditorJobRecord` | `Runtime.EditorJobProjection.cppm` | feature-owned runtime contract |
| `SandboxEditorJobQueueSnapshot` | `EditorJobQueueSnapshot` | `Runtime.EditorJobProjection.cppm` | feature-owned runtime contract |
| `SandboxEditorJobDependencyModel` | `JobDependency` | `Runtime.JobService.cppm` | canonical scheduler value contract |
| `SandboxEditorJobModel` | `EditorJobRecord` | `Runtime.EditorJobProjection.cppm` | feature-owned runtime contract |
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
| `SandboxEditorParameterizationConfigStatus` | `RuntimeEngineConfigApplyStatus` | `Runtime.EngineConfigControl.cppm` | shared config outcome |
| `SandboxEditorParameterizationConfigCommand` | `ParameterizationConfig` | `Runtime.ParameterizationConfig.cppm` | typed config passed directly to the apply owner |
| `SandboxEditorParameterizationConfigResult` | `RuntimeEngineConfigApplyResult` | `Runtime.EngineConfigControl.cppm` | shared config result; one preview/diagnostic payload |
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
