# ADR 0021: Progressive entity render-data pipeline

- **Status:** Accepted
- **Date:** 2026-06-16
- **Owners:** Runtime, renderer, and editor maintainers
- **Related tasks:** RUNTIME-110, RUNTIME-111, RUNTIME-112, RUNTIME-113, RUNTIME-114, UI-015, GRAPHICS-090

## Context

Imported geometry must become visible before every derived representation is
ready. Meshes, graphs, and point clouds are all first-class geometry domains:
none of the runtime, renderer, or UI contracts should require mesh-only
post-processing before the entity can render with defaults.

Current promoted geometry authority lives in `GeometrySources`, and render
intent is expressed by the `RenderSurface`, `RenderEdges`, and `RenderPoints`
component lanes. Runtime owns ECS-to-render extraction, async job orchestration,
and live sidecars. Graphics consumes snapshots, asset IDs, and GPU-owned
resources, and must not import live ECS or asset-service ownership.

The pipeline needs a data-based contract for material/presentation bindings,
compatible geometry properties, generated outputs, per-entity job visibility,
and eventual UI controls. The decision is hard to reverse because it shapes
entity composition, material binding, serialization, and async derived-work
ownership across runtime, renderer, UI, assets, and geometry.

## Decision

- A geometry leaf entity owns exactly one authoritative geometry domain: mesh,
  graph, or point cloud. A model/import that contains multiple primitives is a
  composition entity with child geometry leaves.
- Composition entities own grouping, transforms, provenance, aggregate status,
  and selection affordances. Leaf entities own render lanes, presentation
  bindings, properties, derived jobs, and diagnostics.
- `RenderSurface`, `RenderEdges`, and `RenderPoints` remain primitive render
  intent toggles. They do not own full material records. A separate per-entity
  binding map resolves each lane to a material or presentation key.
- Shared descriptor types define property bindings, slot bindings, readiness
  state, generated-output policy, and derived-job identity. Domain-specific
  adapters pack, bake, or upload mesh, graph, and point-cloud data.
- Stable persisted bindings identify geometry domain, property name, expected
  value type, element count/generation, slot semantic, and generated-output
  policy. They never persist raw property pointers or GPU handles.
- First surface slots are albedo, normal, roughness, metallic, and scalar
  field. Displacement is descriptor-only until a later task handles bounds,
  culling, shaders, and backend implications.
- First point slots are color, scalar field, size, and normal/orientation.
  First line/edge slots are color, scalar field, and width.
- Generated textures default to deterministic child assets. Generated property
  buffers default to session caches. Future policy values are
  `SessionCache`, `DeterministicChildAsset`, and `PersistOnSave`.
- GPU-capable job-domain metadata is defined as `Cpu`, `GpuCompute`,
  `GpuGraphics`, and `Auto`, but first implementation slices land CPU jobs and
  deterministic unavailable diagnostics for GPU-only domains.
- Missing mesh UVs schedule an asynchronous atlas job after import. Raw
  geometry remains visible with slot defaults while UV-dependent texture slots
  wait for generated outputs.
- Jobs are keyed by stable entity id, geometry domain, source generation, and
  output semantic. They may depend on other jobs and may schedule follow-up
  jobs after completion, but follow-up work must record explicit dependency
  edges for UI/debug visibility.
- Completed jobs apply on the main thread. Stale results are discarded when the
  entity, geometry, source property, or binding generation changed. Previous
  valid outputs remain bound until replacement output is ready.
- Material/presentation bindings and generated-output policy are serialized.
  Transient job state is never serialized.

## Consequences

- Mesh, graph, and point-cloud presentation can share UI and job/debug
  vocabulary while keeping domain-specific packing, baking, and upload logic
  explicit.
- Import can publish raw geometry immediately, then progressively enrich the
  entity as normals, UVs, bakes, property buffers, and texture uploads complete.
- Render extraction gets a stable binding model instead of inspecting ad hoc
  per-component material state.
- The design adds descriptor indirection and a runtime job sidecar, so follow-up
  work must land in small CPU-contracted slices before backend smoke coverage.
- Generated output persistence becomes an explicit per-slot policy, avoiding
  accidental serialization of transient worker state or raw property views.

## Alternatives Considered

- **Mesh-first post-processing pipeline.** Rejected because graph and
  point-cloud properties must have equal scheduling, UI visibility, and
  presentation semantics.
- **Store full materials in render-lane components.** Rejected because it couples
  primitive intent toggles to presentation records and makes lane reuse across
  domains harder to evolve.
- **One monolithic generic renderer abstraction.** Rejected because mesh texture
  baking, graph edge buffers, and point-cloud surfel data need different
  packing and backend constraints even when descriptors are shared.
- **Lazy UV atlas generation only when a slot is enabled.** Rejected for the
  first implementation because automatic async UV generation after import gives
  deterministic readiness for common mesh material workflows while still
  preserving immediate default rendering.
- **Use checker/fallback textures for in-progress generated slots.** Rejected
  because pending work is a normal state. Slot defaults plus UI status avoid
  visually conflating progress with missing/error fallback diagnostics.

## Validation

Validation is split across follow-up tasks:

- `RUNTIME-111` owns the first CPU-contracted descriptor and serialization
  contract for all three geometry domains.
- `RUNTIME-112` owns entity-scoped derived-job snapshots, dependencies,
  stale-result discard, and follow-up scheduling.
- `RUNTIME-113` owns render extraction over lane presentation bindings and
  domain property-buffer/default presentation.
- `RUNTIME-114` owns import-time progressive enrichment from raw visibility to
  generated UVs, normals, bakes, uploads, and bindings.
- `UI-015` owns selected-entity and global UI/debug visibility for bindings,
  slot defaults, property pickers, and derived jobs.
- `GRAPHICS-090` owns later opt-in backend evidence for generated surface slots
  and graph/point property-buffer presentation after CPU contracts land.
