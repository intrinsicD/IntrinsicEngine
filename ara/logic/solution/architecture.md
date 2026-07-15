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
- **Decision**: `Runtime.Engine` keeps asset lifecycle/frame ordering and public asset/GPU-cache compatibility facades, while GPU asset cache construction/listener ownership, fallback bootstrap delegation, model texture/model scene handoff ownership, maintenance ticks, and teardown ordering live behind `Extrinsic.Runtime.AssetResidencyService`.
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Evidence**: [src/runtime/Runtime.AssetResidencyService.cppm], [src/runtime/Runtime.AssetResidencyService.cpp], [tasks/archive/RUNTIME-164-extract-asset-residency-service.md]
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
  src/runtime/Runtime.SandboxEditorFacades.cpp,
  tests/contract/runtime/Test.SandboxEditorMeshMethods.cpp,
  docs/architecture/runtime.md,
  tasks/done/ARCH-006-sandbox-editor-content-out-of-runtime.md]
- **From staging**: O24

## A07: App-Owned Sandbox Presentation Over Generic Runtime Editor Infrastructure
- **Decision**: Sandbox-specific windows, menus, ImGui state, and draw
  controllers live in app-owned `EditorShell`, method, mesh-processing, and
  domain-panel modules. Runtime owns only generic editor host/registry/widgets
  and presentation-free Sandbox engine contexts, commands, models, jobs,
  result records, and attachment/session wiring.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Evidence**: [src/app/Sandbox/Editor/Sandbox.EditorShell.cpp,
  src/app/Sandbox/Editor/Sandbox.DomainPanels.cpp,
  src/runtime/Runtime.SandboxEditorFacades.cppm,
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
  src/runtime/Runtime.SandboxParameterizationFacade.cpp,
  tests/contract/runtime/Test.ParameterizationFacade.cpp,
  tasks/done/RUNTIME-176-parameterization-runtime-config-integration.md,
  N213, N215]
- **From staging**: O32
