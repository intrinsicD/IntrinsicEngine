# Architecture

## A01: Entity-Scoped Derived-Job Visibility
- **Decision**: Progressive geometry loading uses entity-scoped derived jobs with UI-visible state; the import queue alone is insufficient because UV generation, normal computation, texture baking, uploads, and material binding continue after base geometry is available.
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Evidence**: [tasks/archive/RUNTIME-110-progressive-entity-render-data-pipeline.md], [docs/adr/0021-progressive-entity-render-data-pipeline.md]
- **From staging**: O01

## A02: Domain Presentation Descriptors
- **Decision**: Render-data presentation is domain-aware: graph node and edge domains are independently addressable, point and graph presentation can use property buffers, and mesh face-domain data can render exactly while optionally baking to UV textures for surface materials.
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Evidence**: [tasks/archive/RUNTIME-110-progressive-entity-render-data-pipeline.md], [docs/adr/0021-progressive-entity-render-data-pipeline.md]
- **From staging**: O03

## A03: RUNTIME-110 Accepted Implementation Defaults
- **Decision**: RUNTIME-110 uses shared descriptors plus domain adapters; render-lane components stay primitive toggles with lane-to-presentation bindings elsewhere; first surface slots are albedo, normal, roughness, metallic, and scalar field with displacement descriptor-only; point/line slots cover color, scalar field, size or width, and point normal/orientation; generated textures default to deterministic child assets while generated property buffers default to session caches; GPU job domains are metadata first with CPU jobs implemented first; parent transforms compose hierarchically and material defaults inherit only into unset child slots; missing-UV atlas generation runs automatically after import; pending outputs render slot defaults with UI status; material/presentation bindings and generated-output policy serialize while transient job state does not.
- **Provenance**: user-revised
- **Crystallized via**: verbal-affirmation
- **Evidence**: [tasks/archive/RUNTIME-110-progressive-entity-render-data-pipeline.md], [docs/adr/0021-progressive-entity-render-data-pipeline.md]
- **From staging**: O04

## A04: Progressive Render-Data Implementation Split
- **Decision**: The accepted progressive entity render-data architecture is split into descriptor contracts (`RUNTIME-111`), derived-job graph snapshots (`RUNTIME-112`), progressive extraction (`RUNTIME-113`), import enrichment (`RUNTIME-114`), UI inspection (`UI-015`), and backend smoke (`GRAPHICS-090`) before any engine-code implementation begins.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Evidence**: [docs/adr/0021-progressive-entity-render-data-pipeline.md], [tasks/archive/RUNTIME-110-progressive-entity-render-data-pipeline.md]

## A05: Engine-Owned Asset Residency Service
<!-- SUPERSEDED by A15 through RUNTIME-183; retained as historical evidence. -->
- **Decision**: `Runtime.Engine` keeps asset lifecycle/frame ordering and public asset/GPU-cache compatibility facades, while GPU asset cache construction/listener ownership, fallback bootstrap delegation, model texture/model scene handoff ownership, maintenance ticks, and teardown ordering live in the Engine-private `AssetResidencyService` implementation glue.
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Evidence**: [src/runtime/Runtime.AssetResidencyService.Internal.hpp], [src/runtime/Runtime.Engine.cpp], [tasks/archive/RUNTIME-164-extract-asset-residency-service.md], [tasks/done/RUNTIME-171-privatize-asset-residency-service-surface.md]
- **From staging**: O07

## A06: App-Owned Sandbox Method Presentation Over Runtime Facades
- **Decision**: Sandbox method presentation and controller state live in
  app-owned registered windows, while runtime retains method models, command
  and undo/history execution, derived-job scheduling, stale-result rejection,
  and result publication. Each app window forwards immediate and pending
  results through the runtime facade and unregisters every callback when its
  owner is destroyed.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Evidence**: [src/app/Sandbox/Editor/Sandbox.MeshProcessingPanels.cpp,
  src/runtime/Runtime.GeometryProcessingOperations.cpp,
  src/runtime/Runtime.GeometryProcessingOperations.Mesh.cpp,
  tests/contract/runtime/Test.SandboxEditorMeshMethods.cpp,
  docs/architecture/runtime.md,
  tasks/done/ARCH-006-sandbox-editor-content-out-of-runtime.md]
