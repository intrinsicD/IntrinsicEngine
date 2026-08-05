# Claims

## C01: PR-fast ccache provides a material same-shape latency reduction
- **Statement**: For the CI-007 `pr-fast` gate shape at commit `5394e51b`, five
  warm ccache samples reduced build median/p95 by 56.7%/58.3% and total
  median/p95 by 47.6%/50.0% versus five contemporary cold samples, while every
  gate and hermetic parity probe passed with zero ccache errors.
- **Status**: supported
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: A same-shape repeat cohort reports no warm median
  build improvement, a regressed warm build p95, nonzero ccache errors, or a
  cached/clean correctness mismatch.
- **Proof**: [ara/evidence/tables/ci007_ccache_cohort.md,
  tasks/archive/CI-007-module-safe-persistent-ccache-pilot.md,
  docs/benchmarking/ci-policy.md]
- **Dependencies**: []
- **Tags**: CI, ccache, C++23 modules, gate latency
- **From staging**: O15

## C02: Bounded disk BFF supports three deterministic CPU reference controls
- **Statement**: On supported triangle disks, the METHOD-023 CPU reference
  deterministically produces finite UVs for automatic conformal, approximate
  target-boundary-length, and prescribed exterior-angle controls, reports
  residual diagnostics, and fails closed for unsupported or invalid input.
- **Status**: supported
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: A supported deterministic fixture produces
  non-finite or nondeterministic UVs, a declared control fails its residual or
  orientation contract, or unsupported topology/input returns a UV payload.
- **Proof**: [tasks/done/METHOD-023-boundary-first-flattening-reference-backend.md,
  tests/unit/geometry/Test.BoundaryFirstFlattening.cpp,
  src/geometry/Geometry.Parameterization.Bff.cpp,
  commit 4bf4f67b]
- **Dependencies**: [K14, K15]
- **Tags**: geometry, parameterization, BFF, CPU reference
- **From staging**: O30

## C03: Adaptive Delaunay/QEF meshing is a gated research hypothesis
- **Statement**: QEF-derived feature samples may improve an adaptive Delaunay
  implicit-meshing reference on thin and sharp-feature fixtures, but the broad
  combination is prior-art constrained and does not warrant a 3D engine
  implementation unless a deterministic adversarial 2D falsifier passes first.
- **Status**: hypothesis
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: The 2D adversarial study fails its manifoldness,
  termination, feature-retention, or field-evaluation gates, or prior-art
  comparison leaves no distinct evidence question worth testing.
- **Proof**: [N220,
  tasks/backlog/methods/METHOD-027-adaptive-delaunay-qef-implicit-meshing.md]
- **Dependencies**: []
- **Tags**: geometry, implicit meshing, Delaunay, QEF, falsifier
- **From staging**: O39

## C04: Confidence-driven subdivision may improve guided Walk on Stars
- **Statement**: After a canonical METHOD-004 CPU reference exists, spatial
  subdivision driven by contribution and direction confidence may improve the
  variance-memory frontier of guided Walk on Stars, as a provisional transfer
  rather than a claim of a new guiding family.
- **Status**: hypothesis
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: Matched deterministic fixtures show no variance
  benefit at equal sample and memory budgets, confidence adds unacceptable
  bias, or the measured behavior is already fully explained by cited guided-WoSt
  prior art.
- **Proof**: [N220,
  tasks/backlog/methods/METHOD-028-confidence-driven-walk-on-stars-guiding.md]
- **Dependencies**: []
- **Tags**: Monte Carlo PDE, Walk on Stars, guiding, confidence, memory
- **From staging**: O40

## C05: Invariant-aware mip objectives may preserve scientific fields better
- **Statement**: At equal storage, field-specific optimization objectives may
  reduce angular error, categorical bleed, or isoline drift in normal, label,
  and signed-scalar mip pyramids relative to raw-channel averaging.
- **Status**: hypothesis
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: Deterministic CPU fixtures show no material
  improvement on the declared invariant metrics, introduce unacceptable
  reconstruction artifacts, or exceed the task's storage and build-cost limits.
- **Proof**: [N220,
  tasks/backlog/geometry/GEOM-065-invariant-aware-scientific-field-mip-pyramids.md]
- **Dependencies**: []
- **Tags**: geometry, scientific fields, mipmaps, invariants, visualization
- **From staging**: O45

## C06: Runtime background work has one CPU-supported lifecycle
- **Statement**: In the default CPU-supported runtime contract, production
  asynchronous workflows use the kernel-owned `JobService`; Engine applies at
  most eight completions before event pump B, `AsyncWorkModule` publishes and
  withdraws that borrowed service and cancels shutdown survivors, and the
  superseded `StreamingExecutor` and `DerivedJobGraph` modules are absent from
  source, CMake registration, dedicated tests, and the generated inventory.
- **Status**: supported — CPU-supported runtime scope; no new GPU/Vulkan
  execution claim
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: A production workflow imports or submits through
  either retired surface, either module returns to source/CMake/inventory, the
  frame path performs an unbounded completion drain, or cancellation/staleness
  permits a result to publish more than once.
- **Proof**: [tasks/done/RUNTIME-194-consolidate-runtime-work-execution.md,
  src/runtime/Kernel/Runtime.AsyncWorkModule.cpp,
  src/runtime/Kernel/Runtime.Engine.cpp,
  tests/contract/runtime/Test.RuntimeJobService.cpp,
  tests/contract/runtime/Test.AsyncWorkModule.cpp,
  tests/contract/runtime/Test.RuntimeEngineLayering.cpp,
  tests/contract/runtime/Test.ImGuiAdapterEngineWiring.cpp,
  docs/api/generated/module_inventory.md]
- **Dependencies**: [K05]
- **Tags**: runtime, JobService, CPU-supported, lifecycle, retirement
- **From staging**: O61

## C07: Multi-range GPU-result readback has a CPU-supported transport contract
- **Statement**: In CPU/fake-queue contracts, `Graphics.GpuTransfer` represents
  one logical GPU result as ordered copied ranges, exposes those bytes only
  after exact generational-handle, descriptor, access, range, and byte-count
  revalidation, and composes with canonical `JobService` parked publication,
  dependency release, world cancellation, and exactly-once consumption.
- **Status**: supported — CPU/fake-queue contract only; operational production
  evidence is tracked separately by C08 and C09
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: A malformed or stale request exposes bytes, a
  cancelled in-flight sink loses required storage or remains consumable, a
  batch consumes twice, a dependent releases before successful publication, or
  world cancellation permits later publication.
- **Proof**: [tasks/done/RUNTIME-195-unified-gpu-result-readback.md,
  src/graphics/renderer/Graphics.GpuTransfer.cppm,
  src/graphics/renderer/Graphics.GpuTransfer.cpp,
  tests/contract/graphics/Test.GpuTransferFacade.cpp,
  tests/contract/runtime/Test.GpuResultReadbackJob.cpp]
- **Dependencies**: [C06]
- **Tags**: graphics, runtime, GPU readback, JobService, CPU-supported
- **From staging**: O62

## C08: K-Means uses the shared result batch on operational Vulkan
- **Statement**: The production K-Means service's private GPU backend submits
  labels, squared distances, and centroids through one copied
  `Graphics.GpuTransfer` batch,
  deduplicates their common source to one transfer-read barrier, consumes the
  batch exactly once in its typed adapter, and matches its CPU reference on the
  deterministic separated-clusters Vulkan fixture.
- **Status**: supported — ASan+UBSan promoted Vulkan on NVIDIA GeForce RTX 3050,
  driver 590.48.01; no Progressive Poisson or whole-task claim
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: K-Means bypasses `ClusteringService`, imports or
  constructs the retired async wrapper, submits more than one logical result
  batch, emits more than one transfer-read barrier for its shared Work buffer,
  consumes the result more than once, or the operational parity fixture
  disagrees with the CPU reference.
