---
id: RUNTIME-110
theme: F
depends_on: []
maturity_target: Scaffolded
---
# RUNTIME-110 — Progressive entity render-data pipeline clarification

## Completion
- Completed: 2026-06-16. Commit/PR: this retirement commit.
- Maturity: `Scaffolded`.
- Fix summary: accepted the progressive entity render-data architecture for
  mesh, graph, and point-cloud leaves; recorded ADR-0021; and split
  implementation into `RUNTIME-111`, `RUNTIME-112`, `RUNTIME-113`,
  `RUNTIME-114`, `UI-015`, and `GRAPHICS-090`.
- Remaining work: `RUNTIME-111` owns the first `CPUContracted` descriptor
  gate. Runtime scheduling/extraction/import implementation is split across
  `RUNTIME-112` through `RUNTIME-114`, editor visibility is owned by `UI-015`,
  and opt-in backend proof is owned by `GRAPHICS-090`.

## Goal
- Define the target data model for progressive entity rendering, derived jobs,
  material/property bindings, and UI visibility before implementation starts.

## Non-goals
- No engine code changes in this clarification task.
- No rename of existing render-lane components such as `RenderEdges`.
- No GPU-compute implementation requirement until the runtime job contract is
  agreed.
- No persistence/schema migration until the in-memory data model is accepted.

## Context
- Owner: `runtime` composition and editor/UI policy, with lower-layer support
  from `ecs`, `geometry`, `assets`, and `graphics`.
- Layer rule: ECS may store CPU/data-only components; graphics must not import
  live ECS; runtime owns ECS-to-render extraction and any live sidecars.
- Current promoted geometry authority is `GeometrySources`.
- Current promoted render-lane components are `RenderSurface`, `RenderEdges`,
  and `RenderPoints`. `RenderEdges` remains the engine component name; the UI
  may label this lane as "Edges" or "Lines" where useful.
- Mesh, graph, and point-cloud content are equally important geometry domains.
  Implementation may be sliced, but the architecture must not be mesh-first.
- ADR-0021 records the accepted architecture before engine code changes.

## Resolved Decisions

### Entity Shape

- A geometry leaf entity owns one authoritative geometry domain:
  - mesh
  - graph
  - point cloud
- A model or imported object that contains multiple primitives is represented as
  a composition entity with child geometry leaf entities.
- The composition entity owns grouping, transforms, asset provenance, aggregate
  child status, and selection affordances.
- The composition entity does not directly store mixed mesh/graph/point-cloud
  `GeometrySources`.
- Leaf entities independently own render lanes, material/presentation bindings,
  properties, derived jobs, and diagnostics.
- Selecting a composition entity should show aggregate child material/job status.
- Composition entities should not expose one editable inherited child-material
  table by default; detailed material editing happens on selected leaf entities.

### Render Intent Components

- Render intent is expressed by component presence.
- A mesh leaf may carry:
  - `RenderSurface` for filled triangle rendering.
  - `RenderEdges` for wire/edge rendering.
  - `RenderPoints` for vertex rendering.
- A graph leaf may carry:
  - `RenderEdges` for edge rendering.
  - `RenderPoints` for node rendering.
- A point-cloud leaf may carry:
  - `RenderPoints` for point/surfel rendering.
- `RenderSurface`, `RenderEdges`, and `RenderPoints` should not directly own
  full material records. They should carry or resolve a small key into a
  separate per-entity presentation/material binding component.
- Point, line/edge, and surface lanes should use distinct material/presentation
  abstractions so each lane can evolve independently.

### Geometry Properties And Domains

- Geometry data remains property-set based.
- Property bindings identify data by stable descriptors:
  - geometry domain
  - property name
  - expected value type
  - element count / generation
- Stable authoring state must not persist raw property pointers. Runtime may
  resolve a descriptor to a borrowed property view for a frame or job.
- User-facing domains:
  - mesh vertex domain
  - mesh edge domain
  - mesh halfedge domain
  - mesh face domain
  - mesh surface/fragment domain derived from UVs or another parameterization
  - graph vertex/node domain
  - graph edge domain
  - point-cloud vertex/point domain
- Mesh halfedge properties are user-facing. Advanced users should have full
  control, even when most presentation workflows ignore halfedges.
- Selection/highlight state is transient overlay state by default, not ordinary
  authored property data.
- The UI should provide an explicit command to transfer current selection state
  into ordinary properties when the user wants to reuse it in algorithms,
  visualization, or material bindings.

### Materials, Presentation, And Texture Slots

- A material/presentation record is data-only.
- A surface material is a collection of texture-capable slots and uniform
  defaults.
- Point and line/edge presentation records may use lighter property-buffer
  bindings, but they still follow the same descriptor/readiness pattern.
- A slot may be backed by:
  - a uniform default value
  - an authored texture asset
  - a generated texture asset
  - a property binding that can schedule a bake
  - a direct property-buffer binding for non-texture presentation paths
- Property bindings are abstract descriptors, not raw pointers.
- When a suitable property is bound to a texture slot, runtime schedules the
  required bake using the mesh texture coordinates.