- **From staging**: O24

## A07: App-Owned Sandbox Presentation Over Generic Runtime Editor Infrastructure
- **Decision**: Sandbox-specific windows, menus, ImGui state, and draw
  controllers live in app-owned `EditorShell`, method, mesh-processing, and
  domain-panel modules. Runtime owns only generic editor host/registry/widgets
  plus the opaque editor-workspace attachment lifecycle and presentation-free,
  feature-owned commands, queries, models, jobs, and result records. The app
  owns the Sandbox contexts and prepared-frame composition.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Evidence**: [src/app/Sandbox/Editor/Sandbox.EditorShell.cpp,
  src/app/Sandbox/Editor/Sandbox.DomainPanels.cpp,
  src/runtime/Runtime.EditorWorkspaceAttachment.cppm,
  src/runtime/Runtime.EditorWorkspaceSnapshots.cppm,
  docs/architecture/runtime.md,
  tasks/done/ARCH-006-sandbox-editor-content-out-of-runtime.md,
  tests/integration/runtime/Test.SandboxEditorPresentation.cpp]
- **From staging**: O25

## A08: Close False-Open Foundations Before New Backlog Code
- **Decision**: Backlog selection first retires already-implemented false-open
  foundations with current verification, then promotes the smallest
  right-sized foundation that opens the intended downstream chain. In this
  geometry loop, GEOM-019 and GEOM-014 closed before GEOM-063 unified the
  implemented CPU parameterizers.
- **Provenance**: user-revised
- **Crystallized via**: verbal-affirmation
- **Evidence**: [N209, N210, N212,
  tasks/done/GEOM-019-harmonic-tutte-parameterization-boundary-constraints.md,
  tasks/done/GEOM-014-feature-aware-quadric-error-simplification.md,
  tasks/done/GEOM-063-unified-cpu-parameterization-strategy-dispatch.md]
- **From staging**: O27

## A09: Parameterization Uses One Typed Config and Apply Lane
- **Decision**: Runtime parameterization control uses stable schema tokens and
  typed LSCM, harmonic, and BFF parameter records through the same validated
  preview/apply path for `Editor`, `AgentCli`, and `Programmatic` sources. The
  surface exposes only the implemented LSCM, harmonic-cotangent,
  Tutte-uniform, and BFF CPU strategies and has no speculative backend picker.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Evidence**: [src/core/Core.Config.Engine.cppm,
  src/core/Core.Config.EngineLoad.cpp,
  src/runtime/Runtime.ParameterizationOperations.cpp,
  tests/contract/runtime/Test.ParameterizationOperations.cpp,
  tasks/done/RUNTIME-176-parameterization-runtime-config-integration.md,
  N213, N215]
- **From staging**: O32

## A10: Slang Starts as One Offline Kernel Pilot
- **Decision**: Any Slang adoption starts with a pinned offline compiler that
  emits SPIR-V and reflection for one pure compute or material kernel, checked
  against an independent analytic C++ oracle and finite-difference gradients.
  It does not imply a shader-language migration, runtime compiler, or
  differentiable-renderer architecture.
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Evidence**: [N222,
  tasks/backlog/rendering/GRAPHICS-123-slang-single-kernel-gradient-pilot.md]
- **From staging**: O42

## A11: meshoptimizer v1.2 Is an External Oracle Before an Engine Dependency
- **Decision**: Evaluate meshoptimizer v1.2 through deterministic standalone
  geometry and cluster-hierarchy evidence before changing the pinned dependency
  or production paths. Topology-changing filters remain opt-in and must preserve
  primitive-to-source-face provenance.
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Evidence**: [N222,
  tasks/backlog/geometry/GEOM-066-meshoptimizer-v1-2-geometry-oracle.md,
  tasks/backlog/rendering/GRAPHICS-125-memory-priced-cluster-hierarchy-evidence.md]
- **From staging**: O43