- **Proof**: [tasks/done/RUNTIME-195-unified-gpu-result-readback.md,
  src/runtime/Modules/Clustering/Runtime.ClusteringGpuBackend.cppm,
  src/runtime/Modules/Clustering/Runtime.ClusteringGpuBackend.cpp,
  src/runtime/Modules/Clustering/Runtime.ClusteringGpuState.cpp,
  src/runtime/Modules/Clustering/Runtime.ClusteringModule.cpp,
  tests/contract/runtime/Test.GpuResultReadbackJob.cpp,
  tests/integration/runtime/Test.ClusteringServiceGpuSmoke.cpp]
- **Dependencies**: [C07]
- **Tags**: K-Means, graphics, runtime, GPU readback, Vulkan, parity
- **From staging**: O63

## C09: Progressive Poisson result transport operates on Vulkan
- **Statement**: The Progressive Poisson GPU backend exposes its
  `AcceptedKeys`, `LevelOffsets`, and `SplatRadii` production buffers to one
  copied `Graphics.GpuTransfer` batch, consumes that batch exactly once in its
  typed adapter, and allocates or records no duplicate result buffers/copies or
  blocking `IDevice::ReadBuffer` result path. An actual Vulkan smoke transports
  and parses a CPU-reference-shaped payload through those three buffers.
- **Status**: supported — ASan+UBSan promoted Vulkan transport/parser evidence
  on NVIDIA GeForce RTX 3050, driver 590.48.01; the payload is seeded and this
  is not Progressive Poisson compute or public CPU/GPU parity
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: The backend reintroduces duplicate readback
  targets or result pre-copies, calls blocking `IDevice::ReadBuffer` for its
  result, submits more than one logical batch, consumes the result more than
  once, or the actual-Vulkan transport fixture does not reproduce the seeded
  CPU-reference-shaped order, level offsets, and splat radii.
- **Proof**: [tasks/done/RUNTIME-195-unified-gpu-result-readback.md,
  src/runtime/Modules/ProgressivePoisson/Runtime.ProgressivePoissonGpuBackend.cppm,
  src/runtime/Modules/ProgressivePoisson/Runtime.ProgressivePoissonGpuBackend.cpp,
  tests/contract/runtime/Test.ProgressivePoissonGpuBackend.cpp,
  tests/integration/runtime/Test.ProgressivePoissonGpuResultReadbackGpuSmoke.cpp]
- **Dependencies**: [C07]
- **Tags**: Progressive Poisson, graphics, runtime, GPU readback, Vulkan,
  transport
- **From staging**: O64

## C10: Runtime compute-result readback has one shared lifecycle
- **Statement**: Every censused runtime compute-result workflow uses one
  copied, multi-range `Graphics.GpuTransfer` batch with feature-owned typed
  parsing and canonical `JobService` waiting/publication. The superseded
  `Runtime.AsyncBufferReadback` and `Runtime.GpuReadbackJob` modules, build
  entries, and wrapper-only tests are absent. Renderer `SelectionReadback`
  remains an explicitly separate frame-correlated lifecycle.
- **Status**: supported — CPU/fake-queue contracts plus ASan+UBSan promoted
  Vulkan on NVIDIA GeForce RTX 3050, driver 590.48.01; Progressive Poisson
  compute/public parity remains excluded and owned by METHOD-014
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: A runtime compute-result producer bypasses the
  shared transfer batch, reintroduces either retired module or a feature-named
  result queue/service, performs a blocking result `IDevice::ReadBuffer`, lets
  the transport interpret feature semantics, or folds renderer selection into
  this lifecycle.
- **Proof**: [tasks/done/RUNTIME-195-unified-gpu-result-readback.md,
  src/graphics/renderer/Graphics.GpuTransfer.cppm,
  src/graphics/renderer/Graphics.GpuTransfer.cpp,
  src/runtime/Modules/Clustering/Runtime.ClusteringGpuBackend.cpp,
  src/runtime/Modules/ProgressivePoisson/Runtime.ProgressivePoissonGpuBackend.cpp,
  tests/contract/runtime/Test.GpuResultReadbackJob.cpp,
  tests/contract/runtime/Test.RuntimeEngineLayering.cpp,
  tests/integration/runtime/Test.GpuResultReadbackGpuSmoke.cpp,
  docs/api/generated/module_inventory.md]
- **Dependencies**: [C06, C08, C09]
- **Tags**: graphics, runtime, GPU readback, JobService, Vulkan, retirement
- **From staging**: O65

## C11: Runtime K-Means has one typed CPU/GPU operation
- **Statement**: Every production K-Means caller uses
  `ClusteringService::RunKMeans`; CPU reference, operational Vulkan compute,
  honest CPU fallback, cancellation, stale validation, selected-entity
  label/color writeback, and visualization refresh terminate through the same
  typed completion/change events. Vulkan recorder/cache/readback details are a
  non-exported clustering module partition, and Sandbox/config/benchmark code
  exposes no parallel backend, queue, or result family.
- **Status**: supported — CPU/Null contracts plus ASan+UBSan promoted Vulkan on
  NVIDIA GeForce RTX 3050, driver 590.48.01; no performance improvement claim
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: A production caller bypasses `ClusteringService`,
  a public K-Means backend/queue/duplicate facade DTO is reintroduced, config
  and UI map different requests, fallback telemetry is false, stale/cancelled
  work publishes success, label/color writeback omits the change event, or the
  actual-Vulkan service fixture disagrees with the CPU reference.
- **Proof**: [src/runtime/Modules/Clustering/Runtime.ClusteringModule.cppm,
  tasks/done/RUNTIME-196-canonical-clustering-service-path.md,
  src/runtime/Modules/Clustering/Runtime.ClusteringModule.cpp,
  src/runtime/Modules/Clustering/Runtime.ClusteringGpuState.cpp,
  src/runtime/Config/internal/Runtime.FeatureConfigCodecs.Detail.cpp,
  src/app/Sandbox/Editor/Sandbox.MethodPanels.cpp,
  tests/contract/runtime/Test.ClusteringModule.cpp,
  tests/contract/runtime/Test.RuntimeEnginePrivateGlue.cpp,
  tests/integration/runtime/Test.SandboxConfigSections.cpp,
  tests/integration/runtime/Test.ClusteringServiceGpuSmoke.cpp,
  benchmarks/geometry/Bench_KMeansGpuVulkanSmoke.cpp]
- **Dependencies**: [C06, C08]
- **Tags**: K-Means, clustering, runtime, config, Vulkan, parity, retirement
- **From staging**: O66

## C12: General geometry presentation has one authored/runtime/extraction path
- **Statement**: Mesh, graph, point-cloud, composition, and procedural geometry
  use one authored `GeometryPresentationRecipe`, a separate runtime-only
  `GeometryPresentationRuntimeState`, and a copied generation-qualified
  `GeometryPresentationSnapshot`. Scene documents write only authored recipe
  state, retain legacy wire-key read compatibility, and graphics receives no
  live ECS, property, job, asset-service, RHI, or Vulkan ownership. The former
  progressive-named general modules and their dedicated tests are absent.
- **Status**: supported — CPU/Null contracts plus ASan+UBSan promoted Vulkan on
  NVIDIA GeForce RTX 3050, driver 590.48.01; no performance claim
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: Runtime readiness, generated outputs,
  diagnostics, or generations are serialized; graphics receives a live
  recipe/state/ECS/service reference; stale generations commit; a supported
  geometry domain bypasses the shared projection; either retired module or
  symbol returns; or the operational Vulkan fixture fails to reach a frame
  through the recipe/state path.
- **Proof**: [tasks/done/RUNTIME-193-general-geometry-presentation-recipe.md,
  src/runtime/GeometryIntegration/Runtime.GeometryPresentation.cppm,
  src/runtime/GeometryIntegration/Runtime.GeometryPresentation.cpp,
  src/runtime/Scene/Runtime.SceneSerialization.cpp,
  src/runtime/Rendering/Runtime.RenderExtraction.cpp,
  src/runtime/AssetWorkflow/Runtime.AssetWorkflowModule.cpp,
  tasks/done/RUNTIME-191-unified-property-texture-bake-pipeline.md,
  tests/contract/runtime/Test.GeometryPresentation.cpp,
  tests/contract/runtime/Test.RuntimeSceneSerialization.cpp,
  tests/contract/runtime/Test.RuntimeEngineLayering.cpp,
  tests/integration/runtime/Test.RuntimeSandboxAcceptanceGpuSmoke.cpp,
  docs/api/generated/module_inventory.md]