- Any suitable property may feed any compatible slot:
  - `glm::vec3` can feed a normal-map slot.
  - `glm::vec3` / `glm::vec4` can feed an albedo/color slot.
  - Scalar properties can feed scalar-field, roughness, metallic, displacement,
    or colormapped color slots.
- Mesh texture coordinates are mandatory as a target invariant for texture
  baking/sampling. If an imported mesh lacks valid UVs, runtime should schedule
  an xatlas atlas-generation job.
- A mesh without ready UVs still renders immediately with default material
  values and no UV-dependent texture bindings.
- Graph and point-cloud properties use property buffers for the first
  implementation; texture baking is not required for those domains.
- Point clouds are not expected to render with textures in the initial model.
- Roughness and metallic are separate logical UI slots. Runtime/graphics may
  pack them into a combined `MetallicRoughness` texture internally.

Candidate surface slot semantics:

```text
Albedo
Normal
Roughness
Metallic
MetallicRoughness
Emissive
Occlusion
Displacement
ScalarField
Custom
```

Candidate slot record:

```text
slot semantic
source domain
source property name
expected property type
bake mode
texture asset id, if available
uniform default value
colormap/range metadata, if scalar
readiness/provenance
dirty generation
```

### Asynchronous Derived Jobs

- Heavy derived computations run as async jobs/tasks.
- Jobs may run on CPU or GPU depending on the available implementation domain.
- Jobs are keyed by stable entity id, geometry domain, source generation, and
  output semantic.
- Jobs may depend on other jobs.
- Completed jobs may schedule follow-up jobs after inspecting their output.
- Follow-up scheduling must still record explicit graph edges so UI/debug state
  can explain why a job exists.
- Job application happens on the main thread.
- Stale results must be discarded when the entity, geometry, source property, or
  material binding generation changed.
- Jobs should be cancelled automatically when possible after source changes.
  Stale-result discard remains mandatory because cancellation may race.
- Progress is numeric when the algorithm can report useful progress; otherwise
  status text is acceptable.
- Failed jobs leave the previous valid output bound.
- Previous valid outputs are destroyed only after the next valid output is
  ready, using deferred destruction to avoid rendering hazards and unnecessary
  memory churn.
- Jobs must be visible in the UI per entity and in a global queue/debug view.

Examples:

- Compute mesh vertex normals.
- Generate mesh UV atlas.
- Bake a normal map from `v:normal`.
- Bake color/albedo from `v:color` or another vector property.
- Bake face colors into a texture when a surface material needs one.
- Bake scalar fields through a colormap.
- Bake roughness, metallic, or displacement from scalar properties.
- Upload generated texture assets.
- Bind generated texture assets to material slots.
- Upload property buffers for graph edge colors, graph node colors, point-cloud
  colors, point-cloud sizes, or point-cloud normals.

### Example: Mesh With Positions And Triangles Only

Expected flow:

- Import creates one mesh leaf entity.
- The entity has positions and triangle topology, but no normals, colors, or
  authored textures.
- If valid UVs are missing, runtime schedules an xatlas UV job.
- `RenderSurface` can render the triangle surface immediately with unshaded,
  flat, or default material values.
- `RenderPoints` can be added to render vertices.
- `RenderEdges` can be added to render wire edges.
- Runtime schedules `ComputeVertexNormals`.
- When normals are ready:
  - apply `v:normal` to the mesh vertex property domain
  - mark vertex attributes dirty
  - schedule `BakeTextureSlot(Normal, v:normal)` if the normal slot is bound to
    that property and UVs are ready
  - otherwise wait for the UV job
- When the normal-map bake finishes:
  - create/update a generated texture asset
  - request texture upload
  - bind the generated texture asset to the material normal slot
- On the next extraction/render frame, the surface lane uses the normal map.

The same pattern applies to vertex colors, face colors, scalar fields,
roughness, metallic, and displacement, subject to each slot's source type and
mapping requirements.

### Face, Edge, Node, And Point Presentation

- Mesh face colors and scalar fields need exact face-domain visualization.
- If a mesh material wants a texture for a face-domain source and the texture
  does not exist yet, render with a default/in-progress color, schedule the
  bake, and replace the slot when the valid texture is ready.
- Graph edge colors and scalar fields use shader lookup by edge id.
- The graph edge buffer must therefore be freely indexable by shader edge id.
- Graph node properties and point-cloud properties use property buffers.
- Point-cloud normals feed surfel orientation directly from a property buffer.
- Point-cloud normal data is not baked to texture.

### UI Contract

The selected-entity UI should expose:

- Entity shape:
  - model/composition entity
  - mesh leaf
  - graph leaf
  - point-cloud leaf
- Geometry domains and property tables.
- Render-lane toggles:
  - surface
  - edges/lines
  - points
- Per-lane material/presentation binding.
- Per-slot controls:
  - enabled/disabled
  - uniform default value
  - source property selector
  - authored/generated texture asset state
  - bake readiness
  - last diagnostic
- Color picker for the selected render lane's material albedo/default color
  slot.
- Source-property picker for each compatible texture/presentation slot.
- Property picker ordering:
  - compatible properties first
  - incompatible properties still visible with disabled state and reasons