## A12: Neural Render Proxy Work Begins With Deterministic Path Replay
- **Decision**: Neural Render Proxy research begins with a non-neural,
  deterministic path-record and GatherLight replay reference, correctness tests,
  and benchmark evidence. Training or renderer integration remains optional and
  blocked until operational path tracing, hardware RT, and differentiable
  consumers exist and the reference evidence justifies expansion.
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Evidence**: [N222,
  tasks/backlog/methods/METHOD-030-neural-render-proxy-path-replay-reference.md]
- **From staging**: O44

## A13: Issue 445 Research Is Deferred Behind a Shared Stability Audit
- **Decision**: Every Issue 445 candidate task depends first on REVIEW-003, a
  one-shot, commit-scoped, report-only audit of architecture convergence and
  right-sizing. Candidate tasks begin with bounded evidence and explicit
  stop/kill criteria; they cannot introduce shared frameworks or production
  integration before their local evidence gates pass.
- **Provenance**: user-revised
- **Crystallized via**: artifact-commitment
- **Evidence**: [N222,
  tasks/backlog/architecture/REVIEW-003-architecture-stability-right-sizing-readiness-audit.md,
  tasks/SESSION-BRIEF.md]
- **From staging**: O47

## A14: Historical-Engine Adaptations Use Consumer-Backed Native Slices
- **Decision**: Selective historical-engine adaptation is represented by
  IntrinsicEngine-native task slices: weighted Dijkstra before A*, rectangular
  LSQR with LSCM as adopter, a narrow fixed-variable smoothing solve, reusable
  sharp-feature facts before Catmull-Clark creases, device-lifetime Vulkan
  timestamp profiling, and guarded ECS hierarchy traversal. Legacy repositories
  provide behavior and integration evidence rather than subsystem ownership.
  Watcher lifecycle work stays with a future live-reload consumer, and
  Gaussian-derivative expansion stays deferred until a concrete consumer exists.
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Evidence**: [N240, N243, N244,
  tasks/backlog/geometry/GEOM-068-weighted-dijkstra-edge-cost-contract.md,
  tasks/backlog/geometry/GEOM-069-astar-graph-shortest-path.md,
  tasks/backlog/geometry/GEOM-070-sparse-lsqr-lscm-adoption.md,
  tasks/backlog/bugs/BUG-110-implicit-smoothing-boundary-dirichlet-solve.md,
  tasks/backlog/geometry/GEOM-071-reusable-sharp-feature-classification.md,
  tasks/backlog/geometry/GEOM-072-catmull-clark-crease-masks.md,
  tasks/backlog/rendering/GRAPHICS-127-native-gpu-timestamp-profiler.md,
  tasks/backlog/ecs/HARDEN-086-guarded-hierarchy-query-helpers.md,
  tasks/backlog/rendering/README.md,
  tasks/backlog/geometry/README.md]
- **From staging**: O51

## A15: App-Composed Asset Workflow Services
- **Decision**: An app-composed `AssetWorkflowModule` owns asset service,
  GPU-cache, staged import, private model materialization/texture residency, and
  object-space normal-bake composition. It is itself the sole published import
  service and exports one recipe with typed copied results for route, decode,
  CPU materialization, ECS authoring, postprocess, GPU residency, and completion.
  It additionally publishes exact `AssetService`, `GpuAssetCache`, and
  `IAssetFrameHooks` services; `Runtime.Engine` consumes cache, hooks, and
  dropped-file import capability through optional registry lookups and exposes
  none of the retired asset/cache/import or bake-diagnostic getters. The former
  public import pipeline, role callback registries, IO bridges, and handoff
  modules are absent.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Evidence**: [N268, N269, commit 74a15419,
  src/runtime/Runtime.AssetWorkflowModule.cppm,
  src/runtime/Runtime.AssetWorkflowModule.cpp,
  tests/contract/runtime/Test.AssetWorkflowModule.cpp,
  tests/contract/runtime/Test.RuntimeEnginePrivateGlue.cpp,
  tests/contract/runtime/Test.RuntimeEngineLayering.cpp]
- **From staging**: O55
- **Supersedes**: A05