- **Dependencies**: []
- **Tags**: runtime, geometry presentation, serialization, extraction, Vulkan,
  retirement
- **From staging**: O67

## C13: Runtime-authored geometry has one upload and residency lifecycle
- **Statement**: Mesh, graph, point-cloud, procedural, mesh-edge, and
  mesh-vertex extraction build owning graphics-only `GeometryUploadPlan`
  values through private typed runtime adapters and reconcile them through one
  `GeometryResidencyCoordinator`. Runtime retains ECS, topology, and canonical
  `GeometryPropertyRef` ownership but does not directly upload, partially
  update, or free geometry. The five public packer modules, procedural cache,
  per-domain retirement queues, forwarding ticks, and wrapper-only tests are
  absent; only truthful mesh-topology and primitive-view value modules remain.
- **Status**: supported — CPU/Null contracts plus ASan+UBSan promoted Vulkan on
  NVIDIA GeForce RTX 3050, driver 590.48.01; no storage-policy or performance
  claim
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: A live geometry lane bypasses the shared plan or
  coordinator; runtime directly calls geometry upload, channel update, or free;
  a retired packer/cache/retirement owner returns; a typed adapter leaks ECS or
  runtime state into graphics; exact payload, generation, update, sharing, or
  deferred-retirement contracts fail; or any of the six lanes fails the
  operational Vulkan fixtures.
- **Proof**: [tasks/done/RUNTIME-197-unified-geometry-upload-residency-coordinator.md,
  src/graphics/renderer/Graphics.GeometryResidency.cppm,
  src/graphics/renderer/Graphics.GeometryResidency.cpp,
  src/runtime/GeometryIntegration/Runtime.GeometryPlanBuilders.cppm,
  src/runtime/Rendering/Runtime.RenderExtraction.Geometry.cpp,
  src/runtime/GeometryIntegration/Runtime.MeshSurfaceTopology.cppm,
  src/runtime/GeometryIntegration/Runtime.MeshPrimitiveView.cppm,
  tests/contract/graphics/Test.GeometryResidency.cpp,
  tests/contract/runtime/Test.RuntimeEngineLayering.cpp,
  tests/contract/runtime/Test.MeshGeometryExtraction.cpp,
  tests/contract/runtime/Test.GraphGeometryExtraction.cpp,
  tests/contract/runtime/Test.PointCloudGeometryExtraction.cpp,
  tests/contract/runtime/Test.ProceduralGeometryExtraction.cpp,
  tests/contract/runtime/Test.MeshPrimitiveViewExtraction.cpp,
  tests/integration/runtime/Test.RuntimeSandboxAcceptanceGpuSmoke.cpp,
  docs/api/generated/module_inventory.md]
- **Dependencies**: [C12]
- **Tags**: runtime, graphics, geometry, residency, extraction, Vulkan,
  retirement
- **From staging**: O68

## C14: Runtime visualization has one closed recipe and pure encoding path
- **Statement**: Production scalar, color, label, vector-field, isoline,
  curvature-direction, and supported Htex metadata visualization intent is
  represented by the closed `VisualizationRecipe` value and encoded by pure
  typed functions into the existing graphics packet/property-residency seams.
  `VisualizationConfig`, `GeometryPresentationRecipe`, and explicit copied
  recipes converge during runtime extraction; Htex recreation is a separate
  typed `JobService` operation. The former adapter interface, concrete
  wrappers, registry/map, opaque binding keys, registration forwarding, and
  wrapper-only tests are absent.
- **Status**: supported — CPU/Null and focused ASan contracts, complete UBSan
  CPU coverage, plus ASan+UBSan promoted Vulkan readback on NVIDIA GeForce RTX
  3050, driver 590.48.01; the unrelated complete-ASan red gate is recorded by
  BUG-122 and no performance claim is made
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: A production visualization path bypasses the
  closed recipe/pure encoder, reintroduces an adapter lifetime, registry,
  opaque key, or registration surface; encoding schedules background work;
  supported inputs lose deterministic validation or packet/residency parity;
  or either operational Vulkan visualization readback fails.
- **Proof**: [tasks/done/RUNTIME-198-data-driven-visualization-recipes.md,
  src/runtime/Visualization/Runtime.VisualizationRecipes.cppm,
  src/runtime/Visualization/Runtime.VisualizationRecipes.cpp,
  src/runtime/Rendering/Runtime.RenderExtraction.Recipes.cpp,
  src/runtime/Rendering/Runtime.RenderExtraction.cpp,
  tests/contract/runtime/Test.VisualizationRecipes.cpp,
  tests/integration/runtime/Test.RuntimeRenderExtraction.cpp,
  tests/integration/graphics/Test.VisualizationOverlaySurfaceGpuSmoke.cpp,
  docs/adr/0009-visualization-packets-and-overlay-upload.md,
  docs/api/generated/module_inventory.md]
- **Dependencies**: [C12, C13]
- **Tags**: runtime, graphics, visualization, recipes, extraction, Vulkan,
  retirement
- **From staging**: O69

## C15: Runtime property-to-texture baking has one production GPU lifecycle
- **Statement**: Production mesh surface property-to-texture requests use one
  canonical `TextureBakeService` request/result vocabulary, one
  `Runtime.TextureBakeModule` lifecycle, one bounded `JobService` GPU
  participant, and one `Graphics.PropertyTextureBake` recorder. Callers prepare
  named source properties and reconcile completed output assets; the baker
  owns no material, presentation, visualization, normal-space, or consumer
  semantics. The selected-mesh, CPU mesh-attribute, and specialized
  object-space-normal runtime/graphics modules and shaders are absent.
- **Status**: supported — CPU/Null contracts plus ASan+UBSan promoted Vulkan
  readback on NVIDIA GeForce RTX 4090, driver 580.159.04; no performance claim
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: A live editor, agent, import, or default-policy
  request bypasses `TextureBakeService`; runtime registers multiple
  property-texture GPU participants; the baker regains consumer semantics or a
  live CPU fallback; a retired module/shader/vocabulary returns; or either
  exact promoted-Vulkan import/reconciliation readback fails.
- **Proof**: [tasks/done/RUNTIME-191-unified-property-texture-bake-pipeline.md,
  src/runtime/Modules/TextureBake/Runtime.TextureBakeModule.cppm,
  src/runtime/Modules/TextureBake/Runtime.TextureBakeModule.cpp,
  src/runtime/AssetWorkflow/Runtime.AssetWorkflowModule.cpp,
  src/graphics/renderer/Graphics.PropertyTextureBake.cppm,
  src/graphics/renderer/Graphics.PropertyTextureBake.cpp,
  tests/contract/runtime/Test.TextureBakeModule.cpp,
  tests/contract/runtime/Test.AssetWorkflowModule.cpp,
  tests/contract/runtime/Test.RuntimeEngineLayering.cpp,
  tests/integration/runtime/Test.RuntimeSandboxAcceptanceGpuSmoke.cpp,
  docs/api/generated/module_inventory.md]
- **Dependencies**: [C12]
- **Tags**: runtime, graphics, property texture bake, Vulkan, parity,
  retirement
- **From staging**: O70

## C16: Runtime entity mutations have one undoable history transaction
- **Statement**: Production undoable runtime entity and geometry edits use the
  include-only `ExecuteUndoableEntityMutation` transaction and the single
  `EditorCommandHistory`. Transform, gizmo, visualization/presentation,
  render-hint, geometry property/topology/domain conversion, parameterization,
  vertex-channel/normal, clustering, and Progressive Poisson owners retain
  their exact typed state, generation validation, atomic apply, and dirty
  policy. Import creation, authoring, and enrichment remain a deliberately
  non-undoable dirty-only document lifecycle. The former gizmo stack,
  CommandBus inverse-history hook, and mutation-specific history adapters are
  absent.
