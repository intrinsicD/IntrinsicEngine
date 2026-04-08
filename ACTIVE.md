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