- Per-entity derived jobs:
  - queued/running/waiting/applying/complete/failed/cancelled
  - dependencies
  - elapsed time
  - numeric progress when available
  - source generation
  - output semantic
  - diagnostic text
- Composition entities show aggregate child job/material status but not one
  editable inherited child-material table.

## Resolved Remaining Decisions

- Implementation should use shared descriptor types for property bindings, slot
  bindings, readiness state, generated outputs, and derived jobs, plus
  domain-specific adapters for mesh, graph, and point-cloud packing, baking, and
  presentation.
- Render-lane components should remain primitive/render toggles. A separate
  per-entity material/presentation binding component maps lane type to
  presentation key.
- First implementation surface slots:
  - albedo
  - normal
  - roughness
  - metallic
  - scalar field
- Displacement is descriptor-only in the first implementation. It must not be
  treated as operational until bounds, culling, shader, and backend implications
  are handled by a later task.
- First implementation point slots:
  - color
  - scalar field
  - size
  - normal/orientation
- First implementation line/edge slots:
  - color
  - scalar field
  - width
- Generated output policy is controlled per slot, with these defaults:
  - generated textures default to deterministic child assets
  - generated property buffers default to session caches
  - later persistent policy values should include `SessionCache`,
    `DeterministicChildAsset`, and `PersistOnSave`
- The first implementation should define GPU-capable job-domain metadata
  (`Cpu`, `GpuCompute`, `GpuGraphics`, `Auto`) but implement CPU jobs first.
  GPU job domains should report deterministic unavailable diagnostics until a
  concrete GPU backend task lands.
- Parent/composition transforms compose hierarchically with child local
  transforms.
- Parent/composition material defaults inherit only into child leaf slots that
  are unset. Explicit child settings always win.
- UV atlas generation is automatic after import for every missing-UV mesh, but
  the job runs asynchronously after raw geometry becomes visible.
- Pending generated outputs render with slot defaults and UI status. Checker or
  fallback textures are reserved for actual missing/error states, not normal
  in-progress work.
- Serialization should persist material/presentation bindings and generated
  output policy. Transient job state is never serialized.
- The implementation should add an ADR before engine code changes because
  entity composition, material binding, and derived-job ownership are hard to
  reverse and cross multiple runtime/render/UI boundaries.

## Required changes
- [x] Resolve answered decisions in this clarification task.
- [x] Resolve the remaining open questions.
- [x] Decide whether the final architecture needs an ADR before implementation.
- [x] Split implementation into reviewable follow-up tasks for ECS/data
      contracts, runtime derived-job scheduling, render extraction/material
      binding, and UI.
- [x] Define a first CPU-contracted slice that proves mesh, graph, and
      point-cloud domains are all represented in the same model.

## Tests
- [x] No implementation tests are required for this clarification-only update.
- [x] Follow-up implementation tasks include CPU/null contract tests for
      entity-shape, material/property binding, job state, stale-result discard,
      and UI model visibility.

## Docs
- [x] Update this file as questions are answered.
- [x] Add an ADR if the accepted model changes entity composition, material
      binding, or derived-job ownership in a hard-to-reverse way.
- [x] Update `src/runtime/README.md`, renderer docs, and UI docs in follow-up
      implementation tasks.

## Acceptance criteria
- [x] Entity composition semantics are decided.
- [x] Render-lane naming and material/presentation binding ownership are
      decided at the intent level.
- [x] General abstraction vs domain-specific adapter boundary is confirmed.
- [x] Mesh UV requirement timing is decided at the intent level.
- [x] Graph and point-cloud property rendering paths are decided.
- [x] Derived-job scheduling, follow-up scheduling, cancellation, and stale
      apply behavior are decided at the intent level.
- [x] UI material-slot/property-picker behavior is decided at the intent level.
- [x] Follow-up implementation tasks are created or this task is promoted to an
      active implementation umbrella.

## Verification
```bash
python3 tools/agents/generate_session_brief.py
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
git diff --check
```

Completed verification:

- `python3 tools/agents/generate_session_brief.py` — passed; regenerated
  `tasks/SESSION-BRIEF.md`.
- `python3 tools/agents/check_task_policy.py --root . --strict` — passed;
  427 task files validated with no maturity follow-up findings.
- `python3 tools/docs/check_doc_links.py --root .` — passed; 1903 relative
  links checked.
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main`
  — passed; triggered docs-sync rules satisfied.
- `git diff --check` — passed.

## Forbidden changes
- Implementing code before the follow-up tasks and ADR are written.
- Treating meshes as the only first-class geometry domain.
- Storing graphics-owned GPU handles in ECS components.
- Persisting raw property pointers instead of stable property binding
  descriptors.
- Mutating authored data properties for transient selection/highlight state.

## Maturity
- Target: `Scaffolded` planning contract.
- This task does not close an implementation gate. `CPUContracted` behavior is
  owned first by `RUNTIME-111`, then by `RUNTIME-112`, `RUNTIME-113`,
  `RUNTIME-114`, and `UI-015`; backend operational evidence is owned by
  `GRAPHICS-090`.