- **Status**: supported — focused and complete CPU/Null contracts; no
  GPU/Vulkan or performance claim
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: An undoable production entity/geometry edit
  bypasses the internal transaction or commits more than one history record; a
  queued result publishes after its captured entity/source/presentation state
  becomes stale; undo/redo fails to restore the feature-owned exact state; a
  second undo stack, CommandBus inverse hook, or mutation-specific history
  builder returns; or import lifecycle starts creating partial undo records.
- **Proof**: [tasks/done/RUNTIME-201-unified-editor-mutation-history-transaction.md,
  src/runtime/Editor/internal/Runtime.EditorMutation.Internal.hpp,
  src/runtime/Editor/Runtime.EditorCommandHistory.cppm,
  src/runtime/Editor/Runtime.EditorCommandHistory.cpp,
  src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp,
  src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp,
  src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp,
  src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.cpp,
  src/runtime/Editor/Operations/Runtime.ParameterizationOperations.cpp,
  src/runtime/Gizmos/Runtime.GizmoInteraction.cpp,
  src/runtime/Modules/Clustering/Runtime.ClusteringModule.cpp,
  tests/contract/runtime/Test.EditorCommandHistory.cpp,
  tests/contract/runtime/Test.RuntimeEngineLayering.cpp,
  tests/contract/runtime/Test.SandboxEditorVisualization.cpp,
  tests/contract/runtime/Test.SandboxEditorMeshMethods.cpp,
  tests/contract/runtime/Test.SandboxEditorClusteringMethods.cpp,
  tests/contract/runtime/Test.AssetImportFormatCoverage.cpp,
  docs/architecture/runtime.md]
- **Dependencies**: [C06, C11, C12, C14]
- **Tags**: runtime, editor, mutation, history, undo, redo, CPU, retirement
- **From staging**: O76

## C17: Audited geometry method-integration gaps have bounded owners
- **Statement**: At `main` revision `577b4583` plus the LOP integration at
  revision `33930efab13764cbbd0887bfc8c726948a480479`, the declared audit scope
  contains one foundational ECS element-source drift and five method
  engine-integration gaps: Progressive Poisson, LOP/WLOP/CLOP/EAR, ICP,
  statistical/radius outlier processing, and Signed Heat. `HARDEN-087`,
  `RUNTIME-206` through `RUNTIME-210`, and `UI-038` through `UI-042` assign a
  bounded dependency-correct owner to every finding.
- **Status**: supported — source audit, stable-reference bibliographic
  cross-check, and strict task-contract validation; this is not sealed A24
  literature intake and makes no method-correctness, CPU/GPU parity,
  performance, sanitizer, or Vulkan claim
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: A production geometry package or callable
  Sandbox geometry method inside the audit's stated revisions and inclusion
  rules violates either catalog contract without appearing in the inventory;
  any listed integration is source-conforming; or any finding lacks a unique
  actionable task chain.
- **Proof**: [docs/reviews/2026-08-02-method-engine-integration-contract-audit.md,
  docs/architecture/contract-catalog.yaml,
  tasks/done/HARDEN-087-unified-geometry-element-source-components.md,
  tasks/done/RUNTIME-206-lop-element-domain-source-integration.md,
  tasks/backlog/runtime/RUNTIME-207-icp-element-domain-source-integration.md,
  tasks/done/RUNTIME-208-progressive-poisson-element-domain-publication.md,
  tasks/backlog/runtime/RUNTIME-209-point-set-outlier-analysis-publication.md,
  tasks/backlog/runtime/RUNTIME-210-signed-heat-runtime-config-integration.md,
  tasks/done/UI-038-progressive-poisson-multi-domain-panel.md,
  tasks/done/UI-039-lop-multi-domain-discovery.md,
  tasks/backlog/ui/UI-040-icp-compatible-source-selection.md,
  tasks/backlog/ui/UI-041-point-set-outlier-multi-domain-panel.md,
  tasks/backlog/ui/UI-042-signed-heat-mesh-panel.md,
  tools/agents/validate_tasks.py,
  tests/regression/tooling/Test.ValidateTasks.py,
  N330]
- **Dependencies**: []
- **Tags**: geometry, methods, runtime, UI, contracts, source audit, backlog
- **From staging**: O95

## C18: Geometry entities expose one canonical element-source matrix
- **Statement**: ECS materialization publishes point clouds as `Vertices`,
  graphs as `Vertices + Halfedges + Edges`, and meshes as
  `Vertices + Halfedges + Edges + Faces`. Graph and mesh use the same physical
  component types while `HasGraphTopology` and the provenance query distinguish
  graph semantics from mesh semantics. Graph
  halfedges carry the graph's real connectivity and custom element properties;
  runtime method bindings, `GraphHalfedge` property lookup, extraction,
  fail-closed topology-validated scene serialization, and Sandbox domain models
  consume those sources without a converter, fabricated faces, or duplicate
  graph-node storage.
- **Status**: supported — focused and complete CPU plus ASan and UBSan
  contracts; no GPU/Vulkan or performance claim
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: A materialized point cloud, graph, or mesh has a
  different physical element-source matrix; graph and mesh vertices or
  halfedges use different component types; graph connectivity is reconstructed
  from edge endpoints or graph faces are fabricated; graph provenance is
  inferred only from source presence; malformed graph topology is published by
  scene load; graph-halfedge properties lack a canonical runtime/UI discovery
  domain; or a production runtime/UI consumer requires duplicate `Nodes`
  storage or a domain converter.
- **Proof**: [tasks/done/HARDEN-087-unified-geometry-element-source-components.md,
  src/ecs/Components/ECS.Component.GeometrySources.cppm,
  src/ecs/Components/ECS.Component.GeometrySources.cpp,
  src/ecs/Components/ECS.Component.GeometrySourcesPopulate.cpp,
  src/geometry/Geometry.Graph.cpp,
  src/runtime/GeometryIntegration/Runtime.GeometryAvailability.cpp,
  src/runtime/GeometryIntegration/Runtime.GeometryPlanBuilders.Graph.cpp,
  src/runtime/Scene/Runtime.SceneSerialization.cpp,
  tests/unit/ecs/Test.ECS.GeometrySourcesPopulate.cpp,
  tests/unit/geometry/Test_RuntimeGraph.cpp,
  tests/contract/runtime/Test.GeometryAvailability.cpp,
  tests/contract/runtime/Test.GraphGeometryExtraction.cpp,
  tests/contract/runtime/Test.RuntimeSceneSerialization.cpp,
  tests/contract/runtime/Test.SandboxEditorModels.cpp,
  tests/integration/runtime/Test.RuntimeSandboxAcceptance.cpp,
  docs/architecture/ecs.md,
  docs/architecture/runtime.md]
- **Dependencies**: [C12, C17]
- **Tags**: ecs, geometry, runtime, UI, element sources, graph halfedges,
  CPU, ASan, UBSan, retirement
- **From staging**: O96

## C19: Verification redesign latency and total-work targets are achievable
- **Statement**: On a matched accepted-change corpus, the planned verification
  system can reach implementation-unit edit p95 at or below 10 seconds,
  module-interface edit p95 at or below 60 seconds, focused pull-request p95 at
  or below 3 minutes, broad pull-request p95 at or below 8 minutes,
  merge-group p95 at or below 10 minutes, and at least 50 percent less executed
  action time while every CI-017/CI-018 quality-admission dimension remains
  non-regressed.
- **Status**: hypothesis — target declaration only; no implementation or matched
  measurement exists
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: Frozen claim-grade matched results miss any
  declared p95 or executed-work threshold, require a quality/capability gate to
  be weakened, show an unexplained selection/cache divergence, or cannot
  reproduce the target under the declared environment and workload identities.
- **Proof**: [docs/architecture/verification-evidence-architecture.md,
  tasks/backlog/process/BUILD-006-cxx23-module-build-backend-bakeoff.md,
  tasks/backlog/process/CI-017-test-quality-and-fault-detection-oracle.md,
  tasks/backlog/process/CI-018-hybrid-impact-selection-admission.md,
  tasks/backlog/process/CI-020-verification-cutover-and-legacy-retirement.md,
  N334, N335]