## A16: Keep Stable Module Seams Thin and Implementation-Private
- **Decision**: Stable architectural seams remain C++23 modules, but their
  interfaces contain only exported types, declarations, small accessors, and
  the exact imports required by that public surface. Runtime.Engine applies
  this posture with twelve owning imports, five typed getters, and no domain
  imports or re-exports; all concrete state, control flow, and implementation-
  only dependencies live behind `Engine::Impl` and private implementation
  headers.
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Evidence**: [src/runtime/Runtime.Engine.cppm,
  src/runtime/Runtime.Engine.cpp,
  src/runtime/Runtime.RenderExtractionService.Internal.hpp,
  tools/repo/kernel_convergence_policy.json,
  tasks/done/RUNTIME-187-finalize-domain-free-engine-surface.md,
  commit 982c72ae, N277]
- **From staging**: O08

## A17: Agent Workflow Is One Repository-Native Cumulative Profile Model
- **Decision**: IntrinsicEngine extends its existing task, review, benchmark,
  method, ARA, documentation, skill, and CI authorities with cumulative
  `micro`, `standard`, `high-risk`, `claim-grade`, and `protected` workflow
  profiles. Plain versioned YAML, JSON, and JSONL artifacts plus small Python
  CLIs provide evidence, review, handoff, experiment custody, and ownership;
  no donor framework, parallel state engine, daemon, or external service owns
  the lifecycle.
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Evidence**: [N290, N291, N292,
  tasks/done/PROC-028-enforced-agent-evidence-review-experiment-workflow.md,
  docs/agent/workflow-evidence.md, tools/agents/workflow_evidence.py,
  tools/agents/experiment_custody.py, tools/agents/task_claim.py,
  .github/workflows/ci-docs.yml]
- **From staging**: O75

## A18: Architecture Views Reuse the Repository Evidence Graph
- **Decision**: IntrinsicEngine owns one diagramming skill over the existing
  deterministic knowledge graph rather than a parallel architecture model.
  It generates aggregated layer and bounded module-import views, requires
  concrete source inspection for composition, sequence, and data-flow views,
  and routes complete-graph exploration to Graphify. Mermaid source is the
  durable editable output; an installed renderer may add a convenience image
  without becoming authoritative.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Evidence**: [N299, N300, N301,
  tasks/done/PROC-029-intrinsicengine-architecture-diagram-skill.md,
  tools/agents/skills/intrinsicengine-draw-architecture/SKILL.md,
  tools/agents/skills/intrinsicengine-draw-architecture/scripts/render_architecture.py,
  tools/agents/skills/intrinsicengine-draw-architecture/references/view-guide.md,
  tests/regression/tooling/Test.ArchitectureDiagramSkill.py]
- **From staging**: O79

## A19: One Published Asset Workflow Executes One Copied Import Recipe
- **Decision**: Production asset import enters through the sole published
  `AssetWorkflowModule` service and carries one copied seven-stage recipe from
  route selection through completion. Decode/materialization, ECS authoring,
  postprocess, texture bake/residency, and completion policy remain private
  runtime implementation details; assets and geometry retain ordinary CPU
  payload and codec ownership.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Evidence**: [src/runtime/Runtime.AssetWorkflowModule.cppm,
  src/runtime/Runtime.AssetWorkflowModule.cpp,
  src/runtime/Runtime.AssetWorkflowImportExecutor.cpp,
  src/runtime/Runtime.AssetWorkflowRecipePolicies.cpp,
  tests/contract/runtime/Test.AssetWorkflowModule.cpp,
  tasks/done/RUNTIME-200-staged-asset-import-materialization-recipe.md,
  tasks/evidence/RUNTIME-200/report.yaml, N268, N269, N288]
- **From staging**: O81

## A20: Selected-Editor Async Work Is Feature-Local and Evidence-Triggered
- **Decision**: Keep the delivered visibility-gated immutable selected-model
  caches, property snapshots, diagnostics, and bounded shared JobService
  publication, but retire the all-feature selected-analysis umbrella. A new
  async derivation belongs to the concrete feature whose measurement proves a
  material full-buffer cost and carries only that feature's generation and
  staleness contract.
- **Provenance**: user-revised
- **Crystallized via**: verbal-affirmation
- **Evidence**: [N312, N313, N317, N318,
  tasks/done/RUNTIME-138-withdraw-broad-selected-analysis-umbrella.md,
  docs/reports/2026-07-05-ui-030-frame-pacing-diagnostics.md,
  src/runtime/README.md,
  tasks/done/RUNTIME-202-retire-sandbox-runtime-facade.md]
