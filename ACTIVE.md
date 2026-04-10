# Active Architecture Notes

Use this file as a quick “what does what, and where should it live?” reference.

## Engine

- **Role:** composition root and application frame-loop owner.
- **Owns:** the top-level runtime subsystems and their wiring.
- **Does:** startup/shutdown, frame orchestration, and cross-subsystem coordination.
- **Should not own:** feature logic, scene content, or render/asset internals.

### Put work here when

- it changes app lifetime or main-loop flow;
- it wires subsystems together;
- it needs direct access to owned runtime services.

## Scene

- **Role:** runtime world state.
- **Contains:** entities, components, and scene-local systems/data.
- **Entities:** identifiers that carry component sets.
- **Components:** plain data attached to entities.
- **Scene events:** local notifications for scene changes.

### Put work here when

- it is per-entity state;
- it belongs to gameplay/editor content;
- it needs ECS queries or component updates.

## Systems

- **Role:** plain functions or function-like units that operate on scene data.
- **Active system:** runs on matching entities and reacts to registered events.
- **Inactive system:** exists in the scene but does not execute or listen.
- **Events:** systems can subscribe through the scene’s `entt::dispatcher`.
- **State:** keep long-lived state in components or owning subsystems, not inside systems.

### Use systems for

- per-frame updates;
- event reactions;
- entity/component processing;
- scene-scoped validation or sync.

### Do not use systems for

- ownership of GPU/device resources;
- asset database policy;
- UI state management;
- broad application orchestration.

## Modules

- **Meaning:** C++23 named modules, not “feature folders.”
- **Rule:** one module for one responsibility boundary.
- **Interface:** `.cppm`
- **Implementation:** `.cpp`
- **Layering:** keep dependencies one-way and prefer the smallest useful import.

### Practical placement

- `Core` — utilities, memory, tasks, telemetry, low-level shared types.
- `ECS` — scene/component infrastructure.
- `Geometry` — algorithms and data structures for meshes, graphs, point clouds.
- `RHI` — graphics/driver abstraction.
- `Graphics` — rendering, GPU upload, frame graph, scene upload/sync.
- `Runtime` — composition layer that wires the engine together.
- `Interface` — editor/UI presentation and input plumbing.

## GUI

- **Role:** presentation and user input surface.
- **Does:** render widgets, collect user choices, and emit parameter changes or commands.
- **Should not do:** core simulation, render ownership, asset ownership, or scene mutation logic by itself.

### Put work here when

- the user edits a parameter struct;
- a button, toggle, or panel should trigger an action;
- you need to expose current state for inspection.

## Quick routing rules

- **Scene state?** put it in components.
- **Cross-system notification?** use `entt::dispatcher`.
- **Per-entity dirty tracking?** use tags/components.
- **Cleanup on removal?** use `on_destroy` hooks.
- **Main-loop / subsystem wiring?** put it in `Engine` or `Runtime`.
- **User-facing controls?** put them in `GUI`, then pass commands/events to the owning system.

## What i want to do next

- The code should be organized cleaner and more modular.
- Application:
  - Engine:
    - FeatureModules:
      - SelectionModule (handles all selection logic and state: entity, face, edge and vertex selection)
      - CameraModule (handles all camera logic and state: active camera, camera controls and camera events)
      - MeshProcessingModule
      - GraphProcessingModule
      - PointCloudProcessingModule
      - LightingModule
      - PostProcessingModule
    - AssetModule:
      - AssetManager (holds active asset instances and their lifetimes)
      - AssetStreamingSystem (is responsible for loading/streaming assets)
    - Scene (holds active entities and their components)
    - TaskGraph
    - RenderGraph
  - Gui:
    - MainMenu
    - Active Widgets


How Rendering should work:
- Core rule:
  - keep topology and attributes separate.
  - topology lives in shared BDA-backed geometry buffers.
  - large per-element attributes live in buffer-backed SoA tables indexed by element id (`vertex`, `edge`, `face`, `vector`).
  - use textures for sampled image data, UV/material lookups, or explicitly atlas-backed tooling; do not treat 2D textures as the default storage for arbitrary geometry attributes.
- Pass ownership:
  - `SurfacePass` renders filled mesh triangles.
  - `LinePass` renders mesh edges, graph edges, and vector-field overlays.
  - `PointPass` renders graph nodes, point clouds, and other point-domain markers.
- Meshes:
  - require vertex positions plus triangle indices.
  - vertex-domain mode: smooth lighting with interpolated normals; use vertex attributes for normals, colors, and UV lookup chains.
  - face-domain mode: flat lighting; use face attributes for normals, colors, and other per-face data.
  - render-time choice is a shading/attribute selection, not a separate geometry ownership model.
- Graphs:
  - require vertex positions plus edge indices.
  - node-domain data is rendered as points when the visual form is node-centered.
  - edge-domain data is rendered as lines when the visual form is edge-centered.
  - keep edge attributes and node attributes in separate indexed tables; do not collapse them into a mesh-style texture atlas.
  - use flat lighting for graph lines; any "normal" or "tangent" contribution is a line-shading convention, not a surface model.
- Point clouds:
  - require vertex positions; all other attributes are vertex-indexed.
  - render modes stay inside `PointPass`: flat points, surfels, sphere impostors, and Gaussian/EWA splats.
  - surfels and impostors use point normals when available; if normals are missing, fall back to the simplest stable point mode.
  - Gaussian splats require covariance data and should degrade safely when the data is absent or ill-conditioned.
- Vector fields:
  - represent each vector as a base position plus a target position (`target = base + vector`).
  - store vector attributes in indexed buffer-backed tables, not in a separate geometry pipeline.
  - normalize/scalar-scale in the shader when needed for consistent length controls.
  - render vector fields as line overlays, with either uniform color or per-vector color.
- Practical rule of thumb:
  - if a datum changes per vertex/edge/face/vector, give it a dedicated indexed attribute buffer.
  - if a datum is sampled like an image, use a texture.
  - if the visual primitive is a triangle, line, or point, route it through the matching primitive pass instead of adding a new render lane.