- **Dependencies**: []
- **Tags**: CI, testing, latency, total work, quality admission, hypothesis
- **From staging**: O101
## C20: Progressive Poisson publishes on every canonical vertex source
- **Statement**: The production Progressive Poisson runtime operation accepts
  the existing `Vertices` source of point-cloud, graph, and mesh entities
  through one direct/queued/config-controlled operation path. It publishes
  source-cardinality `v:poisson_level`, `v:poisson_rank`,
  `v:poisson_splat_radius`, and `v:poisson_prefix_visible` properties, including
  deterministic rejected-sample sentinels, without replacing the source domain
  or changing topology, source order, provenance, presentation, or unrelated
  properties. Requested GPU execution remains an explicit CPU-reference
  fallback through the same publication path.
- **Status**: supported — 46/46 focused runtime/UI contracts; 4,003 passed plus
  one policy-defined skip from 4,004 selected CPU tests; 2,656/2,656 ASan; and
  2,655 passed plus one LSan-only skip from 2,656 selected UBSan tests. This
  makes no operational GPU/Vulkan compute, backend-parity, performance, or
  Graph-panel claim
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: A valid mesh, graph, or point-cloud entity with a
  canonical `Vertices` source cannot execute Progressive Poisson; two source
  domains use different operation/publication paths or produce different
  deterministic channels for identical positions and parameters; publication
  changes source cardinality, topology, element order, provenance,
  presentation, or unrelated properties; rejected vertices lack the documented
  sentinels; a same-cardinality typed connectivity edit is overwritten rather
  than rejected as stale; config/agent execution bypasses validation; or a
  requested GPU backend reports operational GPU execution rather than the
  declared CPU fallback.
- **Proof**: [tasks/done/RUNTIME-208-progressive-poisson-element-domain-publication.md,
  methods/geometry/progressive_poisson/method.yaml,
  methods/geometry/progressive_poisson/paper.md,
  src/runtime/Modules/ProgressivePoisson/Runtime.ProgressivePoissonConfig.cppm,
  src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.cppm,
  src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.cpp,
  src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp,
  src/app/Sandbox/Editor/Sandbox.MethodPanels.cpp,
  tests/contract/runtime/Test.SandboxEditorClusteringMethods.cpp,
  tests/contract/runtime/Test.SandboxEditorMeshMethods.cpp,
  tests/contract/runtime/Test.SandboxEditorModels.cpp,
  tests/integration/runtime/Test.SandboxEditorPresentation.cpp,
  docs/reviews/2026-08-03-runtime-208-clean-workshop-review.md]
- **Dependencies**: [C17, C18]
- **Tags**: runtime, geometry, methods, progressive Poisson, element sources,
  config, history, CPU, ASan, UBSan, retirement
- **From staging**: O102

## C21: Progressive Poisson has one three-domain Sandbox panel path
- **Statement**: The Sandbox registers Progressive Poisson under Mesh, Graph,
  and PointCloud Processing through one shared app-owned panel state and the
  same runtime-owned copied readiness, validated config/apply/run operation,
  diagnostics, backend-fallback status, and source-cardinality
  rank/level/splat-radius/prefix-visibility channels. Missing, empty,
  wrong-typed, and non-finite vertex positions disable execution with an
  explicit copied reason. No registration converts the entity, samples a mesh
  surface, replaces topology, or publishes result properties from the app.
- **Status**: supported — 3/3 focused headless ImGui and 33/33 Progressive
  Poisson cases passed; the default CPU-supported selector selected 4,006,
  passed 4,005, and explicitly skipped one environment-gated
  GLFW/LeakSanitizer case. This makes
  no operational GPU/Vulkan compute, backend-parity, or performance claim
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: A loaded valid mesh, graph, or point cloud lacks
  the Progressive Poisson domain-menu registration; a domain owns a separate
  config or execution implementation; malformed vertex positions leave Run
  enabled or provide no reason; the panel exposes conversion/surface-sampling
  controls, changes topology/cardinality, or publishes properties directly;
  or result-channel/backend-fallback status differs by registration.
- **Proof**: [tasks/done/UI-038-progressive-poisson-multi-domain-panel.md,
  tasks/evidence/UI-038/report.yaml,
  src/app/Sandbox/Editor/Sandbox.MethodPanels.cpp,
  src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp,
  src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.cppm,
  tests/contract/runtime/Test.SandboxEditorModels.cpp,
  tests/integration/runtime/Test.SandboxEditorPresentation.cpp,
  methods/geometry/progressive_poisson/README.md,
  docs/architecture/runtime.md,
  docs/reviews/2026-08-02-method-engine-integration-contract-audit.md]
- **Dependencies**: [C18, C20]
- **Tags**: app, runtime, UI, geometry, methods, progressive Poisson,
  element sources, config, diagnostics, CPU, retirement
- **From staging**: O103
## C22: LOP and WLOP have a deterministic CPU reference contract
- **Statement**: On the checked-in analytic screening fixtures and the
  independently parameterized built-in confirmation cohort, the METHOD-016
  CPU reference implements finite deterministic LOP/WLOP consolidation,
  improves the declared plane/sphere error and repulsion-uniformity measures,
  and fails closed for invalid or resource-bounded requests.
- **Status**: supported — Clang 23 CPU correctness, serial ASan/UBSan geometry
  groups, and independently accepted schema-v2 claim-grade confirmation
  evidence; no scanner-data generalization, runtime/UI, GPU, optimized-backend,
  or performance claim
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: The one-step equation oracle diverges; identical
  seeded calls differ under repetition or concurrent callers; finite supported
  fixtures produce non-finite output; invalid/resource-bounded requests publish
  positions; or the disjoint confirmation cohort exceeds quality-error L2
  `0.03` or loses its denoising/uniformity/outlier gates.
- **Proof**: [tasks/done/METHOD-016-locally-optimal-projection-reference-backend.md,
  src/geometry/Geometry.PointCloud.Consolidation.cpp,
  tests/unit/geometry/Test.PointCloudConsolidation.cpp,
  benchmarks/geometry/manifests/locally_optimal_projection_reference_smoke.yaml,
  tasks/evidence/METHOD-016/experiment/inputs/benchmark_result.json,
  tasks/evidence/METHOD-016/experiment/runs/run-001/bundle.yaml]
- **Dependencies**: []
- **Tags**: geometry, point cloud, LOP, WLOP, CPU reference, deterministic
- **From staging**: O104

## C23: Continuous LOP has a deterministic original-equation CPU reference
- **Statement**: On the checked-in analytic screening fixtures and disjoint
  built-in confirmation cohort, METHOD-017 fits a deterministic Gaussian
  mixture and evaluates the original CLOP three-Gaussian-product attraction
  with the shared finite repulsion contract, bounded component work, and
  fail-closed invalid-input handling.
- **Status**: supported — Clang 23 CPU correctness, serial ASan/UBSan geometry
  groups, and independently accepted schema-v2 claim-grade confirmation
  evidence; no hierarchical-EM equivalence, scanner-data generalization,
  incomplete-gamma extension, runtime/UI, GPU, optimized-backend, or
  performance claim
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: The Gaussian-product equation oracle diverges;
  identical seeded calls differ under repetition or concurrent callers;
  finite supported fixtures produce non-finite output; invalid mixture bounds
  publish positions; or the disjoint confirmation cohort exceeds
  quality-error L2 `0.03` or loses its plane/sphere denoising, WLOP-parity,
  component-work, uniformity, outlier, convergence, or failure-state gates.
- **Proof**: [tasks/done/METHOD-017-continuous-lop-clop-reference-backend.md,
  src/geometry/Geometry.PointCloud.Consolidation.cpp,
  tests/unit/geometry/Test.PointCloudConsolidation.cpp,
  benchmarks/geometry/manifests/continuous_lop_reference_smoke.yaml,
  tasks/evidence/METHOD-017/experiment/inputs/benchmark_result.json,
  tasks/evidence/METHOD-017/experiment/runs/run-001/bundle.yaml,
  tasks/evidence/METHOD-017/experiment/runs/run-001/audit.json]
- **Dependencies**: [C22]
- **Tags**: geometry, point cloud, CLOP, Gaussian mixture, CPU reference,
  deterministic
- **From staging**: O105