- **From staging**: O82

## A21: Consolidation Config Semantics Belong to Runtime
- **Decision**: Point-cloud consolidation uses a runtime-owned config value,
  schema, codec, validator, and explicit typed operation. Sandbox owns only
  section registration/default aggregation and later UI presentation. Config
  preview/apply commits validated parameters without mutating geometry; a
  separate JobService-backed, current-generation, undoable request executes
  consolidation. UI-035 owns the dependent visible proof rather than forming a
  circular RUNTIME-175 acceptance gate.
- **Provenance**: user-revised
- **Crystallized via**: verbal-affirmation
- **Evidence**: [N312, N313, N317, N318,
  tasks/backlog/runtime/RUNTIME-175-pointcloud-consolidation-runtime-config-integration.md,
  tasks/backlog/ui/UI-035-sandbox-pointcloud-consolidation-editor-panel.md,
  src/runtime/Runtime.ParameterizationConfig.cppm,
  src/app/Sandbox/Sandbox.ConfigSections.cpp]
- **From staging**: O84

## A22: Runtime Helper Privatization Is Split by Concrete Owner
- **Decision**: Engine composition privately owns ModuleSchedule,
  EcsSystemBundle, and JobServiceGpuQueueBridge behavior; SceneInteraction
  privately owns GizmoFrameService and SelectionReadback behavior. The two
  deletion slices remain separate. RenderRecipeActivation and DeviceBootstrap
  stay outside the premise because each has multiple load-bearing production
  consumers; their public status changes only under a new concrete task.
- **Provenance**: user-revised
- **Crystallized via**: verbal-affirmation
- **Evidence**: [N312, N313, N317, N318, N327, N328,
  tasks/done/RUNTIME-203-internalize-engine-composition-helpers.md,
  tasks/done/RUNTIME-205-internalize-scene-interaction-helpers.md,
  tasks/evidence/RUNTIME-203/report.yaml,
  tasks/evidence/RUNTIME-205/report.yaml,
  tasks/backlog/architecture/REVIEW-003-architecture-stability-right-sizing-readiness-audit.md,
  src/runtime/Runtime.Engine.cpp,
  src/runtime/Scene/Runtime.SceneInteractionModule.cpp,
  src/runtime/Runtime.EngineConfigControl.cpp,
  src/runtime/Runtime.AssetWorkflowModule.cpp]
- **From staging**: O86

## A23: Method Integrations Share Typed Operations, Not Mandatory Modules
- **Decision**: GEOM, METHOD, and RUNTIME task prefixes identify owning
  capability or evidence scope rather than mandatory consecutive stages.
  Selection and ECS materialization belong to focused typed runtime operations;
  reusable geometry kernels split out only when independently justified, and a
  METHOD task may land executable lower-layer code or a private backend adapter.
  IRuntimeModule remains optional and is introduced only for demonstrated
  lifecycle, durable-state, dependency/commit, or consumer-reaction ownership,
  never as one wrapper per algorithm.
- **Provenance**: user-revised
- **Crystallized via**: verbal-affirmation
- **Evidence**: [N319, N322, N323, N324, N325,
  docs/adr/0026-runtime-module-scope-by-consumer-contract.md,
  docs/architecture/feature-module-playbook.md,
  tasks/backlog/runtime/RUNTIME-175-pointcloud-consolidation-runtime-config-integration.md]
- **From staging**: O88

## A24: Published Methods Require a Validated Literature-Intake Predecessor
- **Decision**: Published and mixed-basis method implementation is gated by a
  versioned, machine-validated literature-intake artifact rather than an
  unverifiable skill-invocation assertion. The artifact records stable primary
  and related reference identities, search sources/queries/cutoff, claim and
  equation locators, selection/exclusion rationale, and implementation deltas.
  A sealed intake phase or separate predecessor completes before production
  implementation; in-house and not-applicable work declares that basis.
- **Provenance**: user-revised
- **Crystallized via**: verbal-affirmation
- **Evidence**: [N322, N323, N324,
  docs/agent/method-workflow.md,
  tools/agents/validate_method_manifests.py,
  methods]
