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
  src/runtime/Runtime.AsyncWorkModule.cpp,
  src/runtime/Runtime.Engine.cpp,
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
  src/runtime/Runtime.ProgressivePoissonGpuBackend.cppm,
  src/runtime/Runtime.ProgressivePoissonGpuBackend.cpp,
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
  src/runtime/Runtime.ProgressivePoissonGpuBackend.cpp,
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
  src/runtime/internal/Runtime.FeatureConfigCodecs.Detail.cpp,
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
  src/runtime/Runtime.GeometryPresentation.cppm,
  src/runtime/Runtime.GeometryPresentation.cpp,
  src/runtime/Runtime.SceneSerialization.cpp,
  src/runtime/Runtime.RenderExtraction.cpp,
  src/runtime/Runtime.AssetWorkflowModule.cpp,
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
  src/runtime/Runtime.GeometryPlanBuilders.cppm,
  src/runtime/Runtime.RenderExtraction.Geometry.cpp,
  src/runtime/Runtime.MeshSurfaceTopology.cppm,
  src/runtime/Runtime.MeshPrimitiveView.cppm,
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
  src/runtime/Runtime.RenderExtraction.Recipes.cpp,
  src/runtime/Runtime.RenderExtraction.cpp,
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
  src/runtime/Runtime.TextureBakeModule.cppm,
  src/runtime/Runtime.TextureBakeModule.cpp,
  src/runtime/Runtime.AssetWorkflowModule.cpp,
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
  src/runtime/Runtime.EditorMutation.Internal.hpp,
  src/runtime/Runtime.EditorCommandHistory.cppm,
  src/runtime/Runtime.EditorCommandHistory.cpp,
  src/runtime/Runtime.SceneEditingOperations.Actions.cpp,
  src/runtime/Runtime.GeometryProcessingOperations.Mesh.cpp,
  src/runtime/Runtime.VisualizationEditingOperations.Actions.cpp,
  src/runtime/Runtime.GeometryProcessingOperations.cpp,
  src/runtime/Runtime.ParameterizationOperations.cpp,
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

## C17: LOP and WLOP have a deterministic CPU reference contract
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
- **From staging**: O88

## C18: Continuous LOP has a deterministic original-equation CPU reference
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
- **Dependencies**: [C17]
- **Tags**: geometry, point cloud, CLOP, Gaussian mixture, CPU reference,
  deterministic
- **From staging**: O89