## C24: Edge-Aware Resampling has a deterministic original-equation CPU reference
- **Statement**: On the checked-in analytic screening fixtures and disjoint
  built-in confirmation cohort, METHOD-018 implements deterministic
  anisotropic WLOP and the original two-stage EAR reference, consumes valid
  oriented normals without mutation or estimates them through the shared
  normal contract, preserves the declared dihedral feature contrast, and
  returns the requested progressive-insertion count with explicit fail-closed
  states.
- **Status**: supported — Clang 23 CPU correctness, serial ASan/UBSan geometry
  groups, and independently accepted schema-v2 claim-grade confirmation
  evidence; no later-method equivalence, scanner-data generalization,
  runtime/UI, GPU, optimized-backend, or performance claim
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: The directional-weight or signed bilateral-normal
  equation oracle diverges; identical seeded calls differ under repetition or
  concurrent callers; authored normals are mutated; unsupported or invalid
  normal/input/resource states publish positions; the confirmation cohort
  exceeds quality-error L2 `0.03`, fails its edge/flat-region/count/uniformity
  gates, or places fewer than all eight inserted samples near the feature.
- **Proof**: [tasks/done/METHOD-018-edge-aware-resampling-anisotropic-lop-reference-backend.md,
  src/geometry/Geometry.PointCloud.Consolidation.cpp,
  src/geometry/Geometry.PointCloud.Kernels.cpp,
  tests/unit/geometry/Test.PointCloudConsolidation.cpp,
  benchmarks/geometry/manifests/edge_aware_resampling_reference_smoke.yaml,
  tasks/evidence/METHOD-018/experiment/inputs/benchmark_result.json,
  tasks/evidence/METHOD-018/experiment/runs/run-001/bundle.yaml,
  tasks/evidence/METHOD-018/experiment/runs/run-001/audit.json]
- **Dependencies**: [C22]
- **Tags**: geometry, point cloud, EAR, anisotropic WLOP, CPU reference,
  deterministic
- **From staging**: O106

## C25: Exact execution-only LOP-family candidates provide useful CPU acceleration
- **Statement**: Exact traversal-scratch, sorted-neighborhood reuse,
  Gaussian-product cache, underflow-only pruning, and local EAR scan changes
  were expected to preserve the LOP/WLOP/CLOP/EAR reference results while
  reducing at least one strategy's paired median runtime ratio to `<= 0.80`.
- **Status**: refuted — on the preregistered single-thread Clang 23 confirmation
  run at clean implementation revision `cfd0d9bd`, all four candidates passed
  parity but the LOP/WLOP/CLOP/EAR ratios were respectively `0.961823997`,
  `0.966844794`, `0.998817796`, and `1.037145476`; no strategy was adopted and
  no public optimized backend/config/UI token was added
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: The hypothesis required at least one strategy to
  retain the frozen parity, determinism, identity, and no-fallback contract
  while reporting a paired median optimized/reference runtime ratio `<= 0.80`
  on the fixed confirmation suite.
- **Proof**: [methods/geometry/locally_optimal_projection/reports/METHOD-019-result.md,
  benchmarks/geometry/manifests/lop_family_comparison_smoke.yaml,
  tasks/evidence/METHOD-019/experiment/inputs/benchmark_result.json,
  tasks/evidence/METHOD-019/experiment/inputs/strategy_rows.jsonl,
  tasks/evidence/METHOD-019/experiment/runs/run-001/bundle.yaml]
- **Dependencies**: [C22, C23, C24]
- **Tags**: geometry, point cloud, LOP, WLOP, CLOP, EAR, optimized CPU,
  negative result
- **From staging**: O107

## C26: Parameterization optimization kernels have a deterministic CPU contract
- **Statement**: For supported triangle meshes and positive-orientation UV
  maps, `Geometry.Parameterization.Optimize` deterministically prepares
  face-storage-aligned reference data, fits signed-SVD proper rotations,
  evaluates the full area-weighted symmetric-Dirichlet objective and analytic
  gradient, assembles gradient-matching ARAP/SLIM proxy systems, and selects a
  finite energy-decreasing step strictly inside the first local flip root.
  Invalid, degenerate, and non-finite inputs fail through explicit statuses;
  reflected maps retain a proper ARAP rotation but are rejected by the
  orientation-aware objective/proxy barrier, and no successful record contains
  a non-finite payload.
- **Status**: supported — Clang 23 focused and complete CPU contracts plus
  focused ASan and UBSan; no method-level convergence, global-boundary
  bijectivity, runtime/UI, GPU/Vulkan, or performance claim
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: A supported fixture produces an improper local
  rotation, an analytic gradient or proxy direction disagrees with finite
  differences, a proxy normal matrix is asymmetric or indefinite, a returned
  injective step reaches/crosses the first flip root or increases energy,
  identical inputs differ bitwise on one host, or malformed/non-finite input
  returns success or a successful non-finite payload.
- **Proof**: [tasks/done/GEOM-064-parameterization-optimization-kernels.md,
  src/geometry/Geometry.Parameterization.Optimize.cppm,
  src/geometry/Geometry.Parameterization.Optimize.cpp,
  tests/unit/geometry/Test.ParameterizationOptimize.cpp,
  tasks/evidence/GEOM-064/commands/revision4-parameterization-tests.json,
  tasks/evidence/GEOM-064/commands/revision4-cpu-tests.json,
  tasks/evidence/GEOM-064/commands/revision4-asan-parameterization-tests.json,
  tasks/evidence/GEOM-064/commands/revision4-ubsan-parameterization-tests.json]
- **Dependencies**: []
- **Tags**: geometry, parameterization, ARAP, SLIM, CPU, deterministic,
  local injectivity
- **From staging**: O112

## C27: Runtime physics is operational through the CPU/Null engine loop
- **Statement**: With `sandbox.physics` enabled, the app-composed
  `PhysicsModule` lazily owns isolated physics worlds and `StableId` sidecars
  per encountered runtime world, synchronizes valid collider/rigid-body
  authoring, executes a bounded fixed-step `Physics::World::SolveStep` lane
  after promoted ECS simulation, writes only changed dynamic poses back with
  transform dirty tags, and clears exact state on disable, world destruction,
  and shutdown. Its public surface exposes config and copied diagnostics but no
  physics world, body handle, parallel service, or exact self-publication.
- **Status**: supported — Clang 23 focused and complete CPU/Null contracts,
  complete ASan and UBSan CPU cohorts, a linked Sandbox executable, and an
  accepted independent fixed-surface implementation review; no GPU/Vulkan
  physics, persistence, contact-event, dedicated-UI, or performance claim is
  made
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: Enabling the validated config fails to create
  isolated state for an active world; unchanged ECS authoring resets a
  simulated pose or velocity; the fixed-step budget is unbounded; static or
  kinematic ECS poses are overwritten; dynamic writeback omits dirty tags;
  disable, world destruction, or shutdown retains owned state; the generic
  Engine imports the concrete module; or the public module exposes live
  physics handles/state or an unconsumed service publication.
- **Proof**: [tasks/done/PHYSICS-004-operational-runtime-physics-module.md,
  src/runtime/Modules/PhysicsIntegration/Runtime.PhysicsModule.cppm,
  src/runtime/Modules/PhysicsIntegration/Runtime.PhysicsModule.cpp,
  src/runtime/Kernel/Runtime.Engine.cpp,
  src/app/Sandbox/main.cpp,
  tests/integration/runtime/Test.PhysicsModule.cpp,
  tests/integration/runtime/Test.SandboxConfigSections.cpp,
  tests/contract/runtime/Test.RuntimeEngineLayering.cpp,
  tasks/evidence/PHYSICS-004/commands/final-ci-tests.json,
  tasks/evidence/PHYSICS-004/commands/final-ci-asan-tests.json,
  tasks/evidence/PHYSICS-004/commands/final-ci-ubsan-tests.json]