- **From staging**: O91

## A25: One Method-Package Graph Owns References, Variants, and Implementations
- **Decision**: Stable scientific reference IDs feed named variant axes and
  supported combinations, which map to real module/source paths, entry points,
  backend tokens, and owning tasks. METHOD and GEOM tasks reference those
  canonical IDs rather than duplicating citation prose. Known variants use
  value-based enums or std::variant payloads; backend is an orthogonal optional
  axis, and shared parity helpers appear only after two positive concrete
  consumers justify them.
- **Provenance**: user-revised
- **Crystallized via**: verbal-affirmation
- **Evidence**: [N322, N323, N324,
  docs/architecture/algorithm-variant-dispatch.md,
  docs/adr/0026-runtime-module-scope-by-consumer-contract.md,
  tasks/backlog/methods/HARDEN-084-localized-cpu-gpu-parity-signatures.md]
- **From staging**: O92

## A26: Reusable Contracts Have Canonical Sources and Explicit Task Enrollment
- **Decision**: Each reusable engine contract has one canonical architecture
  source and one stable entry in the contract catalog. `AGENTS.md` owns the
  global obligation to discover, preserve, and dispose applicable contracts;
  task front-matter records the applicable IDs or a justified empty review.
  Templates and authoring/review skills route agents to that authority, while
  strict validation and executable proof paths enforce it prospectively
  without copying subsystem semantics into every workflow surface.
- **Provenance**: user-revised
- **Crystallized via**: verbal-affirmation
- **Evidence**: [N329, N330, AGENTS.md,
  docs/architecture/contract-catalog.yaml,
  docs/agent/task-format.md,
  tools/agents/validate_tasks.py,
  tests/regression/tooling/Test.ValidateTasks.py,
  tasks/done/PROC-030-contract-applicability-and-method-integration.md]
- **From staging**: O94
- **Parallel staging convergence**: O109 (artifact-commitment after the
  pre-policy feature history merged with the implemented PROC-030 policy).

## A27: Verification Plans Derive from One Evidence Graph
- **Decision**: Local edit, pull-request, merge-group, scheduled-deep, and
  agent verification plans derive from one schema-versioned evidence graph
  linking source/build inputs, target and module closure, stable contracts and
  proofs, logical test cases, variants, capabilities/resources, and
  revision-bound results. One verifier produces plans and receipts; CTest
  labels, workflow YAML, task prose, and human-readable commands remain
  projections rather than independent routing authorities.
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Evidence**: [N333, N334, N335,
  docs/architecture/verification-evidence-architecture.md,
  tasks/backlog/process/CI-012-versioned-verification-evidence-graph.md,
  tasks/backlog/process/CI-013-unified-verifier-profiles-and-receipts.md,
  tasks/backlog/process/CI-014-static-build-contract-impact-graph.md,
  tasks/backlog/process/CI-015-digest-test-inventory-and-sharding.md,
  tasks/backlog/process/CI-019-thin-ci-merge-queue-topology.md,
  tasks/backlog/process/PROC-031-agent-verification-receipts.md]
- **From staging**: O97

## A28: Select the Build Backend Through a Named-Module Bake-Off
- **Decision**: The production build/action-cache backend is not selected from
  remote-cache capability alone. A frozen matched bake-off must prove
  exported-interface and layout invalidation, toolchain/flag/sysroot isolation,
  cross-root and cross-machine reuse, clean-versus-cached output and logical
  test parity, failure behavior, developer tooling, operating cost, and
  rollback. The simplest passing path is retained and rejected experiment
  adapters are deleted; CMake/Ninja remains valid if no candidate clears every
  killing gate.
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Evidence**: [N333, N334, N335,
  docs/architecture/verification-evidence-architecture.md,
  tasks/backlog/process/BUILD-005-hermetic-toolchain-action-identity.md,
  tasks/backlog/process/BUILD-006-cxx23-module-build-backend-bakeoff.md,
  tasks/backlog/process/CI-016-content-addressed-build-test-result-cache.md]
- **From staging**: O100

