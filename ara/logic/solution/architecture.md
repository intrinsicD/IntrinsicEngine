# Architecture

## A01: Entity-Scoped Derived-Job Visibility
- **Decision**: Progressive geometry loading uses entity-scoped derived jobs with UI-visible state; the import queue alone is insufficient because UV generation, normal computation, texture baking, uploads, and material binding continue after base geometry is available.
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Evidence**: [tasks/done/RUNTIME-110-progressive-entity-render-data-pipeline.md], [docs/adr/0021-progressive-entity-render-data-pipeline.md]
- **From staging**: O01

## A02: Domain Presentation Descriptors
- **Decision**: Render-data presentation is domain-aware: graph node and edge domains are independently addressable, point and graph presentation can use property buffers, and mesh face-domain data can render exactly while optionally baking to UV textures for surface materials.
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Evidence**: [tasks/done/RUNTIME-110-progressive-entity-render-data-pipeline.md], [docs/adr/0021-progressive-entity-render-data-pipeline.md]
- **From staging**: O03

## A03: RUNTIME-110 Accepted Implementation Defaults
- **Decision**: RUNTIME-110 uses shared descriptors plus domain adapters; render-lane components stay primitive toggles with lane-to-presentation bindings elsewhere; first surface slots are albedo, normal, roughness, metallic, and scalar field with displacement descriptor-only; point/line slots cover color, scalar field, size or width, and point normal/orientation; generated textures default to deterministic child assets while generated property buffers default to session caches; GPU job domains are metadata first with CPU jobs implemented first; parent transforms compose hierarchically and material defaults inherit only into unset child slots; missing-UV atlas generation runs automatically after import; pending outputs render slot defaults with UI status; material/presentation bindings and generated-output policy serialize while transient job state does not.
- **Provenance**: user-revised
- **Crystallized via**: verbal-affirmation
- **Evidence**: [tasks/done/RUNTIME-110-progressive-entity-render-data-pipeline.md], [docs/adr/0021-progressive-entity-render-data-pipeline.md]
- **From staging**: O04

## A04: Progressive Render-Data Implementation Split
- **Decision**: The accepted progressive entity render-data architecture is split into descriptor contracts (`RUNTIME-111`), derived-job graph snapshots (`RUNTIME-112`), progressive extraction (`RUNTIME-113`), import enrichment (`RUNTIME-114`), UI inspection (`UI-015`), and backend smoke (`GRAPHICS-090`) before any engine-code implementation begins.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Evidence**: [docs/adr/0021-progressive-entity-render-data-pipeline.md], [tasks/done/RUNTIME-110-progressive-entity-render-data-pipeline.md]

## A05: Engine-Owned Asset Residency Service
- **Decision**: `Runtime.Engine` keeps asset lifecycle/frame ordering and public asset/GPU-cache compatibility facades, while GPU asset cache construction/listener ownership, fallback bootstrap delegation, model texture/model scene handoff ownership, maintenance ticks, and teardown ordering live behind `Extrinsic.Runtime.AssetResidencyService`.
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Evidence**: [src/runtime/Runtime.AssetResidencyService.cppm], [src/runtime/Runtime.AssetResidencyService.cpp], [tasks/done/RUNTIME-164-extract-asset-residency-service.md]
- **From staging**: O07