- **Dependencies**: []
- **Tags**: runtime, physics, ECS, fixed step, config, CPU, Null, Operational
- **From staging**: O113
## C28: LOP-family runtime publication accepts every geometry element domain
- **Statement**: `PointCloudConsolidationService` resolves a caller-selected
  finite `vec3` property on mesh vertices, edges, halfedges, or faces; graph
  nodes, edges, or halfedges; and point-cloud points. A same-cardinality
  LOP/WLOP/CLOP/EAR result publishes only named output properties to the
  originating element-domain `PropertySet`, participates in exact undo/redo,
  and rejects stale source values. Topology-bearing cardinality changes and a
  CLOP component count larger than the selected input fail before job
  submission; only canonical point-cloud position replacement may change
  cardinality.
- **Status**: supported — Clang 23 exact module 5/5, complete CPU/Null 4,075
  pass plus one policy skip, ASan 2,670/2,670, and UBSan 2,669 pass plus one
  policy skip; property-aware Sandbox discovery is separately closed by C29,
  and no GPU/Vulkan, optimized-backend, or performance claim is made
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: Any compatible element-domain property is
  rejected because of its container, provenance, or handle wrapper; an
  accepted same-cardinality result publishes outside its originating domain,
  loses unrelated properties/topology, lacks exact history, or survives a
  selected-property mutation; a topology-bearing count change or oversized
  CLOP mixture reaches the job queue; or a non-canonical point-cloud property
  is resized.
- **Proof**: [tasks/done/RUNTIME-206-lop-element-domain-source-integration.md,
  src/runtime/Modules/PointCloudConsolidation/Runtime.PointCloudConsolidationModule.cppm,
  src/runtime/Modules/PointCloudConsolidation/Runtime.PointCloudConsolidationModule.cpp,
  tests/contract/runtime/Test.PointCloudConsolidationModule.cpp,
  tasks/evidence/RUNTIME-206/commands/focused-module-tests-r3.json,
  tasks/evidence/RUNTIME-206/commands/cpu-tests-r2.json,
  tasks/evidence/RUNTIME-206/commands/asan-tests-r2.json,
  tasks/evidence/RUNTIME-206/commands/ubsan-tests-r2.json]
- **Dependencies**: [C18, C22, C23, C24]
- **Tags**: runtime, geometry, point set, property domains, LOP, WLOP, CLOP,
  EAR, CPU, undo, stale writeback
- **From staging**: O115

## C29: The Sandbox binds LOP-family slots across geometry property domains
- **Statement**: Stable Mesh, Graph, and PointCloud Processing registrations
  open one shared consolidation panel/state, which builds its Position,
  optional Normal, and output slots from the selected entity's canonical
  property catalog rather than its container provenance. Compatible
  finite float `vec3` properties on mesh vertex, edge, halfedge, and face;
  graph node, edge, and halfedge; and point-cloud point domains enter the same
  runtime availability, validated config, submit, result, and history path.
  Publication remains on the originating entity/domain, and runtime readiness
  prevents topology-bearing cardinality changes. Canonical published mesh
  area-vector, centroid, and scalar-gradient properties use float `glm::vec3`
  storage while their direct calculations remain double precision.
- **Status**: supported — fresh Clang 23 focused UI/runtime/geometry 50/50,
  three-menu headless presentation 3/3, and complete CPU/Null 4,078 pass plus
  one policy-defined GLFW/LSan skip, with
  strict task, documentation, method-manifest, inventory, layering,
  test-layout, root-hygiene, and clean-workshop gates; no sanitizer,
  GPU/Vulkan, optimized-backend, or performance claim is made
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: Any Mesh, Graph, or PointCloud Processing entry
  is missing or does not open the shared panel path; a compatible float `vec3`
  property is hidden or rejected because of its name, handle wrapper, element
  domain, or mesh/graph provenance; the panel bypasses the runtime
  availability/config/submit path;
  it permits a topology-bearing count change; publication targets another
  entity/domain or mutates geometry directly; or a canonical published mesh
  vector quantity is stored as a public double-vector property.
- **Proof**: [tasks/done/UI-039-lop-multi-domain-discovery.md,
  src/app/Sandbox/Editor/Sandbox.MethodPanels.cpp,
  src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.cppm,
  src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.cppm,
  src/geometry/Geometry.HalfedgeMesh.Utils.cppm,
  tests/integration/runtime/Test.SandboxPointCloudConsolidationPanel.cpp,
  tests/integration/runtime/Test.SandboxEditorPresentation.cpp,
  tests/unit/geometry/Test_MeshQuantities.cpp,
  tasks/evidence/UI-039/commands/review-repair-focused-tests.json,
  tasks/evidence/UI-039/commands/review-repair-presentation-tests.json,
  tasks/evidence/UI-039/commands/review-repair-cpu-tests-r2.json]
- **Dependencies**: [C18, C28]
- **Tags**: app, runtime, geometry, point set, property domains, LOP, WLOP,
  CLOP, EAR, UI, CPU, float properties
- **From staging**: O116

## C30: Canonical LOP publication explicitly invalidates render residency
- **Statement**: An asynchronous LOP-family request changes rendered mesh
  positions only after successful runtime publication. Publishing canonical
  mesh `v:position` explicitly stamps GPU and vertex-position dirty state, and
  render extraction then repacks that channel; queued or still-running work
  has not mutated the property, and publication to another property name does
  not change the canonical rendered position channel.
- **Status**: supported — source-path tracing and focused direct-OBJ,
  LOP-publication, and dirty-position reupload contracts; the bounded
  `child.obj` Vulkan end-to-end proof is recorded separately by C35, with no
  GPU/Vulkan performance claim
- **Provenance**: ai-suggested
- **Crystallized via**: empirical-resolution
- **Falsification criteria**: A successfully published canonical mesh
  position result omits the required dirty state or is not repacked by render
  extraction; a queued/running request mutates the source before publication;
  or publishing only a non-canonical output property changes the packed
  canonical position channel.
- **Proof**: [src/runtime/Modules/PointCloudConsolidation/Runtime.PointCloudConsolidationModule.cpp,
  src/runtime/Rendering/Runtime.RenderExtraction.cpp,
  src/runtime/GeometryIntegration/Runtime.GeometryPlanBuilders.Mesh.cpp,
  tests/contract/runtime/Test.PointCloudConsolidationModule.cpp,
  tests/contract/runtime/Test.AssetImportFormatCoverage.cpp,
  tests/integration/runtime/Test.RuntimeRenderExtraction.cpp]
- **Dependencies**: [C28, C29]
- **Tags**: runtime, geometry, LOP, publication, dirty state, render residency,
  CPU
- **From staging**: O117

## C31: METHOD-020 v1 WLOP fixture cannot adjudicate Vulkan parity
- **Statement**: The frozen `builtin.lop_family.gpu_vulkan.v1` isotropic-WLOP
  fixture is rejected as a GPU confirmation fixture because its CPU-reference
  oracle returns `empty_neighborhood` after one projection iteration with one
  empty projected-density neighborhood, before any Vulkan request is
  submitted. This result says nothing about Vulkan WLOP parity; replacement
  confirmation uses a new v2 dataset and benchmark identity.
- **Status**: refuted — the hypothesis that the v1 suite can evaluate both
  adopted candidates failed its CPU-oracle prerequisite
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: Repeating the exact v1 WLOP float fixture and
  parameters with the frozen CPU reference returns `success` or a finite
  `not_converged` result without an empty projected-density neighborhood.
- **Proof**: [methods/geometry/locally_optimal_projection/reports/METHOD-020-protocol.md,
  methods/geometry/locally_optimal_projection/reports/METHOD-020-v1-screening.md,
  benchmarks/geometry/manifests/lop_family_gpu_vulkan_smoke.yaml]
- **Dependencies**: []
- **Tags**: geometry, LOP, WLOP, CPU reference, Vulkan protocol, refuted
- **From staging**: O124

## C32: METHOD-020 v2 WLOP fixture also cannot adjudicate Vulkan parity
- **Statement**: The frozen `builtin.lop_family.gpu_vulkan.v2` isotropic-WLOP
  fixture is rejected because its CPU-reference oracle returns
  `empty_neighborhood` during the first projected-density calculation, before
  any Vulkan request. METHOD-019 established exact CPU-candidate parity for
  that intentional failure and empty output; it did not establish a usable
  WLOP result for positional GPU parity.