## A29: Geometry Methods Bind to Their Least Structured Data Domain
- **Decision**: Method eligibility is expressed by the least structured
  element domains and typed values the algorithm consumes, not by an entity
  container, provenance label, or handle-indexed property wrapper. Meshes may
  satisfy mesh, graph, and point-set contracts; graphs may satisfy graph and
  point-set contracts. A point-set method accepts compatible `Property<T>`,
  `ConstProperty<T>`, or spans from any originating vertex, edge, halfedge, or
  face domain, while a graph method adds only its named adjacency/property
  requirements. Same-cardinality output returns to the originating domain;
  topology/cardinality changes require an explicitly owned operation and never
  an implicit converter.
- **Provenance**: user
- **Crystallized via**: verbal-affirmation
- **Evidence**: [N355,
  AGENTS.md,
  docs/architecture/geometry-api-style.md,
  docs/architecture/method-api-contract.md,
  docs/architecture/contract-catalog.yaml]
- **From staging**: O114

## A30: Automatic Radii Derive from Property-Specific Neighborhood Profiles
- **Decision**: A geometry-owned, entity-independent profile computes robust
  quantiles of distinct k-nearest-neighbor distances over a bounded,
  deterministic sample of the selected finite `vec3` property. Each method
  maps that shared profile through its own target-neighbor and coverage policy;
  bounding-box scale remains diagnostic/fallback data rather than the primary
  radius. Recommendation uses k-nearest queries, while execution enumerates
  support with the existing KD-tree radius query and applies the method
  kernel's exact cutoff.
- **Provenance**: user-revised
- **Crystallized via**: verbal-affirmation
- **Evidence**: [N367, N368, N369,
  src/geometry/Geometry.KDTree.cppm,
  src/geometry/Geometry.KDTree.cpp,
  src/geometry/Geometry.PointCloud.Utils.cpp,
  src/geometry/Geometry.PointCloud.Consolidation.cpp]
- **From staging**: O119

## A31: Automatic Radius Intent Is Config; Profiles Are Derived Runtime State
- **Decision**: Radius-using operations persist validated `Auto` versus
  `Manual` intent in serializable method config and pass an explicit resolved
  world-unit radius to lower-layer kernels. A computed recommendation is not
  one authored ECS scalar because it is specific to the full property domain
  and method policy. If reuse warrants caching, runtime owns a derived profile
  keyed by stable entity identity, full `GeometryPropertyRef`, source-data
  revision, and estimator version; until mutation has a reliable revision
  contract, execution recomputes the bounded profile rather than trusting
  cached state.
- **Provenance**: user-revised
- **Crystallized via**: verbal-affirmation
- **Evidence**: [N367, N368,
  AGENTS.md,
  src/geometry/Geometry.Properties.cppm,
  src/runtime/Runtime.GeometryAvailability.cppm,
  src/runtime/Runtime.PointCloudConsolidationConfig.cppm]
- **From staging**: O120

## A32: LOP Vulkan Acceleration Uses a Private Fixed-Radius Cell Grid
- **Decision**: The first production LOP-family GPU backend is Vulkan compute,
  not CUDA. It sizes a bounded dense cell grid from the resolved support radius,
  builds the immutable source grid once, rebuilds the projected grid per
  iteration through count, shared prefix scan, and scatter, and streams exact
  27-cell sphere-filtered accumulation without a global neighbor-pair list.
  Iteration and convergence state remain on-device, persistent buffers are
  reused, and one final shared multi-range readback enters the existing runtime
  publication path. The participant remains private to runtime/JobService;
  CUDA, LBVH, and a public generic neighbor-index framework require separate
  evidence before adoption.
- **Provenance**: user-revised
- **Crystallized via**: verbal-affirmation
- **Evidence**: [N370, N371,
  src/runtime/Runtime.PointCloudConsolidationGpu.cpp,
  src/runtime/internal/Runtime.PointCloudConsolidationGpu.Internal.hpp,
  assets/shaders/lop_grid_count.comp,
  assets/shaders/lop_grid_scatter.comp,
  assets/shaders/lop_project.comp,
  tests/integration/runtime/Test.PointCloudConsolidationGpuParity.cpp,
  tasks/done/METHOD-020-lop-family-gpu-vulkan-compute-backend.md]
- **From staging**: O121