- **Status**: refuted — the hypothesis that METHOD-019's WLOP failure-parity
  fixture was a usable Vulkan confirmation oracle failed its CPU prerequisite
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: Repeating the exact v2 WLOP float fixture and
  parameters with the frozen CPU reference returns `success` or a finite
  `not_converged` result with positions rather than `empty_neighborhood` and
  an empty output.
- **Proof**: [methods/geometry/locally_optimal_projection/reports/METHOD-020-protocol-v2.md,
  methods/geometry/locally_optimal_projection/reports/METHOD-020-v2-screening.md,
  methods/geometry/locally_optimal_projection/reports/METHOD-019-result.md,
  benchmarks/geometry/manifests/lop_family_gpu_vulkan_smoke_v2.yaml]
- **Dependencies**: [C31]
- **Tags**: geometry, LOP, WLOP, CPU reference, Vulkan protocol, refuted
- **From staging**: O125

## C33: METHOD-020 v3 has a usable CPU WLOP confirmation oracle
- **Statement**: On the exact METHOD-020 v3 `40 x 24` density-warped plane,
  target 240, seed 1902, eight-iteration CPU reference, radii `0.22` through
  `0.28` fail with one empty projected-density neighborhood, while radii
  `0.30` through `0.40` return a finite 240-position `not_converged` last
  iterate with no empty neighborhoods. V3 freezes `h=0.32`, selected before
  Vulkan execution as one screened step above the observed feasibility
  boundary.
- **Status**: supported — sanitizer-backed CPU-reference screening only; no
  Vulkan parity or performance claim
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: Repeating the exact screened float fixture and
  parameters changes any listed CPU state, iteration count, empty-neighborhood
  count, or output cardinality, or the v3 `h=0.32` oracle lacks finite
  positions.
- **Proof**: [methods/geometry/locally_optimal_projection/reports/METHOD-020-v3-screening.md,
  methods/geometry/locally_optimal_projection/reports/METHOD-020-protocol-v3.md,
  benchmarks/geometry/manifests/lop_family_gpu_vulkan_smoke_v3.yaml]
- **Dependencies**: [C31, C32]
- **Tags**: geometry, LOP, WLOP, CPU reference, Vulkan protocol, screening
- **From staging**: O126

## C34: METHOD-020 proves bounded Vulkan parity for LOP and isotropic WLOP
- **Statement**: On the exact METHOD-020 v3 analytic fixtures and recorded
  NVIDIA RTX 3050 device, claim-grade v4/run-002 completed eight production
  `gpu_vulkan_compute` requests for ordinary LOP and isotropic WLOP with zero
  fallback. The worst CPU-reference-or-repeat positional RMS error was
  `1.6746e-5` (`<= 5e-4`) and the worst L-infinity error was `1.31344e-4`
  (`<= 2e-3`). Anisotropic WLOP, CLOP, and EAR remain explicit
  capability-negative pairs.
- **Status**: supported — exact fixtures/source/device only; no scanner-corpus,
  cross-device, device-time, memory-efficiency, or speedup claim
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: Replaying the exact frozen v4 protocol observes
  fallback, a non-Vulkan actual backend, fewer than eight completed requests,
  a status/cardinality/iteration mismatch, RMS error above `5e-4`, L-infinity
  error above `2e-3`, a same-host repeat delta above either bound, or a declared
  capability-negative pair passes canonical availability preview.
- **Proof**: [src/runtime/Modules/PointCloudConsolidation/Runtime.PointCloudConsolidationGpu.cpp,
  tests/integration/runtime/Test.PointCloudConsolidationGpuParity.cpp,
  benchmarks/geometry/manifests/lop_family_gpu_vulkan_smoke_v3.yaml,
  methods/geometry/locally_optimal_projection/reports/METHOD-020-result.md,
  tasks/evidence/METHOD-020/experiment/protocols/v4/protocol.yaml,
  tasks/evidence/METHOD-020/experiment/inputs/run-002/benchmark_result.json,
  tasks/evidence/METHOD-020/experiment/runs/run-002/bundle.yaml,
  tasks/evidence/METHOD-020/experiment/runs/run-002/audit.json]
- **Dependencies**: [C22, C23, C24, C33]
- **Tags**: geometry, LOP, WLOP, Vulkan, GPU, parity, bounded result
- **From staging**: O127

## C35: Auto WLOP visibly updates the imported child mesh through Vulkan
- **Statement**: On the repository `assets/models/child.obj` fixture with
  50,002 mesh vertices, an explicit Vulkan request using the otherwise-default
  isotropic-WLOP controls and Auto support radius selects rank 5 from the
  requested rank 16 under the unchanged 100,000,000-contribution limit,
  executes as `gpu_vulkan_compute` with zero CPU fallback, publishes non-zero
  canonical mesh-position displacement, and advances the renderer's resident
  position fingerprint and content revision after dirty upload.
- **Status**: supported — one repository asset and this Vulkan-capable host;
  no visual-quality, convergence, speedup, device-time, memory-efficiency, or
  cross-device claim
- **Provenance**: ai-executed
- **Crystallized via**: empirical-resolution
- **Falsification criteria**: Repeating the labeled integration test on an
  operational Vulkan host reports `UnsafeSupportRadius`, selects a rank other
  than 5 under the frozen defaults, falls back to CPU, publishes zero
  displacement, fails to update canonical mesh positions, or leaves the
  renderer position fingerprint/content revision unchanged.
- **Proof**: [src/geometry/Geometry.SupportRadius.cpp,
  src/runtime/Modules/PointCloudConsolidation/Runtime.PointCloudConsolidationGpu.cpp,
  tests/unit/geometry/Test.SupportRadius.cpp,
  tests/integration/runtime/Test.PointCloudConsolidationGpuParity.cpp]
- **Dependencies**: [C30, C34]
- **Tags**: geometry, runtime, LOP, WLOP, Auto radius, Vulkan, GPU, mesh,
  render residency, bounded result
- **From staging**: O128

## C36: Canonical property mutations drive rendering and Vulkan method publication
- **Statement**: On exact revision `548e62b5`, direct canonical position
  mutations without ECS dirty tags advance resident rendering data for mesh,
  graph, and point-cloud sources; CPU-backed visualization packets derive dirty
  stamps from the same property revisions; and the validation-enabled promoted
  Vulkan suite passes a repeated staged-overwrite readback plus ordinary LOP
  and isotropic-WLOP terminal publication-to-rendering workflows with zero
  validation errors.
- **Status**: supported — exact revision and tested CPU/Vulkan host surface;
  independent RUNTIME-214 high-risk acceptance remains pending, and no
  performance, visual-quality, or cross-device claim is made
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: On revision `548e62b5`, any labeled direct-mutation
  extraction test leaves the corresponding resident fingerprint unchanged,
  unchanged sources reupload every frame, CPU-backed visualization retains a
  stale dirty stamp, either LOP workflow reports `Applied` before canonical CPU
  publication, or the final three-test Vulkan command records a validation
  error or functional failure.
- **Proof**: [src/geometry/Geometry.Properties.cppm,
  src/runtime/Rendering/Runtime.RenderExtraction.Geometry.cpp,
  src/runtime/Visualization/Runtime.VisualizationRecipes.cpp,
  tests/contract/runtime/Test.MeshGeometryExtraction.cpp,
  tests/contract/runtime/Test.GraphGeometryExtraction.cpp,
  tests/contract/runtime/Test.PointCloudGeometryExtraction.cpp,
  tests/contract/runtime/Test.VisualizationRecipes.cpp,
  tests/integration/graphics/Test.GpuTransferFacadeGpuSmoke.cpp,
  tests/integration/runtime/Test.PointCloudConsolidationGpuParity.cpp,
  tasks/evidence/RUNTIME-214/commands/ci-final-cpu-tests.json,
  tasks/evidence/RUNTIME-214/commands/ci-asan-tests.json,
  tasks/evidence/RUNTIME-214/commands/ci-ubsan-tests.json,
  tasks/evidence/RUNTIME-214/commands/ci-vulkan-coherence-tests-final.json]
- **Dependencies**: [C30, C34, C35]
- **Tags**: geometry, runtime, rendering, property revisions, Vulkan, staging,
  LOP, coherence, bounded result
- **From staging**: O132
